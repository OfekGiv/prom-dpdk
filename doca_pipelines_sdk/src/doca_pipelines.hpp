#ifndef DOCA_ASO_HPP
#define DOCA_ASO_HPP

#include <rte_log.h>
#include <rte_dev.h>

#define RTE_LOGTYPE_DOCA_PIPELINES RTE_LOGTYPE_USER1
#include "doca_pipelines/pipeline_graph.hpp"

extern "C" {
#include <doca_dev.h>
#include <doca_dpdk.h>
#include <doca_error.h>
#include <doca_flow.h>
#include <doca_flow_external_action_array.h>
#include <doca_flow_external_actions.h>
}

#include <cstddef>
#include <cstdint>

/* Bit offset for meta.u32[idx] — matches flow_memory_aso_steering META_U32_BIT_OFFSET (pkt_meta precedes u32[]). */
static inline uint32_t doca_pipelines_meta_u32_bit_offset(int idx)
{
    return static_cast<uint32_t>(
        (offsetof(struct doca_flow_meta, u32[0]) + static_cast<std::size_t>(idx) * sizeof(uint32_t)) << 3u);
}
#define DOCA_PIPELINES_META_U32_BIT_OFFSET(idx) doca_pipelines_meta_u32_bit_offset(idx)


/**
 * Global pipeline session.
 *
 * Holds the DOCA Flow port handle, ASO resources, and the active pipeline graph.
 * All pipeline builders access this via the extern defined in doca_pipelines.cpp.
 *
 * Separation of concerns:
 *  - Port / ASO / device state:  doca_pipelines_ctx_t fields (lifetime = doca_flow_init
 *    to doca_flow_destroy).
 *  - Graph state (pipes + entries): graph field — replaced on each pipeline rebuild.
 *  - Entry-processing bookkeeping: batch field — reset before each add_entry batch.
 *
 * nb_steer_queues:
 *  Set by the pipeline builder when it creates per-queue RSS steer entries.  Used by
 *  core.cpp to iterate "rss_steer_q<n>" counter entries without knowing the count.
 */
struct doca_pipelines_ctx_t {
    bool    initialized   = false;
    uint8_t pipeline_mode = 0;

    struct doca_flow_port *port = nullptr;

    struct doca_flow_external_resource_array *array_resource     = nullptr;
    uint32_t                                  array_resource_type = 0;
    /**
     * First u32 index of the port's dynamic Memory ASO meta pair (line index), from
     * doca_flow_external_action_get_dynamic_meta_indices; second lane is at i+1. -1 if unset.
     */
    int dynamic_aso_line_meta_u32   = -1;
    int dynamic_aso_offset_meta_u32 = -1;

    /**
     * Number of per-queue RSS steer entries registered in graph as "rss_steer_q<n>".
     * Zero if the active pipeline has no per-queue steer entries.
     */
    uint16_t nb_steer_queues = 0;

    /**
     * Transient bookkeeping shared between add_entry calls and the entry callback.
     * Passed as user_ctx to every doca_flow_pipe_*_add_entry call.
     * Reset via batch.reset() before each batch of add_entry calls.
     */
    EntryBatch batch;

    /**
     * Active pipeline graph: all pipes and named counter entries for the
     * current pipeline_type.  Destroyed and rebuilt whenever the pipeline is
     * torn down or rebuilt.
     */
    PipelineGraph graph;
};

/** After rte_eal_init(), before rte_eth_dev_configure.
 * @param pci_bdf - the PCI BDF of the first doca port
 */
int doca_pipelines_dpdk_probe(const char *pci_bdf);
int doca_pipelines_init(uint16_t nb_queues);
void doca_pipelines_cleanup(void);

/**
 * Process the entries submitted since the last batch.reset().
 * @param port         - the DOCA Flow port
 * @param total_entries - expected number of entry completions
 */
doca_error_t doca_pipelines_process_entries(struct doca_flow_port *port, int total_entries);
/** Query port for dynamic Memory ASO meta.u32 pair; stores result in g_pipelines_ctx. */
doca_error_t doca_pipelines_probe_dynamic_aso_meta_indices(void);

/**
 * True meta round-robin pipeline (pipeline_type "meta_rr").
 *
 * Uses an ASO ADD counter whose value is matched directly against N per-queue
 * entries in the steer table (via a power-of-2 bit mask), giving a strict
 * sequential 0..N-1 round-robin without any RSS hash-based indirection.
 *
 * queues must be a non-zero power of 2.
 */
doca_error_t doca_pipelines_run_meta_rr_pipeline(uint16_t queues);

#endif /* DOCA_ASO_HPP */
