#include "doca_pipelines.hpp"

extern "C" {
#include <doca_flow_external_action_array.h>
#include <doca_flow_external_actions.h>
#include <doca_flow.h>
#include <doca_flow_mp.h>
}

#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

/**
 * Declarative DOCA Flow pipe builder.
 *
 * Provides three builder classes that abstract away the doca_flow_pipe_cfg lifecycle,
 * monitor setup, entry commit loop, and forwarding boilerplate, so callers declare
 * what a pipe does rather than how.
 *
 * Usage pattern:
 *   doca_flow_pipe *p = nullptr;
 *   doca_error_t r = pb::BasicPipeBuilder("MY_PIPE")
 *       .root()
 *       .match(pb::MatchIPv4UDP{})
 *       .action(pb::ActionSetMeta{5, 800})
 *       .copies(&extra_copies)
 *       .fwd(pb::FwdOL{next_pipe, 0})
 *       .miss(pb::FwdDrop{})
 *       .build_single(port, &p);
 */
namespace pb {

/** Packet-match template for BASIC pipes. */
struct MatchIPv4    {};                      ///< outer L3 = IPv4 only
struct MatchIPv4UDP {};                      ///< outer L3 = IPv4, L4 = UDP
struct MatchIPv4Meta { int u32_index; };     ///< IPv4 + meta.u32[u32_index], full 32-bit mask per entry
/**
 * Match only on meta.u32[u32_index] with a full 32-bit mask — NO parser_meta field.
 *
 * Prefer this over MatchIPv4Meta for terminal dispatch pipes (e.g. RSS_STEER) where
 * the upstream ROOT already guarantees the L3/L4 type.  Without parser_meta in the
 * template the match is simpler.
 */
struct MatchMeta    { int u32_index; };      ///< meta.u32[u32_index] only, full 32-bit mask per entry

/**
 * Match on meta.u32[u32_index] with a partial bit mask — NO parser_meta field.
 *
 * The hardware compares only the bits selected by `mask`.  Entry i matches when
 * (meta.u32[u32_index] & mask) == (i & mask).
 *
 * Use for true round-robin dispatch where the counter is free-running: with
 * mask = nb_queues - 1 (power-of-2 queue count) the lower log2(N) bits cycle
 * 0..N-1 indefinitely regardless of how large the counter grows.
 *
 * nb_queues must be a power of 2.
 */
struct MatchMetaMasked { int u32_index; uint32_t mask; }; ///< meta.u32[u32_index] & mask, per entry

/**
 * IPv4 + UDP, exact match on outer UDP destination port.
 *
 * Pipe template: parser_meta IPv4/UDP + full mask on outer.udp.l4_port.dst_port.
 * build_single entry: dst_port = be16(dport).
 *
 * Use for single-port observer pipes (e.g. RoCE v2 on 4791) where each entry
 * pins one specific port value.
 */
struct MatchIPv4UDPDport { uint16_t dport; };

/** outer L3 = IPv4, L4 = ESP (protocol 50). DOCA's parser recognizes ESP as a
 *  first-class L4 type (DOCA_FLOW_L4_META_ESP), same as TCP/UDP/ICMP. */
struct MatchIPv4ESP {};

/**
 * Match on the IPsec ESP header's Sequence Number field (tun.esp_sn) with a
 * partial bit mask — NO parser_meta L4 field beyond what MatchIPv4ESP's own
 * template already carries at the ROOT.
 *
 * Entry i matches when (tun.esp_sn & mask) == (i & mask). With mask =
 * nb_queues - 1 (power-of-2 queue count) the lower log2(N) bits cycle 0..N-1
 * as the sender's SN increments — same masked-round-robin pattern as
 * MatchMetaMasked, but the field being matched is assigned by the traffic's
 * *sender*, never computed by NIC-internal hardware, so it isn't subject to
 * the same class of internal-engine reordering a NIC-computed counter is.
 */
struct MatchEspSnMasked { uint32_t mask; };

/**
 * Match on meta.pkt_meta (not meta.u32[]) with a partial bit mask — NO
 * parser_meta field. Same masked-round-robin pattern as MatchMetaMasked, but
 * targeting pkt_meta specifically: this is the one metadata field DOCA Flow
 * exposes to the application (via the mlx5 PMD's generic flow-metadata
 * dynfield mechanism, gated on rte_flow_dynf_metadata_register() —
 * see drivers/net/mlx5/mlx5_rx.c's rxq_cq_to_mbuf()) — the u32[] scratch
 * registers are internal-only and never reach the mbuf. Needed for any
 * pipeline stage that must match on a value *after* it's been copied into
 * pkt_meta (e.g. steering on an already-decapped packet's original tunnel
 * field, which no longer exists post-decap — see doca_pipelines_esp_rr.cpp).
 */
struct MatchPktMetaMasked { uint32_t mask; };

using MatchSpec = std::variant<MatchIPv4, MatchIPv4UDP, MatchIPv4Meta, MatchMeta,
                               MatchIPv4UDPDport, MatchMetaMasked, MatchIPv4ESP,
                               MatchEspSnMasked, MatchPktMetaMasked>;

/** Forward destination variants. */
struct FwdRSSAll { int nb_queues; uint32_t flags = DOCA_FLOW_RSS_UDP; }; ///< RSS to queues 0..nb_queues-1
struct FwdRSSOne { int queue_id;  uint32_t flags = DOCA_FLOW_RSS_AUTO;}; ///< RSS to one specific queue
struct FwdPipe   { struct doca_flow_pipe *pipe; };                       ///< FWD_PIPE
struct FwdOL     { struct doca_flow_pipe *pipe; int slot; };             ///< FWD_ORDERED_LIST_PIPE at slot
struct FwdPort   { uint16_t port_id = 0; };                              ///< FWD_PORT
struct FwdDrop   {};                                                     ///< FWD_DROP

using FwdSpec = std::variant<FwdRSSAll, FwdRSSOne, FwdPipe, FwdOL, FwdPort, FwdDrop>;

/** Write meta.u32[u32_index] = value (builder encodes BE32). */
struct ActionSetMeta { int u32_index; uint32_t value; };

/** DIAGNOSTIC: write meta.pkt_meta = value (constant), same pattern as
 *  ActionSetMeta but targeting pkt_meta directly -- used to isolate whether
 *  writing pkt_meta at all (via any mechanism) reaches DOCA's own matching
 *  and the mlx5 dynfield, independent of the COPY-from-a-real-field question. */
struct ActionSetPktMeta { uint32_t value; };

/**
 * Copy the matched packet's ESP Sequence Number (tun.esp_sn) into
 * meta.pkt_meta, via a DOCA_FLOW_ACTION_COPY action_desc — the mechanism
 * that lets the value survive into the mbuf on the DPDK side (see
 * mlx5_flow_rxq_dynf_set()/rxq_cq_to_mbuf() in drivers/net/mlx5, gated on
 * rte_flow_dynf_metadata_register() being called).
 *
 * UNVERIFIED: the source field_string below ("tunnel.esp.sn") follows the
 * documented <location>.<protocol>.<field> convention (doca_flow.h's
 * doca_flow_desc_field comment, e.g. "tunnel.gre.protocol") but has no
 * confirmed precedent anywhere in this codebase — the only known-working
 * COPY example (AsoBlockBuilder) copies within the meta scratchpad itself
 * ("meta.data"), not a real header field. Verify against DOCA Flow
 * documentation or empirical testing before trusting this compiles/works as
 * intended; a build or runtime failure here likely means the string is
 * wrong, not that the mechanism is unsupported.
 */
struct ActionCopyEspSnToMeta {};

/**
 * Strip an outer header down to whatever DOCA's decap_cfg considers the
 * "inner" packet, via doca_flow_actions.decap_type/decap_cfg.
 *
 * UNVERIFIED, flagged explicitly rather than assumed: doca_flow_resource_decap_cfg
 * (is_l2/eth/eth_vlan) is documented in general L2/L3-tunnel terms (e.g.
 * VXLAN/GRE-style decap) — it is NOT confirmed here to correctly strip an
 * ESP header's own SPI+SN framing specifically, nor is it confirmed that the
 * generic decap path (vs. doca_flow_crypto.h's DOCA_FLOW_CRYPTO_REFORMAT_DECAP,
 * a different, crypto/SA-resource-bound mechanism) is even the right one for
 * ESP. `is_l2 = false` is used here on the assumption that decap needs to
 * rebuild a real Ethernet header for the resulting inner packet (since
 * examples/l3fwd downstream expects standard Ethernet framing) — `eth` must
 * be supplied by the caller (this builder does not invent MAC addresses).
 * Confirm this whole action against real DOCA Flow documentation/hardware
 * before trusting the transmitted inner packet's framing is correct.
 */
struct ActionDecap {
    bool                        is_l2 = false;
    struct doca_flow_header_eth eth   = {}; ///< only used when is_l2 == false
};


/**
 * Builds a DOCA_FLOW_PIPE_BASIC pipe with a fluent interface.
 *
 * Handles automatically:
 *  - doca_flow_pipe_cfg create / set_* / pipe_create / destroy (RAII guard)
 *  - Non-shared counter monitor (enabled by default, disable with no_monitor())
 *  - Pipe-level fwd: FwdOL → FWD_ORDERED_LIST_PIPE; all others → FWD_CHANGEABLE
 *  - Entry add + doca_pipelines_process_entries
 *
 * build_per_queue:
 *  - Pipe is always CHANGEABLE. If match() is MatchIPv4Meta{u32_index}, each entry i
 *    automatically gets entry_match.meta.u32[u32_index] = be32(i).
 *  - fwd_fn(q) provides the FwdSpec for each entry q (FwdRSSOne or FwdDrop supported).
 */
class BasicPipeBuilder {
public:
    explicit BasicPipeBuilder(std::string name);

    BasicPipeBuilder &root(bool is_root = true);
    BasicPipeBuilder &domain(enum doca_flow_pipe_domain d);


    BasicPipeBuilder &match(MatchSpec m);

    /** Set meta.u32[u32_index] = value (host-order). May be called multiple times. */
    BasicPipeBuilder &action(ActionSetMeta a);

    /** DIAGNOSTIC: set meta.pkt_meta = value (constant, host-order). */
    BasicPipeBuilder &action(ActionSetPktMeta a);

    /** Copy the matched ESP SN into meta.pkt_meta. See ActionCopyEspSnToMeta's
     *  doc comment for the unverified field_string assumption this relies on. */
    BasicPipeBuilder &action(ActionCopyEspSnToMeta a);

    /** Strip the outer tunnel framing. See ActionDecap's doc comment for the
     *  unverified assumptions this relies on for ESP specifically. */
    BasicPipeBuilder &decap(ActionDecap a);

    /** Disable the non-shared counter monitor (it is on by default). */
    BasicPipeBuilder &no_monitor();

    /**
     * Pass nullptr for the actions template (actions_arr) in set_actions.
     * Use when the pipe has only COPY descriptors and no action template (e.g. RSS terminal).
     */
    BasicPipeBuilder &no_actions_template();

    /** Forwarding spec for build_single. Ignored by build_per_queue. */
    BasicPipeBuilder &fwd(FwdSpec f);

    /** Pipe-level miss forwarding (defaults to FwdDrop{}). */
    BasicPipeBuilder &miss(FwdSpec m);

    /**
     * Build pipe + 1 wildcard entry.
     * - FwdOL → pipe-level ordered-list fwd, entry fwd = nullptr (uses pipe default).
     * - All others → CHANGEABLE pipe, entry fwd = actual spec.
     *
     * @param out_entry  Optional; receives the entry pointer (e.g. for hit_counter_entry).
     */
    doca_error_t build_single(struct doca_flow_port *port,
                              struct doca_flow_pipe **out_pipe,
                              struct doca_flow_pipe_entry **out_entry = nullptr);

    /**
     * Build pipe + nb_queues per-queue entries (0..nb_queues-1).
     * Pipe uses CHANGEABLE fwd at pipe level.
     *
     * If match() is MatchIPv4Meta{u32_index}, each entry i gets:
     *   entry_match.parser_meta.outer_l3_type = IPv4
     *   entry_match.meta.u32[u32_index] = be32(i)
     *
     * fwd_fn(q) → FwdSpec for entry q. Supported: FwdRSSOne, FwdDrop.
     *
     * @param out_entries  Optional array[nb_queues] that receives per-entry pointers.
     */
    doca_error_t build_per_queue(struct doca_flow_port *port,
                                 int nb_queues,
                                 std::function<FwdSpec(int q)> fwd_fn,
                                 struct doca_flow_pipe **out_pipe,
                                 struct doca_flow_pipe_entry **out_entries = nullptr);

private:
    std::string                                    name_;
    bool                                           is_root_               = false;
    enum doca_flow_pipe_domain                     domain_                = DOCA_FLOW_PIPE_DOMAIN_DEFAULT;

    std::optional<MatchSpec>                       match_;
    std::vector<ActionSetMeta>                     actions_;
    std::optional<ActionSetPktMeta>                set_pkt_meta_;
    bool                                           copy_esp_sn_to_meta_   = false;
    std::optional<ActionDecap>                     decap_;
    bool                                           monitor_               = true;
    bool                                           null_actions_template_ = false;
    std::optional<FwdSpec>                         fwd_;
    std::optional<FwdSpec>                         miss_;

    /** Shared cfg create / set_* / pipe_create / cfg_destroy. */
    doca_error_t build_pipe(struct doca_flow_port *port,
                            int nr_entries,
                            bool force_changeable,
                            struct doca_flow_pipe **out_pipe);
};

/**
 * Builds a DOCA_FLOW_PIPE_ORDERED_LIST pipe with one Memory ASO external action.
 *
 * Validates that meta_index is even (required by the external action array engine).
 * Dynamic indexing: pass DYNAMIC (-1) for array_index to use the probed meta pair.
 */
class AsoBlockBuilder {
public:
    /** Sentinel for dynamic ASO array index (uses g_pipelines_ctx.dynamic_aso_*). */
    static constexpr int DYNAMIC = -1;

    explicit AsoBlockBuilder(std::string name);

    /** The Memory ASO operation (ADD, LOAD, …). */
    AsoBlockBuilder &op(enum doca_flow_external_action_array_operation operation);

    /** Even u32 index for the external action array I/O. */
    AsoBlockBuilder &meta_index(int idx);

    /** Static ASO array line index, or DYNAMIC for dynamic per-packet selection. */
    AsoBlockBuilder &array_index(int idx);

    /** Offset within the ASO resource (static indexing only). */
    AsoBlockBuilder &resource_offset(int off);

    /** Forward target after the ASO action: FwdPipe or FwdOL. */
    AsoBlockBuilder &fwd(FwdSpec f);

    /** Pipe domain (default = DOCA_FLOW_PIPE_DOMAIN_DEFAULT). */
    AsoBlockBuilder &domain(enum doca_flow_pipe_domain d);

    doca_error_t build(struct doca_flow_port *port,
                       struct doca_flow_external_resource_array *array_resource,
                       uint32_t array_resource_type,
                       struct doca_flow_pipe **out_pipe);

private:
    std::string                                        name_;
    enum doca_flow_external_action_array_operation     op_              = DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_ADD;
    int                                                meta_index_      = 0;
    int                                                array_index_     = 0;
    int                                                resource_offset_ = 0;
    std::optional<FwdSpec>                             fwd_;
    enum doca_flow_pipe_domain                         domain_          = DOCA_FLOW_PIPE_DOMAIN_DEFAULT;
};


} // namespace pb