#include "doca_pipelines.hpp"
#include "pipe_builder.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <rte_byteorder.h>

extern doca_pipelines_ctx_t g_pipelines_ctx;

/**
 * esp_rr pipeline -- round-robin steering keyed on the IPsec ESP Sequence
 * Number instead of a NIC-computed ASO counter, with the SN copied into
 * meta.pkt_meta so it reaches the mbuf on the DPDK side (see the mlx5 PMD's
 * generic flow-metadata dynfield mechanism, gated on
 * rte_flow_dynf_metadata_register() -- drivers/net/mlx5/mlx5_trigger.c),
 * and the outer Ether+IP+ESP framing stripped before the packet reaches the
 * application.
 *
 * Pipeline graph (leaf-to-root):
 *
 *   [LB_ESP_RR_STEER]
 *     match:  meta.pkt_meta & (queues-1)   (MatchPktMetaMasked)
 *     N pre-added entries (index 0..queues-1), each RSS-forwarding to one queue.
 *
 *   [ROOT: LB_ESP_RR_ROOT]
 *     match:  IPv4 + ESP (parser_meta.outer_l4_type == ESP)
 *     action: COPY tun.esp_sn -> meta.pkt_meta   (UNVERIFIED field_string, see
 *             ActionCopyEspSnToMeta's doc comment in pipe_builder.hpp)
 *     decap:  strip outer Ether+IP+ESP framing    (UNVERIFIED config, see
 *             ActionDecap's doc comment in pipe_builder.hpp)
 *     fwd:    -> LB_ESP_RR_STEER
 *     miss:   DROP
 *
 * STEER matches meta.pkt_meta, NOT tun.esp_sn directly, because ROOT already
 * decaps the ESP header before STEER would see it -- by the time STEER's
 * match would run, the tunnel field no longer exists in the packet. The
 * value it needs is the copy ROOT already placed in pkt_meta.
 *
 * No ASO, no ordered-list pipe, no shared mutable state anywhere in this
 * graph -- architecturally simpler than meta_rr, not just a fix bolted on.
 *
 * Selected at runtime via DOCA_PIPELINES_MODE=esp_rr (see doca_pipelines.cpp).
 */
doca_error_t doca_pipelines_run_esp_rr_pipeline(uint16_t queues)
{
    if (queues == 0 || (queues & (queues - 1u)) != 0) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA esp_rr: nb_queues=%u must be a non-zero power of 2\n", queues);
        return DOCA_ERROR_INVALID_VALUE;
    }

    doca_error_t result = DOCA_SUCCESS;

    /*
     * Decap eth header: MAC addresses left as a placeholder (all-zero) --
     * this codebase has no known-correct values for them yet. The ethertype
     * is set to IPv4 since the inner content after ESP is expected to be an
     * IPv4 packet, but the MACs need real values (e.g. the original outer
     * frame's own addresses, if DOCA supports referencing them, or whatever
     * the downstream forwarding logic expects) before this is trustworthy.
     * Flagged loudly rather than silently shipping non-functional zeros.
     */
    RTE_LOG(WARNING, DOCA_PIPELINES,
            "DOCA esp_rr: decap eth header uses placeholder (zero) MAC addresses -- "
            "verify/replace before trusting decapped packet framing\n");
    struct doca_flow_header_eth decap_eth = {};
    decap_eth.type = rte_cpu_to_be_16(0x0800); /* IPv4 */

    /* STEER: N entries, masked match on meta.pkt_meta, each RSS-to-one-queue.
     * Built first (leaf) so PipelineGraph destroys root first. */
    std::vector<struct doca_flow_pipe_entry *> steer_entries(static_cast<size_t>(queues), nullptr);
    struct doca_flow_pipe *steer_pipe = nullptr;
    result = pb::BasicPipeBuilder("LB_ESP_RR_STEER")
        .match(pb::MatchPktMetaMasked{static_cast<uint32_t>(queues - 1)})
        .miss(pb::FwdDrop{})
        .build_per_queue(
            g_pipelines_ctx.port, static_cast<int>(queues),
            [](int q) -> pb::FwdSpec { return pb::FwdRSSOne{q}; },
            &steer_pipe, steer_entries.data());
    if (result != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA esp_rr: steer pipe failed: %s\n", doca_error_get_descr(result));
        return result;
    }
    g_pipelines_ctx.graph.add_pipe(steer_pipe, "esp_rr_steer");
    g_pipelines_ctx.nb_steer_queues = queues;
    for (int q = 0; q < static_cast<int>(queues); q++) {
        g_pipelines_ctx.graph.add_counter_entry(
            "esp_rr_steer_q" + std::to_string(q),
            steer_entries[static_cast<size_t>(q)]);
    }

    /* ROOT: match IPv4 + ESP, copy the real ESP SN into pkt_meta, decap the
     * outer framing, forward into STEER. */
    struct doca_flow_pipe       *classifier_pipe = nullptr;
    struct doca_flow_pipe_entry *hit_entry       = nullptr;
    result = pb::BasicPipeBuilder("LB_ESP_RR_ROOT")
        .root()
        .match(pb::MatchIPv4ESP{})
        .action(pb::ActionCopyEspSnToMeta{})
        /* DIAGNOSTIC: decap removed -- isolate real COPY vs. decap, per cross-instance
         * finding that decap independently breaks RX delivery entirely (see the other
         * doca_pipelines_sdk instance's RACE_INVESTIGATION.md). field_string
         * "tunnel.esp.sn" is confirmed correct there with real traffic. */
        .fwd(pb::FwdPipe{steer_pipe})
        .miss(pb::FwdDrop{})
        .build_single(g_pipelines_ctx.port, &classifier_pipe, &hit_entry);
    if (result != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA esp_rr: root pipe failed: %s\n", doca_error_get_descr(result));
        return result;
    }
    g_pipelines_ctx.graph.add_pipe(classifier_pipe, "esp_rr_root");
    g_pipelines_ctx.graph.add_counter_entry("root_hit", hit_entry);

    return DOCA_SUCCESS;
}
