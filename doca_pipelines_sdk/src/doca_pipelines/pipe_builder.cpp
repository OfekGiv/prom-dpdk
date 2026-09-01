#include "pipe_builder.hpp"

#include <algorithm>
#include <cstring>
#include <numeric>

#include <rte_byteorder.h>

extern doca_pipelines_ctx_t g_pipelines_ctx;

namespace pb {

// ============================================================
// Internal helpers
// ============================================================

/** RAII guard: destroys a doca_flow_pipe_cfg on scope exit. */
struct PipeCfgGuard {
    struct doca_flow_pipe_cfg *cfg = nullptr;
    ~PipeCfgGuard()
    {
        if (cfg) {
            (void)doca_flow_pipe_cfg_destroy(cfg);
            cfg = nullptr;
        }
    }
};

/**
 * Translate FwdSpec → pipe-level doca_flow_fwd.
 * Returns true  when the fwd is fixed at pipe level (FwdOL).
 * Returns false when CHANGEABLE is set (all other types need per-entry fwd).
 */
static bool to_pipe_fwd(const FwdSpec &spec, struct doca_flow_fwd &out)
{
    memset(&out, 0, sizeof(out));
    if (std::holds_alternative<FwdOL>(spec)) {
        const auto &ol       = std::get<FwdOL>(spec);
        out.type             = DOCA_FLOW_FWD_ORDERED_LIST_PIPE;
        out.ordered_list_pipe.pipe = ol.pipe;
        out.ordered_list_pipe.idx  = static_cast<uint32_t>(ol.slot);
        return true;  // fixed pipe-level fwd
    }
    out.type = DOCA_FLOW_FWD_CHANGEABLE;
    return false;  // entry-level fwd required
}

/**
 * Translate FwdSpec → entry-level doca_flow_fwd.
 * rss_storage must remain alive through doca_flow_entries_process.
 * Returns false for FwdOL (use pipe-level default, entry fwd = nullptr).
 */
static bool to_entry_fwd(const FwdSpec &spec,
                          struct doca_flow_fwd &out,
                          std::vector<uint16_t> &rss_storage)
{
    memset(&out, 0, sizeof(out));

    if (std::holds_alternative<FwdOL>(spec)) {
        return false;  // nullptr entry fwd; pipe-level ordered-list handles it
    }
    if (std::holds_alternative<FwdPipe>(spec)) {
        out.type      = DOCA_FLOW_FWD_PIPE;
        out.next_pipe = std::get<FwdPipe>(spec).pipe;
        return true;
    }
    if (std::holds_alternative<FwdRSSAll>(spec)) {
        const auto &r = std::get<FwdRSSAll>(spec);
        rss_storage.resize(static_cast<size_t>(r.nb_queues));
        std::iota(rss_storage.begin(), rss_storage.end(), uint16_t{0});
        out.type             = DOCA_FLOW_FWD_RSS;
        out.rss_type         = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;
        out.rss.queues_array = rss_storage.data();
        out.rss.nr_queues    = static_cast<uint16_t>(r.nb_queues);
        out.rss.outer_flags  = r.flags;
        return true;
    }
    if (std::holds_alternative<FwdRSSOne>(spec)) {
        const auto &r = std::get<FwdRSSOne>(spec);
        rss_storage.resize(1);
        rss_storage[0]       = static_cast<uint16_t>(r.queue_id);
        out.type             = DOCA_FLOW_FWD_RSS;
        out.rss_type         = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;
        out.rss.queues_array = rss_storage.data();
        out.rss.nr_queues    = 1;
        out.rss.outer_flags  = r.flags;
        return true;
    }
    if (std::holds_alternative<FwdPort>(spec)) {
        out.type    = DOCA_FLOW_FWD_PORT;
        out.port_id = std::get<FwdPort>(spec).port_id;
        return true;
    }
    /* FwdDrop */
    out.type = DOCA_FLOW_FWD_DROP;
    return true;
}

/** Translate FwdSpec → pipe-miss or control-entry doca_flow_fwd (direct, no CHANGEABLE). */
static void to_direct_fwd(const FwdSpec &spec, struct doca_flow_fwd &out)
{
    memset(&out, 0, sizeof(out));
    if (std::holds_alternative<FwdDrop>(spec)) {
        out.type = DOCA_FLOW_FWD_DROP;
    } else if (std::holds_alternative<FwdPipe>(spec)) {
        out.type      = DOCA_FLOW_FWD_PIPE;
        out.next_pipe = std::get<FwdPipe>(spec).pipe;
    } else if (std::holds_alternative<FwdOL>(spec)) {
        const auto &ol             = std::get<FwdOL>(spec);
        out.type                   = DOCA_FLOW_FWD_ORDERED_LIST_PIPE;
        out.ordered_list_pipe.pipe = ol.pipe;
        out.ordered_list_pipe.idx  = static_cast<uint32_t>(ol.slot);
    } else {
        out.type = DOCA_FLOW_FWD_DROP;
    }
}

// ============================================================
// BasicPipeBuilder
// ============================================================

BasicPipeBuilder::BasicPipeBuilder(std::string name) : name_(std::move(name)) {}

BasicPipeBuilder &BasicPipeBuilder::root(bool v)                              { is_root_ = v;   return *this; }
BasicPipeBuilder &BasicPipeBuilder::domain(enum doca_flow_pipe_domain d)      { domain_  = d;   return *this; }
BasicPipeBuilder &BasicPipeBuilder::match(MatchSpec m)                        { match_   = m;   return *this; }
BasicPipeBuilder &BasicPipeBuilder::action(ActionSetMeta a)                   { actions_.push_back(a); return *this; }
BasicPipeBuilder &BasicPipeBuilder::action(ActionSetPktMeta a)                { set_pkt_meta_ = a; return *this; }
BasicPipeBuilder &BasicPipeBuilder::action(ActionCopyEspSnToMeta)             { copy_esp_sn_to_meta_ = true; return *this; }
BasicPipeBuilder &BasicPipeBuilder::decap(ActionDecap a)                      { decap_ = a; return *this; }
BasicPipeBuilder &BasicPipeBuilder::no_monitor()                              { monitor_ = false; return *this; }
BasicPipeBuilder &BasicPipeBuilder::no_actions_template()                     { null_actions_template_ = true; return *this; }
BasicPipeBuilder &BasicPipeBuilder::fwd(FwdSpec f)                            { fwd_  = f; return *this; }
BasicPipeBuilder &BasicPipeBuilder::miss(FwdSpec m)                           { miss_ = m; return *this; }

doca_error_t BasicPipeBuilder::build_pipe(struct doca_flow_port *port,
                                           int nr_entries,
                                           bool force_changeable,
                                           struct doca_flow_pipe **out_pipe)
{
    /* --- Match --- */
    struct doca_flow_match match      = {};
    struct doca_flow_match match_mask = {};
    bool has_mask = false;

    if (match_.has_value()) {
        std::visit([&](auto &&m) {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, MatchIPv4>) {
                match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
            } else if constexpr (std::is_same_v<T, MatchIPv4UDP>) {
                match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
                match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_UDP;
            } else if constexpr (std::is_same_v<T, MatchIPv4Meta>) {
                match.parser_meta.outer_l3_type      = DOCA_FLOW_L3_META_IPV4;
                match_mask.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
                match.meta.u32[m.u32_index]          = 0xffffffffu;
                match_mask.meta.u32[m.u32_index]     = 0xffffffffu;
                has_mask = true;
            } else if constexpr (std::is_same_v<T, MatchMeta>) {
                /* Meta-only: no parser_meta field → hash table, not TCAM. */
                match.meta.u32[m.u32_index]      = 0xffffffffu;
                match_mask.meta.u32[m.u32_index] = 0xffffffffu;
                has_mask = true;
            } else if constexpr (std::is_same_v<T, MatchMetaMasked>) {
                /* Partial-mask meta match: only the bits selected by mask are compared.
                 * Template marks the field changeable (0xffffffff); mask restricts
                 * the comparison to the lower log2(N) bits for power-of-2 RR. */
                match.meta.u32[m.u32_index]      = 0xffffffffu;
                match_mask.meta.u32[m.u32_index] = rte_cpu_to_be_32(m.mask);
                has_mask = true;
            } else if constexpr (std::is_same_v<T, MatchIPv4UDPDport>) {
                /* IPv4/UDP + exact dport: dport value comes from the entry, mask lives in the template. */
                match.parser_meta.outer_l3_type                = DOCA_FLOW_L3_META_IPV4;
                match.parser_meta.outer_l4_type                = DOCA_FLOW_L4_META_UDP;
                match_mask.outer.udp.l4_port.dst_port          = 0xFFFF;
                has_mask = true;
            } else if constexpr (std::is_same_v<T, MatchIPv4ESP>) {
                match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
                match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_ESP;
            } else if constexpr (std::is_same_v<T, MatchEspSnMasked>) {
                /* Partial-mask match on the ESP Sequence Number (cleartext by
                 * RFC 4303, no bound crypto/SA resource needed -- same
                 * mechanism as VXLAN VNI / GRE key matching). Template marks
                 * the field changeable; mask restricts the comparison to the
                 * lower log2(N) bits for power-of-2 RR, same pattern as
                 * MatchMetaMasked but on a TX-assigned field instead of a
                 * NIC-computed one. */
                match.tun.type               = DOCA_FLOW_TUN_ESP;
                match.tun.esp_sn             = 0xffffffffu;
                match_mask.tun.type          = DOCA_FLOW_TUN_ESP;
                match_mask.tun.esp_sn        = rte_cpu_to_be_32(m.mask);
                has_mask = true;
            } else if constexpr (std::is_same_v<T, MatchPktMetaMasked>) {
                match.meta.pkt_meta      = 0xffffffffu;
                match_mask.meta.pkt_meta = rte_cpu_to_be_32(m.mask);
                has_mask = true;
            }
        }, *match_);
    }

    /* --- Actions --- */
    struct doca_flow_actions              actions      = {};
    struct doca_flow_actions             *actions_arr[1] = {&actions};
    struct doca_flow_actions              actions_mask = {};
    struct doca_flow_actions             *actions_mask_arr[1] = {&actions_mask};
    struct doca_flow_action_descs         descs      = {};
    struct doca_flow_action_descs        *descs_arr[1] = {&descs};
    struct doca_flow_action_desc          copy_desc  = {};

    for (const auto &a : actions_) {
        actions.meta.u32[a.u32_index] = rte_cpu_to_be_32(a.value);
    }

    if (set_pkt_meta_.has_value()) {
        /* DIAGNOSTIC: same literal-value pattern as ActionSetMeta above,
         * just targeting pkt_meta -- no action_desc/mask needed since this
         * is a constant, not a copy. */
        actions.meta.pkt_meta = rte_cpu_to_be_32(set_pkt_meta_->value);
    }

    if (copy_esp_sn_to_meta_) {
        /*
         * Mark the destination as an active/changeable action field via the
         * actions MASK (mirroring the match/match_mask convention) -- unlike
         * ActionSetMeta, the COPY destination's actual value comes from the
         * action_desc below, not a literal in `actions` itself, so `actions`
         * must NOT also carry a value for this field (that would conflict
         * with the copy and was confirmed to fail entry-add with
         * DOCA_ERROR_INVALID_VALUE during hardware testing).
         */
        actions_mask.meta.pkt_meta = 0xffffffffu;
        /* UNVERIFIED source field_string -- see ActionCopyEspSnToMeta's doc
         * comment in pipe_builder.hpp. */
        copy_desc.type                      = DOCA_FLOW_ACTION_COPY;
        copy_desc.field_op.src.field_string = "tunnel.esp.sn";
        copy_desc.field_op.src.bit_offset   = 0;
        copy_desc.field_op.dst.field_string = "meta.data";
        copy_desc.field_op.dst.bit_offset   = 0; /* meta.pkt_meta precedes u32[]: offset 0 */
        copy_desc.field_op.width            = 32;
        descs.desc_array   = &copy_desc;
        descs.nb_action_desc = 1;
    }

    if (decap_.has_value()) {
        /* UNVERIFIED -- see ActionDecap's doc comment in pipe_builder.hpp. */
        actions.decap_type          = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;
        actions.decap_cfg.is_l2     = decap_->is_l2;
        actions.decap_cfg.eth       = decap_->eth;
    }

    /* --- Monitor --- */
    struct doca_flow_monitor monitor = {};
    if (monitor_) {
        monitor.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;
    }

    /* --- Pipe-level fwd --- */
    struct doca_flow_fwd pipe_fwd = {};
    if (force_changeable) {
        pipe_fwd.type = DOCA_FLOW_FWD_CHANGEABLE;
    } else if (fwd_.has_value()) {
        to_pipe_fwd(*fwd_, pipe_fwd);
    }

    /* --- Pipe miss --- */
    struct doca_flow_fwd pipe_miss = {};
    if (miss_.has_value()) {
        to_direct_fwd(*miss_, pipe_miss);
    } else {
        pipe_miss.type = DOCA_FLOW_FWD_DROP;
    }

    /* --- CFG create / set / pipe_create --- */
    PipeCfgGuard   guard;
    doca_error_t   result = DOCA_SUCCESS;

#define LB_PB_CKV(step)                                                        \
    if (result != DOCA_SUCCESS) {                                              \
        RTE_LOG(ERR, DOCA_PIPELINES, "pipe_builder(%s): \n" step ": %s",                          \
                   name_.c_str(), doca_error_get_descr(result));               \
        return result;                                                         \
    }

    result = doca_flow_pipe_cfg_create(&guard.cfg, port);
    LB_PB_CKV("cfg_create");
    result = doca_flow_pipe_cfg_set_name(guard.cfg, name_.c_str());
    LB_PB_CKV("set_name");
    result = doca_flow_pipe_cfg_set_type(guard.cfg, DOCA_FLOW_PIPE_BASIC);
    LB_PB_CKV("set_type");
    result = doca_flow_pipe_cfg_set_is_root(guard.cfg, is_root_);
    LB_PB_CKV("set_is_root");
    if (domain_ != DOCA_FLOW_PIPE_DOMAIN_DEFAULT) {
        result = doca_flow_pipe_cfg_set_domain(guard.cfg, domain_);
        LB_PB_CKV("set_domain");
    }
    if (nr_entries > 0) {
        result = doca_flow_pipe_cfg_set_nr_entries(guard.cfg, static_cast<uint32_t>(nr_entries));
        LB_PB_CKV("set_nr_entries");
    }
    result = doca_flow_pipe_cfg_set_match(guard.cfg, &match, has_mask ? &match_mask : nullptr);
    LB_PB_CKV("set_match");
    if (monitor_) {
        result = doca_flow_pipe_cfg_set_monitor(guard.cfg, &monitor);
        LB_PB_CKV("set_monitor");
    }
    result = doca_flow_pipe_cfg_set_actions(guard.cfg,
                                            null_actions_template_ ? nullptr : actions_arr,
                                            copy_esp_sn_to_meta_ ? actions_mask_arr : nullptr,
                                            descs_arr, 1);
    LB_PB_CKV("set_actions");
    result = doca_flow_pipe_create(guard.cfg, &pipe_fwd, &pipe_miss, out_pipe);
    LB_PB_CKV("pipe_create");

#undef LB_PB_CKV
    return DOCA_SUCCESS;
}

doca_error_t BasicPipeBuilder::build_single(struct doca_flow_port *port,
                                             struct doca_flow_pipe **out_pipe,
                                             struct doca_flow_pipe_entry **out_entry)
{
    if (!fwd_.has_value()) {
        RTE_LOG(ERR, DOCA_PIPELINES, "pipe_builder(%s): build_single requires .fwd()\n", name_.c_str());
        return DOCA_ERROR_INVALID_VALUE;
    }

    /* FwdOL → fixed pipe-level fwd; all others → CHANGEABLE + entry-level fwd. */
    bool force_changeable = !std::holds_alternative<FwdOL>(*fwd_);
    doca_error_t result = build_pipe(port, 1, force_changeable, out_pipe);
    if (result != DOCA_SUCCESS) return result;

    /* Build entry-level fwd (lifetime: must outlive entries_process). */
    struct doca_flow_fwd  entry_fwd  = {};
    std::vector<uint16_t> rss_storage;
    bool needs_entry_fwd = to_entry_fwd(*fwd_, entry_fwd, rss_storage);

    struct doca_flow_match      entry_match = {};
    struct doca_flow_pipe_entry *hit_entry  = nullptr;

    /* For MatchIPv4UDPDport the entry carries the concrete dport value. */
    if (match_.has_value()) {
        std::visit([&](auto &&m) {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, MatchIPv4UDPDport>) {
                entry_match.outer.udp.l4_port.dst_port = rte_cpu_to_be_16(m.dport);
            }
        }, *match_);
    }

    g_pipelines_ctx.batch.reset();

    result = doca_flow_pipe_basic_add_entry(
        0, *out_pipe, &entry_match, 0, nullptr, nullptr,
        needs_entry_fwd ? &entry_fwd : nullptr,
        DOCA_FLOW_ENTRY_FLAGS_NO_WAIT,
        &g_pipelines_ctx.batch, &hit_entry);
    if (result != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "pipe_builder(%s): add_entry: %s\n", name_.c_str(), doca_error_get_descr(result));
        (void)doca_flow_pipe_destroy(*out_pipe);
        *out_pipe = nullptr;
        return result;
    }

    result = doca_pipelines_process_entries(port, 1);
    if (result != DOCA_SUCCESS) {
        (void)doca_flow_pipe_destroy(*out_pipe);
        *out_pipe = nullptr;
        return result;
    }

    if (out_entry) *out_entry = hit_entry;
    return DOCA_SUCCESS;
}

doca_error_t BasicPipeBuilder::build_per_queue(struct doca_flow_port *port,
                                                int nb_queues,
                                                std::function<FwdSpec(int q)> fwd_fn,
                                                struct doca_flow_pipe **out_pipe,
                                                struct doca_flow_pipe_entry **out_entries)
{
    if (nb_queues <= 0 || nb_queues > 256) {
        RTE_LOG(ERR, DOCA_PIPELINES, "pipe_builder(%s): build_per_queue: invalid nb_queues=%d\n",
                   name_.c_str(), nb_queues);
        return DOCA_ERROR_INVALID_VALUE;
    }

    /* Per-queue pipes always use CHANGEABLE at pipe level. */
    doca_error_t result = build_pipe(port, nb_queues, /*force_changeable=*/true, out_pipe);
    if (result != DOCA_SUCCESS) return result;

    /* Determine meta match index for per-entry match population. */
    int  meta_match_idx      = -1;
    bool meta_match_needs_l3 = false;
    bool is_esp_sn_match     = false;
    bool is_pkt_meta_match   = false;
    if (match_.has_value()) {
        if (std::holds_alternative<MatchIPv4Meta>(*match_)) {
            meta_match_idx      = std::get<MatchIPv4Meta>(*match_).u32_index;
            meta_match_needs_l3 = true;   /* pipe template includes parser_meta */
        } else if (std::holds_alternative<MatchMeta>(*match_)) {
            meta_match_idx      = std::get<MatchMeta>(*match_).u32_index;
            meta_match_needs_l3 = false;  /* meta-only; no parser_meta in template */
        } else if (std::holds_alternative<MatchMetaMasked>(*match_)) {
            meta_match_idx      = std::get<MatchMetaMasked>(*match_).u32_index;
            meta_match_needs_l3 = false;  /* meta-only with partial mask; entry value = queue index */
        } else if (std::holds_alternative<MatchEspSnMasked>(*match_)) {
            is_esp_sn_match = true;       /* entry value = queue index, on tun.esp_sn */
        } else if (std::holds_alternative<MatchPktMetaMasked>(*match_)) {
            is_pkt_meta_match = true;     /* entry value = queue index, on meta.pkt_meta */
        }
    }

    /*
     * Pre-build stable RSS queue-id array: qids[q] = q.
     * DOCA needs a stable pointer to queues_array through entries_process; this vector
     * lives for the duration of build_per_queue so all entry pointers remain valid.
     */
    std::vector<uint16_t> qids(static_cast<size_t>(nb_queues));
    for (int q = 0; q < nb_queues; q++) qids[q] = static_cast<uint16_t>(q);

    g_pipelines_ctx.batch.reset();

    for (int q = 0; q < nb_queues; q++) {
        /* Per-entry match: meta value = q.  Only set parser_meta if the pipe
         * template includes it (MatchIPv4Meta); MatchMeta omits it entirely. */
        struct doca_flow_match entry_match = {};
        if (meta_match_idx >= 0) {
            if (meta_match_needs_l3)
                entry_match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
            entry_match.meta.u32[meta_match_idx] =
                rte_cpu_to_be_32(static_cast<uint32_t>(q));
        } else if (is_esp_sn_match) {
            entry_match.tun.type   = DOCA_FLOW_TUN_ESP;
            entry_match.tun.esp_sn = rte_cpu_to_be_32(static_cast<uint32_t>(q));
        } else if (is_pkt_meta_match) {
            entry_match.meta.pkt_meta = rte_cpu_to_be_32(static_cast<uint32_t>(q));
        }

        /* Per-entry fwd. */
        FwdSpec fwd_spec = fwd_fn(q);
        struct doca_flow_fwd entry_fwd = {};
        memset(&entry_fwd, 0, sizeof(entry_fwd));

        if (std::holds_alternative<FwdDrop>(fwd_spec)) {
            entry_fwd.type = DOCA_FLOW_FWD_DROP;
        } else if (std::holds_alternative<FwdRSSOne>(fwd_spec)) {
            const auto &r  = std::get<FwdRSSOne>(fwd_spec);
            int qid = r.queue_id;
            if (qid < 0 || qid >= nb_queues) {
                RTE_LOG(ERR, DOCA_PIPELINES, "pipe_builder(%s): build_per_queue: queue_id=%d out of range [0,%d)\n",
                           name_.c_str(), qid, nb_queues);
                (void)doca_flow_pipe_destroy(*out_pipe);
                *out_pipe = nullptr;
                return DOCA_ERROR_INVALID_VALUE;
            }
            entry_fwd.type             = DOCA_FLOW_FWD_RSS;
            entry_fwd.rss_type         = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;
            entry_fwd.rss.queues_array = &qids[static_cast<size_t>(qid)];
            entry_fwd.rss.nr_queues    = 1;
            entry_fwd.rss.outer_flags  = r.flags;
        } else {
            RTE_LOG(ERR, DOCA_PIPELINES, "pipe_builder(%s): build_per_queue: unsupported FwdSpec for entry %d \n"
                       "(only FwdRSSOne and FwdDrop supported)",
                       name_.c_str(), q);
            (void)doca_flow_pipe_destroy(*out_pipe);
            *out_pipe = nullptr;
            return DOCA_ERROR_NOT_SUPPORTED;
        }

        struct doca_flow_pipe_entry *hit_ent = nullptr;
        result = doca_flow_pipe_basic_add_entry(
            0, *out_pipe, &entry_match, 0, nullptr, nullptr,
            &entry_fwd,
            DOCA_FLOW_ENTRY_FLAGS_NO_WAIT,
            &g_pipelines_ctx.batch, &hit_ent);
        if (result != DOCA_SUCCESS) {
            RTE_LOG(ERR, DOCA_PIPELINES, "pipe_builder(%s): add_entry q=%d: %s, total_entries_processed=%d\n",
                       name_.c_str(), q, doca_error_get_descr(result), g_pipelines_ctx.batch.total_processed);
            (void)doca_flow_pipe_destroy(*out_pipe);
            *out_pipe = nullptr;
            return result;
        }
        if (out_entries) out_entries[q] = hit_ent;
    }

    result = doca_pipelines_process_entries(port, nb_queues);
    if (result != DOCA_SUCCESS) {
        (void)doca_flow_pipe_destroy(*out_pipe);
        *out_pipe = nullptr;
        return result;
    }

    return DOCA_SUCCESS;
}

// ============================================================
// AsoBlockBuilder
// ============================================================

AsoBlockBuilder::AsoBlockBuilder(std::string name) : name_(std::move(name)) {}

AsoBlockBuilder &AsoBlockBuilder::op(enum doca_flow_external_action_array_operation o) { op_              = o; return *this; }
AsoBlockBuilder &AsoBlockBuilder::meta_index(int idx)                                   { meta_index_      = idx; return *this; }
AsoBlockBuilder &AsoBlockBuilder::array_index(int idx)                                  { array_index_     = idx; return *this; }
AsoBlockBuilder &AsoBlockBuilder::resource_offset(int off)                              { resource_offset_ = off; return *this; }
AsoBlockBuilder &AsoBlockBuilder::fwd(FwdSpec f)                                        { fwd_             = f;   return *this; }
AsoBlockBuilder &AsoBlockBuilder::domain(enum doca_flow_pipe_domain d)                  { domain_          = d;   return *this; }

doca_error_t AsoBlockBuilder::build(struct doca_flow_port *port,
                                     struct doca_flow_external_resource_array *array_resource,
                                     uint32_t array_resource_type,
                                     struct doca_flow_pipe **out_pipe)
{
    if (!fwd_.has_value()) {
        RTE_LOG(ERR, DOCA_PIPELINES, "pipe_builder(aso:%s): requires .fwd(FwdPipe or FwdOL)\n", name_.c_str());
        return DOCA_ERROR_INVALID_VALUE;
    }
    if (meta_index_ < 0 || meta_index_ >= DOCA_FLOW_META_SCRATCH_PAD_MAX) {
        RTE_LOG(ERR, DOCA_PIPELINES, "pipe_builder(aso:%s): invalid meta_index %d\n", name_.c_str(), meta_index_);
        return DOCA_ERROR_INVALID_VALUE;
    }
    /*
     * external action array requires an even register index
     * (hws_register_is_first_reg_c_in_pair: reg_id % 2 == 0).
     */
    if ((meta_index_ & 1) != 0) {
        RTE_LOG(ERR, DOCA_PIPELINES, "pipe_builder(aso:%s): meta_index must be even (got %d); \n"
                   "use 0,2,4,… or COPY into an even slot first",
                   name_.c_str(), meta_index_);
        return DOCA_ERROR_NOT_SUPPORTED;
    }
    if (array_index_ < DYNAMIC) {
        RTE_LOG(ERR, DOCA_PIPELINES, "pipe_builder(aso:%s): invalid array_index %d (use -1 for dynamic)\n",
                   name_.c_str(), array_index_);
        return DOCA_ERROR_INVALID_VALUE;
    }

    /* --- External action --- */
    struct doca_flow_external_action_array array_action = {};
    array_action.input_output.field_string = "meta.data";
    array_action.input_output.bit_offset   = DOCA_PIPELINES_META_U32_BIT_OFFSET(meta_index_);
    array_action.operation                 = op_;
    if (array_index_ == DYNAMIC) {
        array_action.index      = UINT32_MAX;
        array_action.is_dynamic = true;
    } else {
        array_action.index      = static_cast<uint32_t>(array_index_);
        array_action.is_dynamic = false;
    }

    struct doca_flow_external_actions external_action = {};
    external_action.type            = array_resource_type;
    external_action.resource        = array_resource;
    external_action.resource_offset =
        array_action.is_dynamic ? UINT32_MAX : static_cast<uint32_t>(resource_offset_);
    external_action.action          = &array_action;

    /* --- Ordered list --- */
    struct doca_flow_ordered_list_element_adjusted ordered_el_adj = {};
    struct doca_flow_ordered_list                  ordered_list   = {};
    memset(&ordered_el_adj, 0, sizeof(ordered_el_adj));
    memset(&ordered_list,   0, sizeof(ordered_list));

    ordered_el_adj.external.type             = DOCA_FLOW_ORDERED_LIST_ELEMENT_EXTERNAL_ACTIONS;
    ordered_el_adj.external.external_actions = &external_action;
    ordered_list.idx      = 0;
    ordered_list.size     = 1;
    ordered_list.elements = &ordered_el_adj.element;

    struct doca_flow_ordered_list *ordered_lists[1] = {&ordered_list};

    /* --- Pipe fwd (FwdPipe or FwdOL) --- */
    struct doca_flow_fwd fwd = {};
    if (std::holds_alternative<FwdOL>(*fwd_)) {
        const auto &ol             = std::get<FwdOL>(*fwd_);
        fwd.type                   = DOCA_FLOW_FWD_ORDERED_LIST_PIPE;
        fwd.ordered_list_pipe.pipe = ol.pipe;
        fwd.ordered_list_pipe.idx  = static_cast<uint32_t>(ol.slot);
    } else if (std::holds_alternative<FwdPipe>(*fwd_)) {
        fwd.type      = DOCA_FLOW_FWD_PIPE;
        fwd.next_pipe = std::get<FwdPipe>(*fwd_).pipe;
    } else {
        RTE_LOG(ERR, DOCA_PIPELINES, "pipe_builder(aso:%s): fwd must be FwdPipe or FwdOL\n", name_.c_str());
        return DOCA_ERROR_INVALID_VALUE;
    }

    /* --- CFG create / set / pipe_create --- */
    PipeCfgGuard guard;
    doca_error_t result = DOCA_SUCCESS;

#define LB_PB_ASO_CKV(step)                                                    \
    if (result != DOCA_SUCCESS) {                                              \
        RTE_LOG(ERR, DOCA_PIPELINES, "pipe_builder(aso:%s): \n" step ": %s",                      \
                   name_.c_str(), doca_error_get_descr(result));               \
        return result;                                                         \
    }

    result = doca_flow_pipe_cfg_create(&guard.cfg, port);
    LB_PB_ASO_CKV("cfg_create");
    result = doca_flow_pipe_cfg_set_name(guard.cfg, name_.c_str());
    LB_PB_ASO_CKV("set_name");
    result = doca_flow_pipe_cfg_set_type(guard.cfg, DOCA_FLOW_PIPE_ORDERED_LIST);
    LB_PB_ASO_CKV("set_type");
    result = doca_flow_pipe_cfg_set_domain(guard.cfg, domain_);
    LB_PB_ASO_CKV("set_domain");
    result = doca_flow_pipe_cfg_set_is_root(guard.cfg, false);
    LB_PB_ASO_CKV("set_is_root");
    result = doca_flow_pipe_cfg_set_nr_entries(guard.cfg, 1);
    LB_PB_ASO_CKV("set_nr_entries");
    result = doca_flow_pipe_cfg_set_ordered_lists(guard.cfg, ordered_lists, 1);
    LB_PB_ASO_CKV("set_ordered_lists");
    result = doca_flow_pipe_create(guard.cfg, &fwd, nullptr, out_pipe);
    LB_PB_ASO_CKV("pipe_create");

#undef LB_PB_ASO_CKV

    g_pipelines_ctx.batch.reset();

    struct doca_flow_pipe_entry *ordered_entry = nullptr;
    result = doca_flow_pipe_ordered_list_add_entry(
        0, *out_pipe, 0,
        &ordered_list, nullptr,
        DOCA_FLOW_ENTRY_FLAGS_NO_WAIT,
        &g_pipelines_ctx.batch, &ordered_entry);
    if (result != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "pipe_builder(aso:%s): ordered_entry_add: %s\n",
                   name_.c_str(), doca_error_get_descr(result));
        (void)doca_flow_pipe_destroy(*out_pipe);
        *out_pipe = nullptr;
        return result;
    }

    result = doca_pipelines_process_entries(port, 1);
    if (result != DOCA_SUCCESS) {
        (void)doca_flow_pipe_destroy(*out_pipe);
        *out_pipe = nullptr;
        return result;
    }

    return DOCA_SUCCESS;
}


} // namespace pb