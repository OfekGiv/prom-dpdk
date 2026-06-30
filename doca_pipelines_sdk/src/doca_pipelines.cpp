/*
 * DOCA meta_rr pipeline — SDK copy (meta round-robin only).
 */
#include "doca_pipelines.hpp"
#include "doca_pipelines/pipe_builder.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <rte_byteorder.h>
#include <rte_ethdev.h>
#include <rte_pci.h>

extern "C" {
#include <doca_verbs.h>
}

doca_pipelines_ctx_t g_pipelines_ctx = {};
static struct doca_dev *g_doca_probe_dev = nullptr;
static bool g_doca_dpdk_port_probed = false;
static bool g_doca_probe_dev_owned = false;

static std::string pci_base_for_compare(const char *allow_arg)
{
    if (allow_arg == nullptr || allow_arg[0] == '\0') {
        return {};
    }
    std::string s(allow_arg);
    const size_t comma = s.find(',');
    if (comma != std::string::npos) {
        s.resize(comma);
    }
    if (s.size() >= 4 && s.compare(0, 4, "pci:") == 0) {
        s.erase(0, 4);
    }
    return s;
}

static bool pci_same_device(const char *a, const char *b)
{
    struct rte_pci_addr pa = {};
    struct rte_pci_addr pb = {};
    const std::string sa = pci_base_for_compare(a);
    const std::string sb = pci_base_for_compare(b);
    if (sa.empty() || sb.empty()) {
        return false;
    }
    if (rte_pci_addr_parse(sa.c_str(), &pa) != 0) {
        return false;
    }
    if (rte_pci_addr_parse(sb.c_str(), &pb) != 0) {
        return false;
    }
    return rte_pci_addr_cmp(&pa, &pb) == 0;
}

static bool ethdev_exists_for_pci_bdf(const char *pci_use)
{
    const uint16_t n = rte_eth_dev_count_avail();
    for (uint16_t pid = 0; pid < n; pid++) {
        char name[RTE_ETH_NAME_MAX_LEN];
        if (rte_eth_dev_get_name_by_port(pid, name) != 0) {
            continue;
        }
        if (pci_same_device(name, pci_use)) {
            return true;
        }
    }
    return false;
}


static void doca_pipelines_entry_cb(struct doca_flow_pipe_entry *entry,
                                  uint16_t pipe_queue,
                                  enum doca_flow_entry_status status,
                                  enum doca_flow_entry_op op,
                                  void *user_ctx)
{
    (void)entry;
    (void)pipe_queue;
    (void)op;
    EntryBatch *batch = static_cast<EntryBatch *>(user_ctx);
    if (batch == nullptr) {
        return;
    }
    if (status != DOCA_FLOW_ENTRY_STATUS_SUCCESS) {
        batch->failure = true;
    }
    batch->nb_processed++;
    batch->total_processed++;
}

doca_error_t doca_pipelines_process_entries(struct doca_flow_port *port, int total_entries)
{
    EntryBatch &batch = g_pipelines_ctx.batch;
    doca_error_t result = DOCA_SUCCESS;
    if (batch.nb_processed < total_entries) {
        result = doca_flow_entries_process(port, 0, 10000, total_entries);
        if (result != DOCA_SUCCESS) {
            RTE_LOG(ERR, DOCA_PIPELINES, "DOCA: entries_process failed: %s\n", doca_error_get_descr(result));
            return result;
        }
        if (batch.failure) {
            RTE_LOG(ERR, DOCA_PIPELINES, "DOCA: entry callback reported failure\n");
            return DOCA_ERROR_BAD_STATE;
        }
    }
    if (batch.nb_processed != total_entries || batch.failure) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA: expected %d completions, got %d (failure=%d)\n",
                      total_entries, batch.nb_processed, static_cast<int>(batch.failure));
        return DOCA_ERROR_BAD_STATE;
    }
    return result;
}

static doca_error_t open_doca_dev_by_pci(const char *pci_addr, struct doca_dev **out_dev)
{
    struct doca_devinfo **dev_list = nullptr;
    uint32_t nb_devs = 0;
    doca_error_t res = doca_devinfo_create_list(&dev_list, &nb_devs);
    if (res != DOCA_SUCCESS) {
        return res;
    }
    for (uint32_t i = 0; i < nb_devs; i++) {
        uint8_t is_equal = 0;
        res = doca_devinfo_is_equal_pci_addr(dev_list[i], pci_addr, &is_equal);
        if (res == DOCA_SUCCESS && is_equal != 0) {
            res = doca_dev_open(dev_list[i], out_dev);
            doca_devinfo_destroy_list(dev_list);
            return res;
        }
    }
    doca_devinfo_destroy_list(dev_list);
    return DOCA_ERROR_NOT_FOUND;
}

int doca_pipelines_dpdk_probe(const char *pci_bdf)
{
    if (g_doca_dpdk_port_probed) {
        return 0;
    }

    if (pci_bdf == nullptr || pci_bdf[0] == '\0') {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA probe: null or empty pci_bdf\n");
        return -1;
    }

    RTE_LOG(INFO, DOCA_PIPELINES, "DOCA: probing DPDK bridge for PCI %s\n", pci_bdf);

    if (ethdev_exists_for_pci_bdf(pci_bdf)) {
        RTE_LOG(ERR, DOCA_PIPELINES, 
            "DOCA: DPDK already has rte_eth for %s — use EAL -a pci:00:00.0 -a auxiliary: \n"
            "and do not whitelist the dataplane PF",
            pci_bdf);
        return -1;
    }

    struct doca_dev *dev = nullptr;
    doca_error_t res = open_doca_dev_by_pci(pci_bdf, &dev);
    if (res != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA probe: doca_dev_open failed for %s: %s\n", pci_bdf, doca_error_get_descr(res));
        return -1;
    }

    g_doca_probe_dev = dev;
    g_doca_probe_dev_owned = true;

    res = doca_dpdk_port_probe(g_doca_probe_dev, "dv_flow_en=2");
    if (res != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA probe: doca_dpdk_port_probe failed: %s\n", doca_error_get_descr(res));
        (void)doca_dev_close(g_doca_probe_dev);
        g_doca_probe_dev = nullptr;
        g_doca_probe_dev_owned = false;
        return -1;
    }

    g_doca_dpdk_port_probed = true;
    RTE_LOG(INFO, DOCA_PIPELINES, "DOCA DPDK bridge probe ok (dv_flow_en=2)\n");
    return 0;
}

int doca_pipelines_init(uint16_t nb_queues)
{
    g_pipelines_ctx.dynamic_aso_line_meta_u32 = -1;
    g_pipelines_ctx.dynamic_aso_offset_meta_u32 = -1;

    if (g_pipelines_ctx.initialized) {
        return 0;
    }

    doca_error_t result;
    struct doca_dev *dev = nullptr;
    struct doca_flow_cfg *flow_cfg = nullptr;
    struct doca_flow_port_cfg *port_cfg = nullptr;
    const uint32_t actions_mem_size = 1U << 17;
    const uint16_t nb_q = nb_queues;
    const uint32_t nr_counters = static_cast<uint32_t>(nb_q) + 9u;

    RTE_LOG(INFO, DOCA_PIPELINES, "DOCA: init nb_queues=%u nr_counters=%u\n", nb_q, nr_counters);

    result = doca_dpdk_port_as_dev(0, &dev);
    if (result != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA: doca_dpdk_port_as_dev failed: %s\n", doca_error_get_descr(result));
        return -1;
    }

    struct doca_flow_external_resource_array_cfg array_cfg = {};
    result = doca_flow_external_action_array_register(&g_pipelines_ctx.array_resource_type);
    if (result != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA ASO: array register failed: %s\n", doca_error_get_descr(result));
        return -1;
    }

    result = doca_flow_cfg_create(&flow_cfg);
    if (result != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA: flow cfg create failed: %s\n", doca_error_get_descr(result));
        return -1;
    }

    result = doca_flow_cfg_set_pipe_queues(flow_cfg, 1u);
    if (result != DOCA_SUCCESS) {
        doca_pipelines_cleanup();
        return -1;
    }
    result = doca_flow_cfg_set_mode_args(flow_cfg, "vnf,hws");
    if (result != DOCA_SUCCESS) {
        doca_pipelines_cleanup();
        return -1;
    }
    result = doca_flow_cfg_set_cb_entry_process(flow_cfg, doca_pipelines_entry_cb);
    if (result != DOCA_SUCCESS) {
        doca_pipelines_cleanup();
        return -1;
    }
    result = doca_flow_cfg_set_nr_counters(flow_cfg, nr_counters);
    if (result != DOCA_SUCCESS) {
        doca_pipelines_cleanup();
        return -1;
    }

    result = doca_flow_init(flow_cfg);
    if (result != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA: flow init failed: %s\n", doca_error_get_descr(result));
        doca_pipelines_cleanup();
        return -1;
    }

    result = doca_flow_port_cfg_create(&port_cfg);
    if (result != DOCA_SUCCESS) {
        doca_pipelines_cleanup();
        return -1;
    }
    result = doca_flow_port_cfg_set_port_id(port_cfg, 0);
    if (result != DOCA_SUCCESS) {
        doca_pipelines_cleanup();
        return -1;
    }
    result = doca_flow_port_cfg_set_dev(port_cfg, dev);
    if (result != DOCA_SUCCESS) {
        doca_pipelines_cleanup();
        return -1;
    }
    result = doca_flow_port_cfg_set_actions_mem_size(port_cfg, actions_mem_size);
    if (result != DOCA_SUCCESS) {
        doca_pipelines_cleanup();
        return -1;
    }

    result = doca_flow_port_start(port_cfg, &g_pipelines_ctx.port);
    if (result != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA: port start failed: %s\n", doca_error_get_descr(result));
        doca_pipelines_cleanup();
        return -1;
    }

    array_cfg.size = 8;
    array_cfg.num_resources = static_cast<uint32_t>(nb_q);
    array_cfg.granularity = DOCA_FLOW_EXTERNAL_RESOURCE_GRANULARITY_64BIT;
    (void)doca_pipelines_probe_dynamic_aso_meta_indices();
    RTE_LOG(INFO, DOCA_PIPELINES, "DOCA ASO: dynamic_aso_line_meta_u32=%d dynamic_aso_offset_meta_u32=%d\n",
         g_pipelines_ctx.dynamic_aso_line_meta_u32, g_pipelines_ctx.dynamic_aso_offset_meta_u32);
    result = doca_flow_external_resource_array_create(&array_cfg, g_pipelines_ctx.port,
                                                      &g_pipelines_ctx.array_resource);
    if (result != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA ASO: array create failed: %s\n", doca_error_get_descr(result));
        doca_pipelines_cleanup();
        return -1;
    }

    result = doca_pipelines_run_meta_rr_pipeline(nb_q);
    if (result != DOCA_SUCCESS) {
        RTE_LOG(ERR, DOCA_PIPELINES, "DOCA meta_rr pipeline failed: %s\n", doca_error_get_descr(result));
        doca_pipelines_cleanup();
        return -1;
    }

    RTE_LOG(INFO, DOCA_PIPELINES, "DOCA meta_rr pipeline ready (%u queues)\n", static_cast<unsigned>(nb_q));
    g_pipelines_ctx.initialized = true;

    char n[RTE_ETH_NAME_MAX_LEN];
    if (rte_eth_dev_get_name_by_port(0, n) != 0) {
        RTE_LOG(WARNING, DOCA_PIPELINES, "DOCA: rte_eth_dev_get_name_by_port(0) failed\n");
        return -1;
    }
    return 0;
}

void doca_pipelines_cleanup(void)
{
    if (g_pipelines_ctx.initialized) {
        g_pipelines_ctx.graph.destroy();
        g_pipelines_ctx.nb_steer_queues = 0;

        if (g_pipelines_ctx.port != nullptr) {
            (void)doca_flow_port_stop(g_pipelines_ctx.port);
            g_pipelines_ctx.port = nullptr;
        }
        (void)doca_flow_destroy();
        g_pipelines_ctx.array_resource = nullptr;
        g_pipelines_ctx.initialized = false;
        RTE_LOG(INFO, DOCA_PIPELINES, "DOCA cleaned up\n");
    }
    if (g_doca_probe_dev != nullptr && g_doca_probe_dev_owned) {
        (void)doca_dev_close(g_doca_probe_dev);
        g_doca_probe_dev = nullptr;
        g_doca_probe_dev_owned = false;
    }
    g_doca_dpdk_port_probed = false;
}

doca_error_t doca_pipelines_probe_dynamic_aso_meta_indices()
{
    g_pipelines_ctx.dynamic_aso_line_meta_u32 = -1;
    g_pipelines_ctx.dynamic_aso_offset_meta_u32 = -1;
    if (g_pipelines_ctx.port == nullptr) {
        return DOCA_ERROR_INVALID_VALUE;
    }
    uint64_t mask = 0;
    doca_error_t r = doca_flow_external_action_get_dynamic_meta_indices(g_pipelines_ctx.port, &mask);
    if (r != DOCA_SUCCESS || mask == 0ULL) {
        return r != DOCA_SUCCESS ? r : DOCA_ERROR_NOT_SUPPORTED;
    }
    for (int i : {8}) {
        if ((mask & (1ULL << i)) && (mask & (1ULL << (i + 1)))) {
            g_pipelines_ctx.dynamic_aso_line_meta_u32 = i;
            g_pipelines_ctx.dynamic_aso_offset_meta_u32 = i + 1;
            return DOCA_SUCCESS;
        }
    }
    return DOCA_ERROR_NOT_SUPPORTED;
}
