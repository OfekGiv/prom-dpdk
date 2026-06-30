#include "doca_pipelines.hpp"
#include "pipe_builder.hpp"

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <rte_byteorder.h>
#include <rte_ethdev.h>

extern doca_pipelines_ctx_t g_pipelines_ctx;

/**
 * True round-robin pipeline using metadata matching.
 *
 * this pipeline dispatches each packet to
 * a specific queue by matching the ASO counter value directly against N
 * dedicated entries in the steer table, one per queue.
 *
 * Pipeline graph (leaf-to-root):
 *
 *   [ROOT: LB_META_RR_ROOT]
 *     match: IPv4/UDP (parser_meta)
 *     action: meta.u32[2] = 1  (initial counter operand for ASO ADD)
 *     fwd:    → FwdOL → LB_META_RR_ASO_ADD
 *     miss:   DROP
 *
 *   [LB_META_RR_ASO_ADD]
 *     op:     ADD  ASO[line=0] += meta.u32[2]  → meta.u32[2] = new counter
 *     fwd:    → LB_META_RR_STEER
 *
 *   [LB_META_RR_STEER]
 *     match:  meta.u32[2] & (queues-1)  (MatchMetaMasked, power-of-2 cycle)
 *     N entries: entry i  → RSS to queue i
 *     miss:   DROP
 *
 * The counter in ASO[0] increments freely; only the lower log2(N) bits are
 * compared in the steer table, giving a deterministic 0..N-1 round-robin.
 *
 * Requirement: queues must be a power of 2.
 */
doca_error_t doca_pipelines_run_meta_rr_pipeline(uint16_t queues)
{
    if (queues == 0 || (queues & (queues - 1u)) != 0) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA meta_rr: nb_queues=%u must be a non-zero power of 2\n", queues);
        return DOCA_ERROR_INVALID_VALUE;
    }

    const int      increment_index = 2;
    const int      increment_value = 1;
    const uint32_t queue_mask      = static_cast<uint32_t>(queues - 1u);

    doca_error_t result = DOCA_SUCCESS;

    /* Build in leaf-to-root order so PipelineGraph destroys root first. */

    /*
     * Terminal steer pipe: match meta.u32[increment_index] masked to the lower
     * log2(queues) bits.  Entry i forwards to RSS queue i.
     */
    std::vector<struct doca_flow_pipe_entry *> steer_entries(static_cast<size_t>(queues), nullptr);
    struct doca_flow_pipe *steer_pipe = nullptr;
    result = pb::BasicPipeBuilder("LB_META_RR_STEER")
        .match(pb::MatchMetaMasked{increment_index, queue_mask})
        .miss(pb::FwdDrop{})
        .build_per_queue(g_pipelines_ctx.port, static_cast<int>(queues),
                         [](int q) -> pb::FwdSpec {
                             return pb::FwdRSSOne{q, DOCA_FLOW_RSS_AUTO};
                         },
                         &steer_pipe, steer_entries.data());
    if (result != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA meta_rr: steer pipe failed: %s\n", doca_error_get_descr(result));
        return result;
    }
    g_pipelines_ctx.graph.add_pipe(steer_pipe, "meta_rr_steer");
    g_pipelines_ctx.nb_steer_queues = queues;
    for (int q = 0; q < static_cast<int>(queues); q++) {
        g_pipelines_ctx.graph.add_counter_entry(
            "meta_rr_steer_q" + std::to_string(q),
            steer_entries[static_cast<size_t>(q)]);
    }

    /*
     * ASO ADD pipe: atomically reads ASO[line=0], adds meta.u32[increment_index]
     * (= 1, set by root), writes result back to both ASO[0] and meta.u32[increment_index].
     * Forwards to steer pipe.
     */
    struct doca_flow_pipe *aso_add_pipe = nullptr;
    result = pb::AsoBlockBuilder("LB_META_RR_ASO_ADD")
        .op(DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_ADD)
        .meta_index(increment_index)
        .array_index(0)
        .resource_offset(0)
        .fwd(pb::FwdPipe{steer_pipe})
        .build(g_pipelines_ctx.port, g_pipelines_ctx.array_resource,
               g_pipelines_ctx.array_resource_type, &aso_add_pipe);
    if (result != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA meta_rr: ASO ADD pipe failed: %s\n", doca_error_get_descr(result));
        return result;
    }
    g_pipelines_ctx.graph.add_pipe(aso_add_pipe, "meta_rr_aso_add");

    /*
     * Root classifier pipe: match IPv4/UDP, initialise meta.u32[increment_index]
     * to increment_value (the ADD operand), then enter the ASO ordered list.
     */
    struct doca_flow_pipe       *classifier_pipe = nullptr;
    struct doca_flow_pipe_entry *hit_entry       = nullptr;
    result = pb::BasicPipeBuilder("LB_META_RR_ROOT")
        .root()
        .match(pb::MatchIPv4{})
        .fwd(pb::FwdOL{aso_add_pipe, 0})
        .miss(pb::FwdDrop{})
        .action(pb::ActionSetMeta{increment_index, static_cast<uint32_t>(increment_value)})
        .build_single(g_pipelines_ctx.port, &classifier_pipe, &hit_entry);
    if (result != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA meta_rr: root classifier pipe failed: %s\n", doca_error_get_descr(result));
        return result;
    }
    g_pipelines_ctx.graph.add_pipe(classifier_pipe, "meta_rr_root");
    g_pipelines_ctx.graph.add_counter_entry("root_hit", hit_entry);

    /* Initialise ASO counter line 0 to zero. */
    const uint64_t rr_zero = 0ULL;
    result = doca_flow_external_resource_array_update(
        g_pipelines_ctx.port, g_pipelines_ctx.array_resource, 0, 0, rr_zero);
    if (result != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA meta_rr: ASO array init failed: %s\n", doca_error_get_descr(result));
        return result;
    }

    /* Non-fatal sanity: verify the root entry counter is queryable. */
    {
        struct doca_flow_resource_query query = {};
        doca_error_t r = doca_flow_resource_query_entry(hit_entry, &query);
        if (r != DOCA_SUCCESS) {
            RTE_LOG(WARNING, DOCA_PIPELINES, "DOCA meta_rr: root entry query not available: %s\n",
                              doca_error_get_descr(r));
        }
    }

    return DOCA_SUCCESS;
}
