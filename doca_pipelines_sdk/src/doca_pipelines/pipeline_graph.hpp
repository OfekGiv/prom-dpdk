#ifndef DOCA_PIPELINE_GRAPH_HPP
#define DOCA_PIPELINE_GRAPH_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include <doca_error.h>
#include <doca_flow.h>
}

// ============================================================
// EntryBatch — transient bookkeeping for a single add_entry batch
// ============================================================

/**
 * Tracks completion state for one batch of doca_flow_pipe_*_add_entry calls.
 *
 * Pass &batch as the user_ctx argument to every add_entry call in the batch.
 * The global entry callback increments nb_processed and sets failure on error.
 * After doca_flow_entries_process() returns, check nb_processed == expected
 * and !failure.
 *
 * reset() before each new batch; total_processed accumulates across resets for
 * diagnostics.
 */
struct EntryBatch {
    int  nb_processed    = 0;
    int  total_processed = 0;
    bool failure         = false;

    void reset()
    {
        nb_processed = 0;
        failure      = false;
    }
};

// ============================================================
// PipelineGraph — container for a single pipeline's pipes and entries
// ============================================================

/**
 * Owns the set of DOCA Flow pipes built for one pipeline configuration and
 * tracks named counter entries for stats queries.
 *
 * Construction order:
 *   Builders add pipes via add_pipe() in the order they are created (typically
 *   leaf-to-root: RSS terminal first, root classifier last).  destroy() tears
 *   them down in reverse order (root first, leaves last), which is the order
 *   DOCA Flow requires before doca_flow_port_stop().
 *
 * Entry counters:
 *   Any entry created with a non-shared counter monitor can be registered via
 *   add_counter_entry() with a stable label string.  query_counter() and
 *   log_counters() provide named access to those hardware counters.
 *
 * Lifetime:
 *   Must not outlive the doca_flow_port the pipes were built against.
 *   destroy() is idempotent; the destructor calls it automatically.
 */
class PipelineGraph {
public:
    PipelineGraph()  = default;
    ~PipelineGraph() { destroy(); }

    PipelineGraph(const PipelineGraph &) = delete;
    PipelineGraph &operator=(const PipelineGraph &) = delete;

    PipelineGraph(PipelineGraph &&other) noexcept;
    PipelineGraph &operator=(PipelineGraph &&other) noexcept;

    /**
     * Register a pipe for ordered destruction.
     *
     * @param pipe  Non-null pipe handle returned by doca_flow_pipe_create.
     * @param name  Optional lookup name; empty string means anonymous.
     *              Duplicate names are silently overwritten in the lookup map.
     */
    void add_pipe(struct doca_flow_pipe *pipe, std::string name = {});

    /**
     * Register a named counter entry for stats queries.
     *
     * The entry must have been created with a non-shared counter monitor
     * (DOCA_FLOW_RESOURCE_TYPE_NON_SHARED on the monitor) so that
     * doca_flow_resource_query_entry() returns valid data.
     *
     * Entry memory is owned by DOCA (tied to its pipe); the graph only
     * holds the pointer for querying.  Duplicate labels are silently
     * overwritten.
     *
     * Common label conventions:
     *   "root_hit"            — classifier root entry
     *   "jsq_threshold_drop"  — JSQ overload drop entry
     *   "rss_steer_q<n>"      — per-queue RSS steer entry
     */
    void add_counter_entry(std::string label, struct doca_flow_pipe_entry *entry);

    /** Look up a registered pipe by name; returns nullptr if not found. */
    struct doca_flow_pipe *find_pipe(const std::string &name) const;

    /** Look up a counter entry by label; returns nullptr if not found. */
    struct doca_flow_pipe_entry *find_entry(const std::string &label) const;

    /**
     * Query a named entry's packet counter.
     *
     * @param label    Label string passed to add_counter_entry().
     * @param out_pkts Receives total_pkts from the hardware counter.
     * @return DOCA_SUCCESS on success.
     *         DOCA_ERROR_NOT_FOUND if the label is not registered.
     *         Any DOCA query error otherwise.
     */
    doca_error_t query_counter(const std::string &label, uint64_t *out_pkts) const;

    /**
     * Log all registered counter entries at INFO level.
     *
     * @param prefix  Printed before the counter name in each log line.
     */
    void log_counters(const char *prefix = "pipeline") const;

    /**
     * Destroy all registered pipes in reverse-registration order, then clear
     * the graph.  Safe to call multiple times (second call is a no-op).
     */
    void destroy();

    bool empty() const { return pipes_.empty(); }

    /** Number of counter entries registered (for iteration or sanity checks). */
    size_t nb_counter_entries() const { return entries_.size(); }

private:
    struct OwnedPipe {
        std::string            name;
        struct doca_flow_pipe *pipe = nullptr;
    };

    struct CounterEntry {
        std::string                  label;
        struct doca_flow_pipe_entry *entry = nullptr;
    };

    /* Ordered by insertion — destroyed in reverse. */
    std::vector<OwnedPipe> pipes_;

    /* Name → pipe for O(1) lookup; does not affect destruction order. */
    std::unordered_map<std::string, struct doca_flow_pipe *> named_pipes_;

    /* Named entries for counter queries; order matches registration order. */
    std::vector<CounterEntry> entries_;
};

#endif // DOCA_PIPELINE_GRAPH_HPP