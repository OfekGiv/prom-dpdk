#include "pipeline_graph.hpp"

#include <cinttypes>
#include <algorithm>
#include <utility>

#include "doca_pipelines.hpp"

// ============================================================
// PipelineGraph — move semantics
// ============================================================

PipelineGraph::PipelineGraph(PipelineGraph &&other) noexcept
    : pipes_(std::move(other.pipes_))
    , named_pipes_(std::move(other.named_pipes_))
    , entries_(std::move(other.entries_))
{}

PipelineGraph &PipelineGraph::operator=(PipelineGraph &&other) noexcept
{
    if (this != &other) {
        destroy();
        pipes_       = std::move(other.pipes_);
        named_pipes_ = std::move(other.named_pipes_);
        entries_     = std::move(other.entries_);
    }
    return *this;
}

// ============================================================
// Registration
// ============================================================

void PipelineGraph::add_pipe(struct doca_flow_pipe *pipe, std::string name)
{
    if (pipe == nullptr) {
        RTE_LOG(WARNING, DOCA_PIPELINES, "PipelineGraph::add_pipe: ignoring null pipe (name=\"%s\")\n",
                       name.c_str());
        return;
    }
    if (!name.empty()) {
        named_pipes_[name] = pipe;
    }
    pipes_.push_back({std::move(name), pipe});
}

void PipelineGraph::add_counter_entry(std::string label, struct doca_flow_pipe_entry *entry)
{
    if (entry == nullptr) {
        RTE_LOG(WARNING, DOCA_PIPELINES, "PipelineGraph::add_counter_entry: ignoring null entry (label=\"%s\")\n",
                       label.c_str());
        return;
    }
    /* Overwrite existing entry with the same label to keep labels unique. */
    for (auto &e : entries_) {
        if (e.label == label) {
            e.entry = entry;
            return;
        }
    }
    entries_.push_back({std::move(label), entry});
}

// ============================================================
// Lookup
// ============================================================

struct doca_flow_pipe *PipelineGraph::find_pipe(const std::string &name) const
{
    auto it = named_pipes_.find(name);
    return (it != named_pipes_.end()) ? it->second : nullptr;
}

struct doca_flow_pipe_entry *PipelineGraph::find_entry(const std::string &label) const
{
    for (const auto &e : entries_) {
        if (e.label == label) {
            return e.entry;
        }
    }
    return nullptr;
}

// ============================================================
// Counter queries
// ============================================================

doca_error_t PipelineGraph::query_counter(const std::string &label, uint64_t *out_pkts) const
{
    if (out_pkts == nullptr) {
        return DOCA_ERROR_INVALID_VALUE;
    }
    struct doca_flow_pipe_entry *entry = find_entry(label);
    if (entry == nullptr) {
        return DOCA_ERROR_NOT_FOUND;
    }
    struct doca_flow_resource_query stats = {};
    doca_error_t r = doca_flow_resource_query_entry(entry, &stats);
    if (r == DOCA_SUCCESS) {
        *out_pkts = stats.counter.total_pkts;
    }
    return r;
}

void PipelineGraph::log_counters(const char *prefix) const
{
    for (const auto &e : entries_) {
        struct doca_flow_resource_query stats = {};
        doca_error_t r = doca_flow_resource_query_entry(e.entry, &stats);
        if (r == DOCA_SUCCESS) {
            RTE_LOG(INFO, DOCA_PIPELINES, "%s [%s]: pkts=%" PRIu64 " bytes=%" PRIu64 "\n",
                        prefix, e.label.c_str(),
                        stats.counter.total_pkts,
                        stats.counter.total_bytes);
        } else {
            RTE_LOG(WARNING, DOCA_PIPELINES, "%s [%s]: query failed: %s\n",
                           prefix, e.label.c_str(), doca_error_get_descr(r));
        }
    }
}

// ============================================================
// Destruction
// ============================================================

void PipelineGraph::destroy()
{
    /*
     * Destroy pipes in reverse-registration order.
     * Builders register leaf pipes first and the root pipe last, so reversing
     * gives root-first destruction — the order DOCA Flow expects before
     * doca_flow_port_stop().
     */
    for (auto it = pipes_.rbegin(); it != pipes_.rend(); ++it) {
        if (it->pipe != nullptr) {
            (void)doca_flow_pipe_destroy(it->pipe);
            it->pipe = nullptr;
        }
    }
    pipes_.clear();
    named_pipes_.clear();
    /*
     * Entry pointers are owned by their pipes; they are already invalid after
     * the pipes are destroyed above.  Just clear the bookkeeping vector.
     */
    entries_.clear();
}