/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2015 6WIND S.A.
 * Copyright 2015 Mellanox Technologies, Ltd
 */

#include <infiniband/mlx5dv.h>
#include <unistd.h>

#include <rte_ether.h>
#include <ethdev_driver.h>
#include <rte_interrupts.h>
#include <rte_alarm.h>
#include <rte_cycles.h>
#include <rte_pci.h>

#include <mlx5_malloc.h>

#include "mlx5.h"
#include "mlx5_flow.h"
#include "mlx5_rx.h"
#include "mlx5_tx.h"
#include "mlx5_utils.h"
#include "rte_common.h"
#include "rte_pmd_mlx5.h"

#include <doca_flow.h>
#include <doca_dev.h>
#include <doca_dpdk.h>
#include <doca_eth_rxq.h>
#include "doca_error.h"

#define DOCA_MAX_FLOWS (8096)

static struct doca_flow_port *doca_ports[RTE_MAX_ETHPORTS];
static struct doca_flow_pipe *doca_root_pipes[RTE_MAX_ETHPORTS];
static struct doca_flow_pipe *doca_esp_spi_pipes[RTE_MAX_ETHPORTS];
static struct doca_dev *doca_devs[RTE_MAX_ETHPORTS];

static void mlx5_traffic_disable_legacy(struct rte_eth_dev *dev);

doca_error_t eth_rxq_regular_receive(const char *ib_dev_name, bool timestamp_enable);

static void
entry_process_cb(struct doca_flow_pipe_entry *entry,
                 uint16_t pipe_queue,
                 enum doca_flow_entry_status status,
                 enum doca_flow_entry_op op,
                 void *user_ctx)
{
    const char *op_str = "UNKNOWN";

    switch (op) {
    case DOCA_FLOW_ENTRY_OP_ADD:
        op_str = "ADD";
        break;
    case DOCA_FLOW_ENTRY_OP_DEL:
        op_str = "DEL";
        break;
    default:
        break;
    }

    printf("DOCA Flow entry process callback:\n");
    printf("  entry      = %p\n", (void *)entry);
    printf("  queue      = %u\n", pipe_queue);
    printf("  operation  = %s\n", op_str);
    printf("  status     = %d\n", status);
    printf("  user_ctx   = %p\n", user_ctx);

    if (status != DOCA_FLOW_ENTRY_STATUS_SUCCESS) {
        printf("  result     = FAILED\n");
    } else {
        printf("  result     = SUCCESS\n");
    }
}

static int mlx5_doca_flow_init(struct rte_eth_dev *dev, const char *mode)
{
	struct doca_flow_cfg *flow_cfg;
	doca_error_t result, tmp_result;
	struct mlx5_priv *priv = dev->data->dev_private;

	result = doca_flow_cfg_create(&flow_cfg);
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "doca_flow_cfg_create failed\n");
		return -1;
	}

	result = doca_flow_cfg_set_pipe_queues(flow_cfg, priv->rxqs_n);
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to set doca_flow_cfg pipe_queues: %s", doca_error_get_descr(result));
		goto destroy_cfg;
	}

	result = doca_flow_cfg_set_mode_args(flow_cfg, mode);
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to set doca_flow_cfg mode_args: %s", doca_error_get_descr(result));
		goto destroy_cfg;
	}

	/*
	result = doca_flow_cfg_set_resource_mode(flow_cfg, DOCA_FLOW_RESOURCE_MODE_PORT);
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to set doca_flow_cfg resource mode: %s", doca_error_get_descr(result));
		goto destroy_cfg;
	}
	*/

	result = doca_flow_cfg_set_cb_entry_process(flow_cfg, entry_process_cb);
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to set doca_flow_cfg cb_entry_process: %s", doca_error_get_descr(result));
		goto destroy_cfg;
	}

	result = doca_flow_init(flow_cfg);
	if (result != DOCA_SUCCESS)
		DRV_LOG(ERR, "Failed to initialize doca flow: %s", doca_error_get_descr(result));

destroy_cfg:
	tmp_result = doca_flow_cfg_destroy(flow_cfg);
	if (tmp_result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to destroy doca_flow_cfg: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return (result == DOCA_SUCCESS) ? 0 : -1;
}

static struct doca_dev *
mlx5_open_doca_dev(struct rte_eth_dev *dev)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	struct doca_devinfo **dev_list = NULL;
	struct doca_dev *doca_dev = NULL;
	char pci_addr[PCI_PRI_STR_SIZE] = {0};
	uint32_t nb_devs = 0;
	uint32_t index;
	doca_error_t result;
	doca_error_t tmp_result;

	if (priv->pci_dev == NULL) {
		DRV_LOG(ERR, "Port %u is not backed by a PCI device",
			dev->data->port_id);
		return NULL;
	}
	rte_pci_device_name(&priv->pci_dev->addr, pci_addr, sizeof(pci_addr));
	result = doca_devinfo_create_list(&dev_list, &nb_devs);
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to enumerate DOCA devices for %s: %s",
			pci_addr, doca_error_get_descr(result));
		return NULL;
	}
	result = DOCA_ERROR_NOT_FOUND;
	for (index = 0; index < nb_devs; index++) {
		uint8_t is_equal = 0;

		tmp_result = doca_devinfo_is_equal_pci_addr(dev_list[index],
							    pci_addr, &is_equal);
		if (tmp_result != DOCA_SUCCESS) {
			DRV_LOG(DEBUG, "Failed to compare DOCA PCI address for %s: %s",
				pci_addr, doca_error_get_descr(tmp_result));
			continue;
		}
		if (!is_equal)
			continue;
		result = doca_dev_open(dev_list[index], &doca_dev);
		if (result != DOCA_SUCCESS) {
			DRV_LOG(ERR, "Failed to open DOCA device for %s: %s",
				pci_addr, doca_error_get_descr(result));
			doca_dev = NULL;
		}
		break;
	}
	tmp_result = doca_devinfo_destroy_list(dev_list);
	if (tmp_result != DOCA_SUCCESS) {
		DRV_LOG(WARNING, "Failed to destroy DOCA device list: %s",
			doca_error_get_descr(tmp_result));
	}
	if (doca_dev == NULL && result == DOCA_ERROR_NOT_FOUND) {
		DRV_LOG(ERR, "No DOCA device matches PCI address %s", pci_addr);
	}
	return doca_dev;
}

static struct doca_flow_port *mlx5_create_doca_flow_port(struct rte_eth_dev *dev)
{
	struct doca_flow_port_cfg *port_cfg;
	doca_error_t result, tmp_result;
	struct doca_flow_port *port;
	uint16_t port_id = dev->data->port_id;

	doca_devs[port_id] = mlx5_open_doca_dev(dev);
	if (doca_devs[port_id] == NULL) {
		DRV_LOG(ERR, "Failed to open DOCA device for port %u", port_id);
		return NULL;
	}

	result = doca_flow_port_cfg_create(&port_cfg);
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to create doca_flow_port_cfg: %s", doca_error_get_descr(result));
		doca_dev_close(doca_devs[port_id]);
		doca_devs[port_id] = NULL;
		return NULL;
	}

	result = doca_flow_port_cfg_set_port_id(port_cfg, port_id);
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to set doca_flow_port_cfg port_id: %s", doca_error_get_descr(result));
		goto destroy_port_cfg;
	}

	result = doca_flow_port_cfg_set_dev(port_cfg, doca_devs[port_id]);
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to set doca_flow_port_cfg doca device: %s", doca_error_get_descr(result));
		goto destroy_port_cfg;
	}

	result = doca_flow_port_cfg_set_actions_mem_size(
		port_cfg,
		rte_align32pow2(DOCA_MAX_FLOWS * DOCA_FLOW_MAX_ENTRY_ACTIONS_MEM_SIZE));
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to set doca_flow_port_cfg actions mem size: %s", doca_error_get_descr(result));
		goto destroy_port_cfg;
	}

	result = doca_flow_port_start(port_cfg, &port);
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to start doca_flow port: %s", doca_error_get_descr(result));
		goto destroy_port_cfg;
	}

destroy_port_cfg:
	tmp_result = doca_flow_port_cfg_destroy(port_cfg);
	if (tmp_result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to destroy doca_flow port: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	if (result != DOCA_SUCCESS && doca_devs[port_id] != NULL) {
		tmp_result = doca_dev_close(doca_devs[port_id]);
		if (tmp_result != DOCA_SUCCESS) {
			DRV_LOG(WARNING, "Failed to close DOCA device for port %u: %s",
				port_id, doca_error_get_descr(tmp_result));
		}
		doca_devs[port_id] = NULL;
	}

	return result == DOCA_SUCCESS ? port : NULL;
}

static bool
mlx5_doca_flow_ports_active(void)
{
	uint16_t port_id;

	for (port_id = 0; port_id < RTE_MAX_ETHPORTS; port_id++) {
		if (doca_ports[port_id] != NULL)
			return true;
	}
	return false;
}

static void
mlx5_doca_flow_teardown(struct rte_eth_dev *dev)
{
	uint16_t port_id = dev->data->port_id;
	doca_error_t result;

	if (doca_root_pipes[port_id] != NULL) {
		doca_flow_pipe_destroy(doca_root_pipes[port_id]);
		doca_root_pipes[port_id] = NULL;
	}
	if (doca_esp_spi_pipes[port_id] != NULL) {
		doca_flow_pipe_destroy(doca_esp_spi_pipes[port_id]);
		doca_esp_spi_pipes[port_id] = NULL;
	}
	if (doca_ports[port_id] != NULL) {
		doca_flow_port_pipes_flush(doca_ports[port_id]);
		result = doca_flow_port_stop(doca_ports[port_id]);
		if (result != DOCA_SUCCESS) {
			DRV_LOG(WARNING, "Failed to stop DOCA flow port %u: %s",
				port_id, doca_error_get_descr(result));
		}
		doca_ports[port_id] = NULL;
	}
	if (doca_devs[port_id] != NULL) {
		result = doca_dev_close(doca_devs[port_id]);
		if (result != DOCA_SUCCESS) {
			DRV_LOG(WARNING, "Failed to close DOCA device for port %u: %s",
				port_id, doca_error_get_descr(result));
		}
		doca_devs[port_id] = NULL;
	}
	if (!mlx5_doca_flow_ports_active())
		doca_flow_destroy();
}

/*
 * Create a non-root BASIC pipe that steers ESP packets by SPI to individual
 * RX queues.  The pipe uses DOCA_FLOW_FWD_CHANGEABLE so each entry can
 * supply its own forwarding action (single-queue RSS).
 *
 * Match template: outer ESP, full SPI mask (0xFFFFFFFF).
 */
static int
mlx5_create_doca_esp_spi_pipe(struct doca_flow_port *port,
			      struct doca_flow_pipe **pipe_out)
{
	struct doca_flow_pipe_cfg *pipe_cfg = NULL;
	struct doca_flow_pipe *pipe = NULL;
	struct doca_flow_match match = {0};
	struct doca_flow_match match_mask = {0};
	struct doca_flow_actions actions = {0};
	struct doca_flow_actions *actions_arr[] = {&actions};
	struct doca_flow_fwd fwd = {0};
	struct doca_flow_fwd fwd_miss = {0};
	doca_error_t result;

	/* Template: entry supplies ESP SPI; tunnel type remains ESP. */
	match.tun.type = DOCA_FLOW_TUN_ESP;
	match.tun.esp_spi = RTE_BE32(0xFFFFFFFF);
	match_mask.tun.type = UINT32_MAX;
	match_mask.tun.esp_spi = RTE_BE32(0xFFFFFFFF);

	result = doca_flow_pipe_cfg_create(&pipe_cfg, port);
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to create esp_spi pipe_cfg: %s",
			doca_error_get_descr(result));
		return -1;
	}
	result = doca_flow_pipe_cfg_set_name(pipe_cfg, "mlx5_esp_spi");
	if (result != DOCA_SUCCESS)
		goto destroy_cfg;
	result = doca_flow_pipe_cfg_set_type(pipe_cfg, DOCA_FLOW_PIPE_BASIC);
	if (result != DOCA_SUCCESS)
		goto destroy_cfg;
	result = doca_flow_pipe_cfg_set_domain(pipe_cfg,
					      DOCA_FLOW_PIPE_DOMAIN_SECURE_INGRESS);
	if (result != DOCA_SUCCESS)
		goto destroy_cfg;
	result = doca_flow_pipe_cfg_set_is_root(pipe_cfg, false);
	if (result != DOCA_SUCCESS)
		goto destroy_cfg;
	result = doca_flow_pipe_cfg_set_nr_entries(pipe_cfg, DOCA_MAX_FLOWS);
	if (result != DOCA_SUCCESS)
		goto destroy_cfg;
	result = doca_flow_pipe_cfg_set_match(pipe_cfg, &match, &match_mask);
	if (result != DOCA_SUCCESS)
		goto destroy_cfg;
	result = doca_flow_pipe_cfg_set_actions(pipe_cfg, actions_arr,
					     NULL, NULL, 1);
	if (result != DOCA_SUCCESS)
		goto destroy_cfg;
	/*
	 * Use changeable RSS template: entry-level forwarding will provide
	 * concrete queue and RSS flags.
	 */
	fwd.type = DOCA_FLOW_FWD_RSS;
	fwd.rss_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;
	fwd.rss.nr_queues = -1;
	fwd_miss.type = DOCA_FLOW_FWD_DROP;
	result = doca_flow_pipe_create(pipe_cfg, &fwd, &fwd_miss, &pipe);
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to create esp_spi pipe: %s",
			doca_error_get_descr(result));
		goto destroy_cfg;
	}
	*pipe_out = pipe;

destroy_cfg:
	if (pipe_cfg)
		doca_flow_pipe_cfg_destroy(pipe_cfg);
	return (result == DOCA_SUCCESS) ? 0 : -1;
}

/*
 * Add one entry per RX queue into the ESP SPI pipe:
 *   SPI = htonl(queue + 1)  →  RSS { queues=[queue], nr_queues=1 }
 *
 * The sender cycles SPI=1,2,...,nr_queues,1,2,... for round-robin delivery.
 */
static int
mlx5_add_esp_spi_entries(struct doca_flow_port *port,
			  struct doca_flow_pipe *pipe,
			  uint16_t nr_queues)
{
	uint16_t q;

	for (q = 0; q < nr_queues; q++) {
		struct doca_flow_match match = {0};
		struct doca_flow_fwd fwd = {0};
		struct doca_flow_pipe_entry *entry = NULL;
		uint16_t queues_arr[1] = {q};
		doca_error_t result;

		/* SPI value for this queue (1-based, network byte order) */
		match.tun.type = DOCA_FLOW_TUN_ESP;
		match.tun.esp_spi = rte_cpu_to_be_32((uint32_t)q + 1);

		/* Forward to a single RX queue via RSS */
		fwd.type = DOCA_FLOW_FWD_RSS;
		fwd.rss_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;
		fwd.rss.outer_flags = DOCA_FLOW_RSS_ESP;
		fwd.rss.queues_array = queues_arr;
		fwd.rss.nr_queues = 1;

		result = doca_flow_pipe_basic_add_entry(0, pipe, &match, 0,
						    NULL, NULL, &fwd,
						    DOCA_FLOW_ENTRY_FLAGS_NO_WAIT,
						    NULL, &entry);
		if (result != DOCA_SUCCESS) {
			DRV_LOG(ERR, "Failed to add ESP SPI entry q%u: %s",
				q, doca_error_get_descr(result));
			return -1;
		}
		result = doca_flow_entries_process(port, 0, 0, 1);
		if (result != DOCA_SUCCESS) {
			DRV_LOG(ERR, "Failed to process ESP SPI entry q%u: %s",
				q, doca_error_get_descr(result));
			return -1;
		}
		if (doca_flow_pipe_entry_get_status(entry) !=
		    DOCA_FLOW_ENTRY_STATUS_SUCCESS) {
			DRV_LOG(ERR, "ESP SPI entry q%u not offloaded", q);
			return -1;
		}
		DRV_LOG(DEBUG, "ESP SPI entry: SPI=0x%08x → queue %u",
			rte_be_to_cpu_32(match.tun.esp_spi), q);
	}
	return 0;
}

static int
mlx5_create_doca_root_basic_pipe(struct doca_flow_port *port,
				 struct doca_flow_pipe *next_pipe,
				 struct doca_flow_pipe **pipe_out)
{
	struct doca_flow_pipe_cfg *pipe_cfg = NULL;
	struct doca_flow_pipe *pipe = NULL;
	struct doca_flow_match match = {0};
	struct doca_flow_actions actions = {0};
	struct doca_flow_actions *actions_arr[] = {&actions};
	struct doca_flow_fwd fwd = {0};
	struct doca_flow_fwd fwd_miss = {0};
	doca_error_t result;

	result = doca_flow_pipe_cfg_create(&pipe_cfg, port);
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to create doca_flow_pipe_cfg: %s",
			doca_error_get_descr(result));
		return -1;
	}
	result = doca_flow_pipe_cfg_set_name(pipe_cfg, "mlx5_root_basic");
	if (result != DOCA_SUCCESS)
		goto destroy_cfg;
	result = doca_flow_pipe_cfg_set_type(pipe_cfg, DOCA_FLOW_PIPE_BASIC);
	if (result != DOCA_SUCCESS)
		goto destroy_cfg;
	result = doca_flow_pipe_cfg_set_is_root(pipe_cfg, true);
	if (result != DOCA_SUCCESS)
		goto destroy_cfg;
	result = doca_flow_pipe_cfg_set_nr_entries(pipe_cfg, DOCA_MAX_FLOWS);
	if (result != DOCA_SUCCESS)
		goto destroy_cfg;
	result = doca_flow_pipe_cfg_set_match(pipe_cfg, &match, &match);
	if (result != DOCA_SUCCESS)
		goto destroy_cfg;
	result = doca_flow_pipe_cfg_set_actions(pipe_cfg, actions_arr,
					     NULL, NULL, 1);
	if (result != DOCA_SUCCESS)
		goto destroy_cfg;
	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = next_pipe;
	fwd_miss.type = DOCA_FLOW_FWD_DROP;
	result = doca_flow_pipe_create(pipe_cfg, &fwd, &fwd_miss, &pipe);
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to create root basic pipe: %s",
			doca_error_get_descr(result));
		goto destroy_cfg;
	}
	*pipe_out = pipe;

destroy_cfg:
	if (pipe_cfg)
		doca_flow_pipe_cfg_destroy(pipe_cfg);
	return (result == DOCA_SUCCESS) ? 0 : -1;
}

static int
mlx5_add_doca_root_entry(struct doca_flow_port *port, struct doca_flow_pipe *pipe)
{
	struct doca_flow_match match = {0};
	struct doca_flow_actions actions = {0};
	struct doca_flow_pipe_entry *entry = NULL;
	doca_error_t result;

	result = doca_flow_pipe_basic_add_entry(0, pipe, &match, 0,
						&actions, NULL, NULL,
						DOCA_FLOW_ENTRY_FLAGS_NO_WAIT,
						NULL, &entry);
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to add root pipe entry: %s",
			doca_error_get_descr(result));
		return -1;
	}
	result = doca_flow_entries_process(port, 0, 0, 1);
	if (result != DOCA_SUCCESS) {
		DRV_LOG(ERR, "Failed to process DOCA entries: %s",
			doca_error_get_descr(result));
		return -1;
	}
	if (doca_flow_pipe_entry_get_status(entry) != DOCA_FLOW_ENTRY_STATUS_SUCCESS) {
		DRV_LOG(ERR, "Root pipe entry was not offloaded successfully");
		return -1;
	}
	return 0;
}

/**
 * Stop traffic on Tx queues.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 */
static void
mlx5_txq_stop(struct rte_eth_dev *dev)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	unsigned int i;

	for (i = 0; i != priv->txqs_n; ++i)
		mlx5_txq_release(dev, i);
}

/**
 * Start traffic on Tx queues.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
static int
mlx5_txq_start(struct rte_eth_dev *dev)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	struct mlx5_txq_ctrl *master_txq_ctrl;
	uint8_t log_mu_grp_size = dev->data->mu_sq_log_grp_size;
	uint32_t log_max_wqe = log2above(mlx5_dev_get_max_wq_size(priv->sh));
	uint32_t flags = MLX5_MEM_RTE | MLX5_MEM_ZERO;
	unsigned int i=0, cnt;
	int ret;

	priv->sh->mu_group.log_group_size = log_mu_grp_size;
	priv->sh->mu_group.group_size = 1 << log_mu_grp_size;
	// Single-user SQ
	if (log_mu_grp_size == 0) {
		for (cnt = log_max_wqe; cnt > 0; cnt -= 1) {
			for (i = 0; i != priv->txqs_n; ++i) {
				struct mlx5_txq_ctrl *txq_ctrl = mlx5_txq_get(dev, i);
				struct mlx5_txq_data *txq_data = &txq_ctrl->txq;

				if (!txq_ctrl)
					continue;
				if (txq_data->elts_n != cnt) {
					mlx5_txq_release(dev, i);
					continue;
				}
				if (!txq_ctrl->is_hairpin)
					txq_alloc_elts(txq_ctrl);
				MLX5_ASSERT(!txq_ctrl->obj);
				txq_ctrl->obj = mlx5_malloc_numa_tolerant(flags,
					      sizeof(struct mlx5_txq_obj),
					      0, txq_ctrl->socket);
				if (!txq_ctrl->obj) {
					DRV_LOG(ERR, "Port %u Tx queue %u cannot allocate "
	 				    "memory resources.", dev->data->port_id,
					     txq_data->idx);
					rte_errno = ENOMEM;
					goto error;
				}
				ret = priv->obj_ops.txq_obj_new(dev, i);
				if (ret < 0) {
					mlx5_free(txq_ctrl->obj);
					txq_ctrl->obj = NULL;
					goto error;
				}
				if (!txq_ctrl->is_hairpin) {
					size_t size = txq_data->cqe_s * sizeof(*txq_data->fcqs);

					txq_data->fcqs = mlx5_malloc_numa_tolerant(flags, size,
						RTE_CACHE_LINE_SIZE,
						txq_ctrl->socket);
					if (!txq_data->fcqs) {
						DRV_LOG(ERR, "Port %u Tx queue %u cannot "
						      "allocate memory (FCQ).",
						      dev->data->port_id, i);
						rte_errno = ENOMEM;
						goto error;
					}
				}
				DRV_LOG(DEBUG, "Port %u txq %u updated with %p.",
				    dev->data->port_id, i, (void *)&txq_ctrl->obj);
				LIST_INSERT_HEAD(&priv->txqsobj, txq_ctrl->obj, next);
			}
		}
	}
	// Multi-user SQ
	else {

		// Master SQ dev creation

		master_txq_ctrl = mlx5_txq_get(dev, 0);
		struct mlx5_txq_data *master_txq_data = &master_txq_ctrl->txq;

		if (!master_txq_ctrl->is_hairpin)
			txq_alloc_elts(master_txq_ctrl);
		MLX5_ASSERT(!master_txq_ctrl->obj);
		master_txq_ctrl->obj = mlx5_malloc_numa_tolerant(flags,
					    sizeof(struct mlx5_txq_obj),
					    0, master_txq_ctrl->socket);
		if (!master_txq_ctrl->obj) {
			DRV_LOG(ERR, "Port %u Tx queue %u cannot allocate "
			   "memory resources.", dev->data->port_id,
			   master_txq_data->idx);
			rte_errno = ENOMEM;
			goto error;
		}
		ret = priv->obj_ops.txq_obj_new(dev, 0);
		printf("Created CQ with CQN: 0x%x\n", master_txq_ctrl->obj->cq_obj.cq->id);
		if (ret < 0) {
			mlx5_free(master_txq_ctrl->obj);
			master_txq_ctrl->obj = NULL;
		goto error;
		}
		if (!master_txq_ctrl->is_hairpin) {
			size_t size = master_txq_data->cqe_s * sizeof(*master_txq_data->fcqs);

			master_txq_data->fcqs = mlx5_malloc_numa_tolerant(flags, size,
					      RTE_CACHE_LINE_SIZE,
					      master_txq_ctrl->socket);
			if (!master_txq_data->fcqs) {
				DRV_LOG(ERR, "Port %u Tx queue %u cannot "
				    "allocate memory (FCQ).",
				    dev->data->port_id, 0);
				rte_errno = ENOMEM;
				goto error;
			}
		}
		DRV_LOG(DEBUG, "Port %u txq %u updated with %p.",
			  dev->data->port_id, 0, (void *)&master_txq_ctrl->obj);
		LIST_INSERT_HEAD(&priv->txqsobj, master_txq_ctrl->obj, next);

		for (unsigned int idx = 1; idx != priv->txqs_n; ++idx) {

			struct mlx5_txq_ctrl *txq_ctrl = mlx5_txq_get(dev, idx);
			struct mlx5_txq_data *txq_data = &txq_ctrl->txq;
			struct mlx5_priv *priv = dev->data->dev_private;
			struct mlx5_dev_ctx_shared *sh = priv->sh;
			struct mlx5_proc_priv *ppriv = MLX5_PROC_PRIV(PORT_ID(priv));
			struct mlx5_txq_data *master_txq_data = &master_txq_ctrl->txq;
			struct mlx5_txq_obj *master_txq_obj = master_txq_ctrl->obj;

			// Memory structure:
			// < Master WQ + CQ > < Master CQ DBR > < Master SQ DBR > < Slave i SQ DBR >
			txq_ctrl->is_master = false;
			txq_data->qp_db = RTE_PTR_ADD(master_txq_obj->sq_obj.db_rec, (idx * MLX5_DBR_SIZE));
			txq_data->qp_db = &txq_data->qp_db[MLX5_SND_DBR];
			*txq_data->qp_db = 0;
			ppriv->uar_table[txq_data->idx] = sh->tx_uar.bf_db;

			/* Create the Work Queue. */
			txq_data->wqe_n = master_txq_data->wqe_n;
			txq_data->wqe_s = master_txq_data->wqe_s;
			txq_data->wqe_m = master_txq_data->wqe_m;
			txq_data->wqes = master_txq_data->wqes;
			txq_data->wqes_end = master_txq_data->wqes_end;
			txq_data->wqe_ci = MLX5_MU_WQE_SIZE * idx;
			txq_data->wqe_pi = 0;
			txq_data->wqe_group_thres = MLX5_MU_WQE_SIZE * (sh->mu_group.group_size - idx);
			txq_data->wqe_comp = 0;
			txq_data->wqe_thres = master_txq_data->wqe_thres;
			txq_data->qp_num_8s = (master_txq_obj->sq_obj.sq->id + idx) << 8;
			txq_data->db_heu = sh->cdev->config.dbnc == MLX5_SQ_DB_HEURISTIC;
			txq_data->db_nc = sh->tx_uar.dbnc;
			txq_data->wait_on_time = !!(!sh->config.tx_pp &&
				sh->cdev->config.hca_attr.wait_on_time);
			txq_data->cq_ci = idx;
			txq_data->cq_pi = idx;
			txq_data->cqes = master_txq_data->cqes;
			txq_data->cqe_s = master_txq_data->cqe_s;
			txq_data->cqe_n = master_txq_data->cqe_n;
			txq_data->cqe_m = master_txq_data->cqe_m;
			/* Per-slave fcqs: each slave tracks its own completion
			 * heads so parallel requests from different slaves can't
			 * overwrite each other. Sized same as the shared CQ, used
			 * by the slave alone — capacity in # of outstanding
			 * requests is cqe_s (since cq_pi advances by group_size
			 * per request and we shift out those bits when indexing).
			 */
			{
				size_t size = txq_data->cqe_s *
					      sizeof(*txq_data->fcqs);
				txq_data->fcqs = mlx5_malloc_numa_tolerant(
					flags, size, RTE_CACHE_LINE_SIZE,
					txq_ctrl->socket);
				if (!txq_data->fcqs) {
					DRV_LOG(ERR, "Port %u slave TxQ %u "
						"cannot allocate memory (FCQ).",
						dev->data->port_id, idx);
					rte_errno = ENOMEM;
					goto error;
				}
			}
			txq_data->cq_db= master_txq_data->cq_db;
		}
	}
	return 0;
error:
	ret = rte_errno; /* Save rte_errno before cleanup. */
	do {
		mlx5_txq_release(dev, i);
	} while (i-- != 0);
	rte_errno = ret; /* Restore rte_errno. */
	return -rte_errno;
}

/**
 * Register Rx queue mempools and fill the Rx queue cache.
 * This function tolerates repeated mempool registration.
 *
 * @param[in] rxq_ctrl
 *   Rx queue control data.
 *
 * @return
 *   0 on success, (-1) on failure and rte_errno is set.
 */
static int
mlx5_rxq_mempool_register(struct mlx5_rxq_ctrl *rxq_ctrl)
{
	struct rte_mempool *mp;
	uint32_t s;
	int ret = 0;

	mlx5_mr_flush_local_cache(&rxq_ctrl->rxq.mr_ctrl);
	/* MPRQ mempool is registered on creation, just fill the cache. */
	if (mlx5_rxq_mprq_enabled(&rxq_ctrl->rxq))
		return mlx5_mr_mempool_populate_cache(&rxq_ctrl->rxq.mr_ctrl,
						      rxq_ctrl->rxq.mprq_mp);
	for (s = 0; s < rxq_ctrl->rxq.rxseg_n; s++) {
		bool is_extmem;

		mp = rxq_ctrl->rxq.rxseg[s].mp;
		is_extmem = (rte_pktmbuf_priv_flags(mp) &
			     RTE_PKTMBUF_POOL_F_PINNED_EXT_BUF) != 0;
		ret = mlx5_mr_mempool_register(rxq_ctrl->sh->cdev, mp,
					       is_extmem);
		if (ret < 0 && rte_errno != EEXIST)
			return ret;
		ret = mlx5_mr_mempool_populate_cache(&rxq_ctrl->rxq.mr_ctrl,
						     mp);
		if (ret < 0)
			return ret;
	}
	return 0;
}

/**
 * Stop traffic on Rx queues.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 */
static void
mlx5_rxq_stop(struct rte_eth_dev *dev)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	unsigned int i;

	for (i = 0; i != priv->rxqs_n; ++i)
		mlx5_rxq_release(dev, i);
}

static int
mlx5_rxq_ctrl_prepare(struct rte_eth_dev *dev, struct mlx5_rxq_ctrl *rxq_ctrl,
		      unsigned int idx)
{
	int ret = 0;

	if (!rxq_ctrl->is_hairpin) {
		/*
		 * Pre-register the mempools. Regardless of whether
		 * the implicit registration is enabled or not,
		 * Rx mempool destruction is tracked to free MRs.
		 */
		if (mlx5_rxq_mempool_register(rxq_ctrl) < 0)
			return -rte_errno;
		ret = rxq_alloc_elts(rxq_ctrl);
		if (ret)
			return ret;
	}
	MLX5_ASSERT(!rxq_ctrl->obj);
	rxq_ctrl->obj = mlx5_malloc_numa_tolerant(MLX5_MEM_RTE | MLX5_MEM_ZERO,
						  sizeof(*rxq_ctrl->obj), 0,
						  rxq_ctrl->socket);
	if (!rxq_ctrl->obj) {
		DRV_LOG(ERR, "Port %u Rx queue %u can't allocate resources.",
			dev->data->port_id, idx);
		rte_errno = ENOMEM;
		return -rte_errno;
	}
	DRV_LOG(DEBUG, "Port %u rxq %u updated with %p.", dev->data->port_id,
		idx, (void *)&rxq_ctrl->obj);
	return 0;
}

/**
 * Start traffic on Rx queues.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
static int
mlx5_rxq_start(struct rte_eth_dev *dev)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	unsigned int i;
	int ret = 0;

	/* Allocate/reuse/resize mempool for Multi-Packet RQ. */
	if (mlx5_mprq_alloc_mp(dev)) {
		/* Should not release Rx queues but return immediately. */
		return -rte_errno;
	}
	DRV_LOG(DEBUG, "Port %u max work queue size is %d.",
		dev->data->port_id, mlx5_dev_get_max_wq_size(priv->sh));
	DRV_LOG(DEBUG, "Port %u dev_cap.max_sge is %d.",
		dev->data->port_id, priv->sh->dev_cap.max_sge);
	for (i = 0; i != priv->rxqs_n; ++i) {
		struct mlx5_rxq_priv *rxq = mlx5_rxq_ref(dev, i);
		struct mlx5_rxq_ctrl *rxq_ctrl;

		if (rxq == NULL)
			continue;
		rxq_ctrl = rxq->ctrl;
		if (!rxq_ctrl->started)
			if (mlx5_rxq_ctrl_prepare(dev, rxq_ctrl, i) < 0)
				goto error;
		ret = priv->obj_ops.rxq_obj_new(rxq);
		if (ret) {
			mlx5_free(rxq_ctrl->obj);
			rxq_ctrl->obj = NULL;
			goto error;
		}
		if (!rxq_ctrl->started)
			LIST_INSERT_HEAD(&priv->rxqsobj, rxq_ctrl->obj, next);
		rxq_ctrl->started = true;
	}
	return 0;
error:
	ret = rte_errno; /* Save rte_errno before cleanup. */
	do {
		mlx5_rxq_release(dev, i);
	} while (i-- != 0);
	rte_errno = ret; /* Restore rte_errno. */
	return -rte_errno;
}

/**
 * Binds Tx queues to Rx queues for hairpin.
 *
 * Binds Tx queues to the target Rx queues.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
static int
mlx5_hairpin_auto_bind(struct rte_eth_dev *dev)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	struct mlx5_devx_modify_sq_attr sq_attr = { 0 };
	struct mlx5_devx_modify_rq_attr rq_attr = { 0 };
	struct mlx5_txq_ctrl *txq_ctrl;
	struct mlx5_rxq_priv *rxq;
	struct mlx5_rxq_ctrl *rxq_ctrl;
	struct mlx5_devx_obj *sq;
	struct mlx5_devx_obj *rq;
	unsigned int i;
	int ret = 0;
	bool need_auto = false;
	uint16_t self_port = dev->data->port_id;

	for (i = 0; i != priv->txqs_n; ++i) {
		txq_ctrl = mlx5_txq_get(dev, i);
		if (!txq_ctrl)
			continue;
		if (!txq_ctrl->is_hairpin ||
		    txq_ctrl->hairpin_conf.peers[0].port != self_port) {
			mlx5_txq_release(dev, i);
			continue;
		}
		if (txq_ctrl->hairpin_conf.manual_bind) {
			mlx5_txq_release(dev, i);
			return 0;
		}
		need_auto = true;
		mlx5_txq_release(dev, i);
	}
	if (!need_auto)
		return 0;
	for (i = 0; i != priv->txqs_n; ++i) {
		txq_ctrl = mlx5_txq_get(dev, i);
		if (!txq_ctrl)
			continue;
		/* Skip hairpin queues with other peer ports. */
		if (!txq_ctrl->is_hairpin ||
		    txq_ctrl->hairpin_conf.peers[0].port != self_port) {
			mlx5_txq_release(dev, i);
			continue;
		}
		if (!txq_ctrl->obj) {
			rte_errno = ENOMEM;
			DRV_LOG(ERR, "port %u no txq object found: %d",
				dev->data->port_id, i);
			mlx5_txq_release(dev, i);
			return -rte_errno;
		}
		sq = txq_ctrl->obj->sq;
		rxq = mlx5_rxq_get(dev, txq_ctrl->hairpin_conf.peers[0].queue);
		if (rxq == NULL) {
			mlx5_txq_release(dev, i);
			rte_errno = EINVAL;
			DRV_LOG(ERR, "port %u no rxq object found: %d",
				dev->data->port_id,
				txq_ctrl->hairpin_conf.peers[0].queue);
			return -rte_errno;
		}
		rxq_ctrl = rxq->ctrl;
		if (!rxq_ctrl->is_hairpin ||
		    rxq->hairpin_conf.peers[0].queue != i) {
			rte_errno = ENOMEM;
			DRV_LOG(ERR, "port %u Tx queue %d can't be binded to "
				"Rx queue %d", dev->data->port_id,
				i, txq_ctrl->hairpin_conf.peers[0].queue);
			goto error;
		}
		rq = rxq_ctrl->obj->rq;
		if (!rq) {
			rte_errno = ENOMEM;
			DRV_LOG(ERR, "port %u hairpin no matching rxq: %d",
				dev->data->port_id,
				txq_ctrl->hairpin_conf.peers[0].queue);
			goto error;
		}
		sq_attr.state = MLX5_SQC_STATE_RDY;
		sq_attr.sq_state = MLX5_SQC_STATE_RST;
		sq_attr.hairpin_peer_rq = rq->id;
		sq_attr.hairpin_peer_vhca =
				priv->sh->cdev->config.hca_attr.vhca_id;
		ret = mlx5_devx_cmd_modify_sq(sq, &sq_attr);
		if (ret)
			goto error;
		rq_attr.state = MLX5_RQC_STATE_RDY;
		rq_attr.rq_state = MLX5_RQC_STATE_RST;
		rq_attr.hairpin_peer_sq = sq->id;
		rq_attr.hairpin_peer_vhca =
				priv->sh->cdev->config.hca_attr.vhca_id;
		ret = mlx5_devx_cmd_modify_rq(rq, &rq_attr);
		if (ret)
			goto error;
		/* Qs with auto-bind will be destroyed directly. */
		rxq->hairpin_status = 1;
		txq_ctrl->hairpin_status = 1;
		mlx5_txq_release(dev, i);
	}
	return 0;
error:
	mlx5_txq_release(dev, i);
	return -rte_errno;
}

/*
 * Fetch the peer queue's SW & HW information.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param peer_queue
 *   Index of the queue to fetch the information.
 * @param current_info
 *   Pointer to the input peer information, not used currently.
 * @param peer_info
 *   Pointer to the structure to store the information, output.
 * @param direction
 *   Positive to get the RxQ information, zero to get the TxQ information.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
mlx5_hairpin_queue_peer_update(struct rte_eth_dev *dev, uint16_t peer_queue,
			       struct rte_hairpin_peer_info *current_info,
			       struct rte_hairpin_peer_info *peer_info,
			       uint32_t direction)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	RTE_SET_USED(current_info);

	if (dev->data->dev_started == 0) {
		rte_errno = EBUSY;
		DRV_LOG(ERR, "peer port %u is not started",
			dev->data->port_id);
		return -rte_errno;
	}
	/*
	 * Peer port used as egress. In the current design, hairpin Tx queue
	 * will be bound to the peer Rx queue. Indeed, only the information of
	 * peer Rx queue needs to be fetched.
	 */
	if (direction == 0) {
		struct mlx5_txq_ctrl *txq_ctrl;

		txq_ctrl = mlx5_txq_get(dev, peer_queue);
		if (txq_ctrl == NULL) {
			rte_errno = EINVAL;
			DRV_LOG(ERR, "Failed to get port %u Tx queue %d",
				dev->data->port_id, peer_queue);
			return -rte_errno;
		}
		if (!txq_ctrl->is_hairpin) {
			rte_errno = EINVAL;
			DRV_LOG(ERR, "port %u queue %d is not a hairpin Txq",
				dev->data->port_id, peer_queue);
			mlx5_txq_release(dev, peer_queue);
			return -rte_errno;
		}
		if (txq_ctrl->obj == NULL || txq_ctrl->obj->sq == NULL) {
			rte_errno = ENOMEM;
			DRV_LOG(ERR, "port %u no Txq object found: %d",
				dev->data->port_id, peer_queue);
			mlx5_txq_release(dev, peer_queue);
			return -rte_errno;
		}
		peer_info->qp_id = mlx5_txq_get_sqn(txq_ctrl);
		peer_info->vhca_id = priv->sh->cdev->config.hca_attr.vhca_id;
		/* 1-to-1 mapping, only the first one is used. */
		peer_info->peer_q = txq_ctrl->hairpin_conf.peers[0].queue;
		peer_info->tx_explicit = txq_ctrl->hairpin_conf.tx_explicit;
		peer_info->manual_bind = txq_ctrl->hairpin_conf.manual_bind;
		mlx5_txq_release(dev, peer_queue);
	} else { /* Peer port used as ingress. */
		struct mlx5_rxq_priv *rxq = mlx5_rxq_get(dev, peer_queue);
		struct mlx5_rxq_ctrl *rxq_ctrl;

		if (rxq == NULL) {
			rte_errno = EINVAL;
			DRV_LOG(ERR, "Failed to get port %u Rx queue %d",
				dev->data->port_id, peer_queue);
			return -rte_errno;
		}
		rxq_ctrl = rxq->ctrl;
		if (!rxq_ctrl->is_hairpin) {
			rte_errno = EINVAL;
			DRV_LOG(ERR, "port %u queue %d is not a hairpin Rxq",
				dev->data->port_id, peer_queue);
			return -rte_errno;
		}
		if (rxq_ctrl->obj == NULL || rxq_ctrl->obj->rq == NULL) {
			rte_errno = ENOMEM;
			DRV_LOG(ERR, "port %u no Rxq object found: %d",
				dev->data->port_id, peer_queue);
			return -rte_errno;
		}
		peer_info->qp_id = rxq_ctrl->obj->rq->id;
		peer_info->vhca_id = priv->sh->cdev->config.hca_attr.vhca_id;
		peer_info->peer_q = rxq->hairpin_conf.peers[0].queue;
		peer_info->tx_explicit = rxq->hairpin_conf.tx_explicit;
		peer_info->manual_bind = rxq->hairpin_conf.manual_bind;
	}
	return 0;
}

/*
 * Bind the hairpin queue with the peer HW information.
 * This needs to be called twice both for Tx and Rx queues of a pair.
 * If the queue is already bound, it is considered successful.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param cur_queue
 *   Index of the queue to change the HW configuration to bind.
 * @param peer_info
 *   Pointer to information of the peer queue.
 * @param direction
 *   Positive to configure the TxQ, zero to configure the RxQ.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
mlx5_hairpin_queue_peer_bind(struct rte_eth_dev *dev, uint16_t cur_queue,
			     struct rte_hairpin_peer_info *peer_info,
			     uint32_t direction)
{
	int ret = 0;

	/*
	 * Consistency checking of the peer queue: opposite direction is used
	 * to get the peer queue info with ethdev port ID, no need to check.
	 */
	if (peer_info->peer_q != cur_queue) {
		rte_errno = EINVAL;
		DRV_LOG(ERR, "port %u queue %d and peer queue %d mismatch",
			dev->data->port_id, cur_queue, peer_info->peer_q);
		return -rte_errno;
	}
	if (direction != 0) {
		struct mlx5_txq_ctrl *txq_ctrl;
		struct mlx5_devx_modify_sq_attr sq_attr = { 0 };

		txq_ctrl = mlx5_txq_get(dev, cur_queue);
		if (txq_ctrl == NULL) {
			rte_errno = EINVAL;
			DRV_LOG(ERR, "Failed to get port %u Tx queue %d",
				dev->data->port_id, cur_queue);
			return -rte_errno;
		}
		if (!txq_ctrl->is_hairpin) {
			rte_errno = EINVAL;
			DRV_LOG(ERR, "port %u queue %d not a hairpin Txq",
				dev->data->port_id, cur_queue);
			mlx5_txq_release(dev, cur_queue);
			return -rte_errno;
		}
		if (txq_ctrl->obj == NULL || txq_ctrl->obj->sq == NULL) {
			rte_errno = ENOMEM;
			DRV_LOG(ERR, "port %u no Txq object found: %d",
				dev->data->port_id, cur_queue);
			mlx5_txq_release(dev, cur_queue);
			return -rte_errno;
		}
		if (txq_ctrl->hairpin_status != 0) {
			DRV_LOG(DEBUG, "port %u Tx queue %d is already bound",
				dev->data->port_id, cur_queue);
			mlx5_txq_release(dev, cur_queue);
			return 0;
		}
		/*
		 * All queues' of one port consistency checking is done in the
		 * bind() function, and that is optional.
		 */
		if (peer_info->tx_explicit !=
		    txq_ctrl->hairpin_conf.tx_explicit) {
			rte_errno = EINVAL;
			DRV_LOG(ERR, "port %u Tx queue %d and peer Tx rule mode"
				" mismatch", dev->data->port_id, cur_queue);
			mlx5_txq_release(dev, cur_queue);
			return -rte_errno;
		}
		if (peer_info->manual_bind !=
		    txq_ctrl->hairpin_conf.manual_bind) {
			rte_errno = EINVAL;
			DRV_LOG(ERR, "port %u Tx queue %d and peer binding mode"
				" mismatch", dev->data->port_id, cur_queue);
			mlx5_txq_release(dev, cur_queue);
			return -rte_errno;
		}
		sq_attr.state = MLX5_SQC_STATE_RDY;
		sq_attr.sq_state = MLX5_SQC_STATE_RST;
		sq_attr.hairpin_peer_rq = peer_info->qp_id;
		sq_attr.hairpin_peer_vhca = peer_info->vhca_id;
		ret = mlx5_devx_cmd_modify_sq(txq_ctrl->obj->sq, &sq_attr);
		if (ret == 0)
			txq_ctrl->hairpin_status = 1;
		mlx5_txq_release(dev, cur_queue);
	} else {
		struct mlx5_rxq_priv *rxq = mlx5_rxq_get(dev, cur_queue);
		struct mlx5_rxq_ctrl *rxq_ctrl;
		struct mlx5_devx_modify_rq_attr rq_attr = { 0 };

		if (rxq == NULL) {
			rte_errno = EINVAL;
			DRV_LOG(ERR, "Failed to get port %u Rx queue %d",
				dev->data->port_id, cur_queue);
			return -rte_errno;
		}
		rxq_ctrl = rxq->ctrl;
		if (!rxq_ctrl->is_hairpin) {
			rte_errno = EINVAL;
			DRV_LOG(ERR, "port %u queue %d not a hairpin Rxq",
				dev->data->port_id, cur_queue);
			return -rte_errno;
		}
		if (rxq_ctrl->obj == NULL || rxq_ctrl->obj->rq == NULL) {
			rte_errno = ENOMEM;
			DRV_LOG(ERR, "port %u no Rxq object found: %d",
				dev->data->port_id, cur_queue);
			return -rte_errno;
		}
		if (rxq->hairpin_status != 0) {
			DRV_LOG(DEBUG, "port %u Rx queue %d is already bound",
				dev->data->port_id, cur_queue);
			return 0;
		}
		if (peer_info->tx_explicit !=
		    rxq->hairpin_conf.tx_explicit) {
			rte_errno = EINVAL;
			DRV_LOG(ERR, "port %u Rx queue %d and peer Tx rule mode"
				" mismatch", dev->data->port_id, cur_queue);
			return -rte_errno;
		}
		if (peer_info->manual_bind !=
		    rxq->hairpin_conf.manual_bind) {
			rte_errno = EINVAL;
			DRV_LOG(ERR, "port %u Rx queue %d and peer binding mode"
				" mismatch", dev->data->port_id, cur_queue);
			return -rte_errno;
		}
		rq_attr.state = MLX5_RQC_STATE_RDY;
		rq_attr.rq_state = MLX5_RQC_STATE_RST;
		rq_attr.hairpin_peer_sq = peer_info->qp_id;
		rq_attr.hairpin_peer_vhca = peer_info->vhca_id;
		ret = mlx5_devx_cmd_modify_rq(rxq_ctrl->obj->rq, &rq_attr);
		if (ret == 0)
			rxq->hairpin_status = 1;
	}
	return ret;
}

/*
 * Unbind the hairpin queue and reset its HW configuration.
 * This needs to be called twice both for Tx and Rx queues of a pair.
 * If the queue is already unbound, it is considered successful.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param cur_queue
 *   Index of the queue to change the HW configuration to unbind.
 * @param direction
 *   Positive to reset the TxQ, zero to reset the RxQ.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
mlx5_hairpin_queue_peer_unbind(struct rte_eth_dev *dev, uint16_t cur_queue,
			       uint32_t direction)
{
	int ret = 0;

	if (direction != 0) {
		struct mlx5_txq_ctrl *txq_ctrl;
		struct mlx5_devx_modify_sq_attr sq_attr = { 0 };

		txq_ctrl = mlx5_txq_get(dev, cur_queue);
		if (txq_ctrl == NULL) {
			rte_errno = EINVAL;
			DRV_LOG(ERR, "Failed to get port %u Tx queue %d",
				dev->data->port_id, cur_queue);
			return -rte_errno;
		}
		if (!txq_ctrl->is_hairpin) {
			rte_errno = EINVAL;
			DRV_LOG(ERR, "port %u queue %d not a hairpin Txq",
				dev->data->port_id, cur_queue);
			mlx5_txq_release(dev, cur_queue);
			return -rte_errno;
		}
		/* Already unbound, return success before obj checking. */
		if (txq_ctrl->hairpin_status == 0) {
			DRV_LOG(DEBUG, "port %u Tx queue %d is already unbound",
				dev->data->port_id, cur_queue);
			mlx5_txq_release(dev, cur_queue);
			return 0;
		}
		if (!txq_ctrl->obj || !txq_ctrl->obj->sq) {
			rte_errno = ENOMEM;
			DRV_LOG(ERR, "port %u no Txq object found: %d",
				dev->data->port_id, cur_queue);
			mlx5_txq_release(dev, cur_queue);
			return -rte_errno;
		}
		sq_attr.state = MLX5_SQC_STATE_RST;
		sq_attr.sq_state = MLX5_SQC_STATE_RDY;
		ret = mlx5_devx_cmd_modify_sq(txq_ctrl->obj->sq, &sq_attr);
		if (ret == 0)
			txq_ctrl->hairpin_status = 0;
		mlx5_txq_release(dev, cur_queue);
	} else {
		struct mlx5_rxq_priv *rxq = mlx5_rxq_get(dev, cur_queue);
		struct mlx5_rxq_ctrl *rxq_ctrl;
		struct mlx5_devx_modify_rq_attr rq_attr = { 0 };

		if (rxq == NULL) {
			rte_errno = EINVAL;
			DRV_LOG(ERR, "Failed to get port %u Rx queue %d",
				dev->data->port_id, cur_queue);
			return -rte_errno;
		}
		rxq_ctrl = rxq->ctrl;
		if (!rxq_ctrl->is_hairpin) {
			rte_errno = EINVAL;
			DRV_LOG(ERR, "port %u queue %d not a hairpin Rxq",
				dev->data->port_id, cur_queue);
			return -rte_errno;
		}
		if (rxq->hairpin_status == 0) {
			DRV_LOG(DEBUG, "port %u Rx queue %d is already unbound",
				dev->data->port_id, cur_queue);
			return 0;
		}
		if (rxq_ctrl->obj == NULL || rxq_ctrl->obj->rq == NULL) {
			rte_errno = ENOMEM;
			DRV_LOG(ERR, "port %u no Rxq object found: %d",
				dev->data->port_id, cur_queue);
			return -rte_errno;
		}
		rq_attr.state = MLX5_RQC_STATE_RST;
		rq_attr.rq_state = MLX5_RQC_STATE_RDY;
		ret = mlx5_devx_cmd_modify_rq(rxq_ctrl->obj->rq, &rq_attr);
		if (ret == 0)
			rxq->hairpin_status = 0;
	}
	return ret;
}

/*
 * Bind the hairpin port pairs, from the Tx to the peer Rx.
 * This function only supports to bind the Tx to one Rx.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param rx_port
 *   Port identifier of the Rx port.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
static int
mlx5_hairpin_bind_single_port(struct rte_eth_dev *dev, uint16_t rx_port)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	int ret = 0;
	struct mlx5_txq_ctrl *txq_ctrl;
	uint32_t i;
	struct rte_hairpin_peer_info peer = {0xffffff};
	struct rte_hairpin_peer_info cur;
	const struct rte_eth_hairpin_conf *conf;
	uint16_t num_q = 0;
	uint16_t local_port = priv->dev_data->port_id;
	uint32_t manual;
	uint32_t explicit;
	uint16_t rx_queue;

	if (mlx5_eth_find_next(rx_port, dev->device) != rx_port) {
		rte_errno = ENODEV;
		DRV_LOG(ERR, "Rx port %u does not belong to mlx5", rx_port);
		return -rte_errno;
	}
	/*
	 * Before binding TxQ to peer RxQ, first round loop will be used for
	 * checking the queues' configuration consistency. This would be a
	 * little time consuming but better than doing the rollback.
	 */
	for (i = 0; i != priv->txqs_n; i++) {
		txq_ctrl = mlx5_txq_get(dev, i);
		if (txq_ctrl == NULL)
			continue;
		if (!txq_ctrl->is_hairpin) {
			mlx5_txq_release(dev, i);
			continue;
		}
		/*
		 * All hairpin Tx queues of a single port that connected to the
		 * same peer Rx port should have the same "auto binding" and
		 * "implicit Tx flow" modes.
		 * Peer consistency checking will be done in per queue binding.
		 */
		conf = &txq_ctrl->hairpin_conf;
		if (conf->peers[0].port == rx_port) {
			if (num_q == 0) {
				manual = conf->manual_bind;
				explicit = conf->tx_explicit;
			} else {
				if (manual != conf->manual_bind ||
				    explicit != conf->tx_explicit) {
					rte_errno = EINVAL;
					DRV_LOG(ERR, "port %u queue %d mode"
						" mismatch: %u %u, %u %u",
						local_port, i, manual,
						conf->manual_bind, explicit,
						conf->tx_explicit);
					mlx5_txq_release(dev, i);
					return -rte_errno;
				}
			}
			num_q++;
		}
		mlx5_txq_release(dev, i);
	}
	/* Once no queue is configured, success is returned directly. */
	if (num_q == 0)
		return ret;
	/* All the hairpin TX queues need to be traversed again. */
	for (i = 0; i != priv->txqs_n; i++) {
		txq_ctrl = mlx5_txq_get(dev, i);
		if (txq_ctrl == NULL)
			continue;
		if (!txq_ctrl->is_hairpin) {
			mlx5_txq_release(dev, i);
			continue;
		}
		if (txq_ctrl->hairpin_conf.peers[0].port != rx_port) {
			mlx5_txq_release(dev, i);
			continue;
		}
		rx_queue = txq_ctrl->hairpin_conf.peers[0].queue;
		/*
		 * Fetch peer RxQ's information.
		 * No need to pass the information of the current queue.
		 */
		ret = rte_eth_hairpin_queue_peer_update(rx_port, rx_queue,
							NULL, &peer, 1);
		if (ret != 0) {
			mlx5_txq_release(dev, i);
			goto error;
		}
		/* Accessing its own device, inside mlx5 PMD. */
		ret = mlx5_hairpin_queue_peer_bind(dev, i, &peer, 1);
		if (ret != 0) {
			mlx5_txq_release(dev, i);
			goto error;
		}
		/* Pass TxQ's information to peer RxQ and try binding. */
		cur.peer_q = rx_queue;
		cur.qp_id = mlx5_txq_get_sqn(txq_ctrl);
		cur.vhca_id = priv->sh->cdev->config.hca_attr.vhca_id;
		cur.tx_explicit = txq_ctrl->hairpin_conf.tx_explicit;
		cur.manual_bind = txq_ctrl->hairpin_conf.manual_bind;
		/*
		 * In order to access another device in a proper way, RTE level
		 * private function is needed.
		 */
		ret = rte_eth_hairpin_queue_peer_bind(rx_port, rx_queue,
						      &cur, 0);
		if (ret != 0) {
			mlx5_txq_release(dev, i);
			goto error;
		}
		mlx5_txq_release(dev, i);
	}
	return 0;
error:
	/*
	 * Do roll-back process for the queues already bound.
	 * No need to check the return value of the queue unbind function.
	 */
	do {
		/* No validation is needed here. */
		txq_ctrl = mlx5_txq_get(dev, i);
		if (txq_ctrl == NULL)
			continue;
		if (!txq_ctrl->is_hairpin ||
		    txq_ctrl->hairpin_conf.peers[0].port != rx_port) {
			mlx5_txq_release(dev, i);
			continue;
		}
		rx_queue = txq_ctrl->hairpin_conf.peers[0].queue;
		rte_eth_hairpin_queue_peer_unbind(rx_port, rx_queue, 0);
		mlx5_hairpin_queue_peer_unbind(dev, i, 1);
		mlx5_txq_release(dev, i);
	} while (i--);
	return ret;
}

/*
 * Unbind the hairpin port pair, HW configuration of both devices will be clear
 * and status will be reset for all the queues used between them.
 * This function only supports to unbind the Tx from one Rx.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param rx_port
 *   Port identifier of the Rx port.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
static int
mlx5_hairpin_unbind_single_port(struct rte_eth_dev *dev, uint16_t rx_port)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	struct mlx5_txq_ctrl *txq_ctrl;
	uint32_t i;
	int ret;
	uint16_t cur_port = priv->dev_data->port_id;

	if (mlx5_eth_find_next(rx_port, dev->device) != rx_port) {
		rte_errno = ENODEV;
		DRV_LOG(ERR, "Rx port %u does not belong to mlx5", rx_port);
		return -rte_errno;
	}
	for (i = 0; i != priv->txqs_n; i++) {
		uint16_t rx_queue;

		txq_ctrl = mlx5_txq_get(dev, i);
		if (txq_ctrl == NULL)
			continue;
		if (!txq_ctrl->is_hairpin) {
			mlx5_txq_release(dev, i);
			continue;
		}
		if (txq_ctrl->hairpin_conf.peers[0].port != rx_port) {
			mlx5_txq_release(dev, i);
			continue;
		}
		/* Indeed, only the first used queue needs to be checked. */
		if (txq_ctrl->hairpin_conf.manual_bind == 0) {
			mlx5_txq_release(dev, i);
			if (cur_port != rx_port) {
				rte_errno = EINVAL;
				DRV_LOG(ERR, "port %u and port %u are in"
					" auto-bind mode", cur_port, rx_port);
				return -rte_errno;
			} else {
				return 0;
			}
		}
		rx_queue = txq_ctrl->hairpin_conf.peers[0].queue;
		mlx5_txq_release(dev, i);
		ret = rte_eth_hairpin_queue_peer_unbind(rx_port, rx_queue, 0);
		if (ret) {
			DRV_LOG(ERR, "port %u Rx queue %d unbind - failure",
				rx_port, rx_queue);
			return ret;
		}
		ret = mlx5_hairpin_queue_peer_unbind(dev, i, 1);
		if (ret) {
			DRV_LOG(ERR, "port %u Tx queue %d unbind - failure",
				cur_port, i);
			return ret;
		}
	}
	return 0;
}

/*
 * Bind hairpin ports, Rx could be all ports when using RTE_MAX_ETHPORTS.
 * @see mlx5_hairpin_bind_single_port()
 */
int
mlx5_hairpin_bind(struct rte_eth_dev *dev, uint16_t rx_port)
{
	int ret = 0;
	uint16_t p, pp;

	/*
	 * If the Rx port has no hairpin configuration with the current port,
	 * the binding will be skipped in the called function of single port.
	 * Device started status will be checked only before the queue
	 * information updating.
	 */
	if (rx_port == RTE_MAX_ETHPORTS) {
		MLX5_ETH_FOREACH_DEV(p, dev->device) {
			ret = mlx5_hairpin_bind_single_port(dev, p);
			if (ret != 0)
				goto unbind;
		}
		return ret;
	} else {
		return mlx5_hairpin_bind_single_port(dev, rx_port);
	}
unbind:
	MLX5_ETH_FOREACH_DEV(pp, dev->device)
		if (pp < p)
			mlx5_hairpin_unbind_single_port(dev, pp);
	return ret;
}

/*
 * Unbind hairpin ports, Rx could be all ports when using RTE_MAX_ETHPORTS.
 * @see mlx5_hairpin_unbind_single_port()
 */
int
mlx5_hairpin_unbind(struct rte_eth_dev *dev, uint16_t rx_port)
{
	int ret = 0;
	uint16_t p;

	if (rx_port == RTE_MAX_ETHPORTS)
		MLX5_ETH_FOREACH_DEV(p, dev->device) {
			ret = mlx5_hairpin_unbind_single_port(dev, p);
			if (ret != 0)
				return ret;
		}
	else
		ret = mlx5_hairpin_unbind_single_port(dev, rx_port);
	return ret;
}

/*
 * DPDK callback to get the hairpin peer ports list.
 * This will return the actual number of peer ports and save the identifiers
 * into the array (sorted, may be different from that when setting up the
 * hairpin peer queues).
 * The peer port ID could be the same as the port ID of the current device.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param peer_ports
 *   Pointer to array to save the port identifiers.
 * @param len
 *   The length of the array.
 * @param direction
 *   Current port to peer port direction.
 *   positive - current used as Tx to get all peer Rx ports.
 *   zero - current used as Rx to get all peer Tx ports.
 *
 * @return
 *   0 or positive value on success, actual number of peer ports.
 *   a negative errno value otherwise and rte_errno is set.
 */
int
mlx5_hairpin_get_peer_ports(struct rte_eth_dev *dev, uint16_t *peer_ports,
			    size_t len, uint32_t direction)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	struct mlx5_txq_ctrl *txq_ctrl;
	uint32_t i;
	uint16_t pp;
	uint32_t bits[(RTE_MAX_ETHPORTS + 31) / 32] = {0};
	int ret = 0;

	if (direction) {
		for (i = 0; i < priv->txqs_n; i++) {
			txq_ctrl = mlx5_txq_get(dev, i);
			if (!txq_ctrl)
				continue;
			if (!txq_ctrl->is_hairpin) {
				mlx5_txq_release(dev, i);
				continue;
			}
			pp = txq_ctrl->hairpin_conf.peers[0].port;
			if (pp >= RTE_MAX_ETHPORTS) {
				rte_errno = ERANGE;
				mlx5_txq_release(dev, i);
				DRV_LOG(ERR, "port %hu queue %u peer port "
					"out of range %hu",
					priv->dev_data->port_id, i, pp);
				return -rte_errno;
			}
			bits[pp / 32] |= 1 << (pp % 32);
			mlx5_txq_release(dev, i);
		}
	} else {
		for (i = 0; i < priv->rxqs_n; i++) {
			struct mlx5_rxq_priv *rxq = mlx5_rxq_get(dev, i);
			struct mlx5_rxq_ctrl *rxq_ctrl;

			if (rxq == NULL)
				continue;
			rxq_ctrl = rxq->ctrl;
			if (!rxq_ctrl->is_hairpin)
				continue;
			pp = rxq->hairpin_conf.peers[0].port;
			if (pp >= RTE_MAX_ETHPORTS) {
				rte_errno = ERANGE;
				DRV_LOG(ERR, "port %hu queue %u peer port "
					"out of range %hu",
					priv->dev_data->port_id, i, pp);
				return -rte_errno;
			}
			bits[pp / 32] |= 1 << (pp % 32);
		}
	}
	for (i = 0; i < RTE_MAX_ETHPORTS; i++) {
		if (bits[i / 32] & (1 << (i % 32))) {
			if ((size_t)ret >= len) {
				rte_errno = E2BIG;
				return -rte_errno;
			}
			peer_ports[ret++] = i;
		}
	}
	return ret;
}

#ifdef HAVE_MLX5_HWS_SUPPORT

/**
 * Check if starting representor port is allowed.
 *
 * If transfer proxy port is configured for HWS, then starting representor port
 * is allowed if and only if transfer proxy port is started as well.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 *
 * @return
 *   If stopping representor port is allowed, then 0 is returned.
 *   Otherwise rte_errno is set, and negative errno value is returned.
 */
static int
mlx5_hw_representor_port_allowed_start(struct rte_eth_dev *dev)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	struct rte_eth_dev *proxy_dev;
	struct mlx5_priv *proxy_priv;
	uint16_t proxy_port_id = UINT16_MAX;
	int ret;

	MLX5_ASSERT(priv->sh->config.dv_flow_en == 2);
	MLX5_ASSERT(priv->sh->config.dv_esw_en);
	MLX5_ASSERT(priv->representor);
	ret = rte_flow_pick_transfer_proxy(dev->data->port_id, &proxy_port_id, NULL);
	if (ret) {
		if (ret == -ENODEV)
			DRV_LOG(ERR, "Starting representor port %u is not allowed. Transfer "
				     "proxy port is not available.", dev->data->port_id);
		else
			DRV_LOG(ERR, "Failed to pick transfer proxy for port %u (ret = %d)",
				dev->data->port_id, ret);
		return ret;
	}
	proxy_dev = &rte_eth_devices[proxy_port_id];
	proxy_priv = proxy_dev->data->dev_private;
	if (proxy_priv->dr_ctx == NULL) {
		DRV_LOG(DEBUG, "Starting representor port %u is allowed, but default traffic flows"
			       " will not be created. Transfer proxy port must be configured"
			       " for HWS and started.",
			       dev->data->port_id);
		return 0;
	}
	if (!proxy_dev->data->dev_started) {
		DRV_LOG(ERR, "Failed to start port %u: transfer proxy (port %u) must be started",
			     dev->data->port_id, proxy_port_id);
		rte_errno = EAGAIN;
		return -rte_errno;
	}
	if (priv->dr_ctx == NULL) {
		DRV_LOG(ERR, "Failed to start port %u: port must be configured for HWS",
			dev->data->port_id);
		rte_errno = EINVAL;
		return -rte_errno;
	}
	return 0;
}

#endif

/*
 * Allocate TxQs unique umem and register its MR.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
static int mlx5_dev_allocate_consec_tx_mem(struct rte_eth_dev *dev)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	uint8_t log_mu_grp_size = dev->data->mu_sq_log_grp_size;
	size_t alignment;
	uint32_t total_size;
	struct mlx5dv_devx_umem *umem_obj = NULL;
	void *umem_buf = NULL;

	/* Legacy per queue allocation, do nothing here. */
	if (priv->sh->config.txq_mem_algn == 0)
		return 0;
	alignment = (size_t)1 << priv->sh->config.txq_mem_algn;
	total_size = priv->consec_tx_mem.sq_total_size + priv->consec_tx_mem.cq_total_size;
	/*
	 * Hairpin queues can be skipped later
	 * queue size alignment is bigger than doorbell alignment, no need to align or
	 * round-up again. One queue have two DBs (for CQ + WQ).
	 */
	if (log_mu_grp_size == 0) {
		// there are txqs_n SQ DBRs + txqs_n CQ DBRs
		total_size += MLX5_DBR_SIZE * priv->txqs_n * 2;
	}
	else {
		// there are txqs_n SQ DBRs + 1 CQ DBR
		total_size += MLX5_DBR_SIZE * (priv->txqs_n + 1);
	}
	umem_buf = mlx5_malloc_numa_tolerant(MLX5_MEM_RTE | MLX5_MEM_ZERO, total_size,
					     alignment, priv->sh->numa_node);
	if (!umem_buf) {
		DRV_LOG(ERR, "Failed to allocate consecutive memory for TxQs.");
		rte_errno = ENOMEM;
		return -rte_errno;
	}
	umem_obj = mlx5_os_umem_reg(priv->sh->cdev->ctx, (void *)(uintptr_t)umem_buf,
				    total_size, IBV_ACCESS_LOCAL_WRITE);
	if (!umem_obj) {
		DRV_LOG(ERR, "Failed to register unique umem for all SQs.");
		rte_errno = errno;
		if (umem_buf)
			mlx5_free(umem_buf);
		return -rte_errno;
	}
	priv->consec_tx_mem.umem = umem_buf;
	priv->consec_tx_mem.sq_cur_off = 0;
	priv->consec_tx_mem.cq_cur_off = priv->consec_tx_mem.sq_total_size;
	priv->consec_tx_mem.umem_obj = umem_obj;
	DRV_LOG(DEBUG, "Allocated umem %p with size %u for %u queues with sq_len %u,"
		" cq_len %u and registered object %p on port %u",
		umem_buf, total_size, priv->txqs_n, priv->consec_tx_mem.sq_total_size,
		priv->consec_tx_mem.cq_total_size, (void *)umem_obj, dev->data->port_id);
	return 0;
}

/*
 * Release TxQs unique umem and register its MR.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param on_stop
 *   If this is on device stop stage.
 */
static void mlx5_dev_free_consec_tx_mem(struct rte_eth_dev *dev, bool on_stop)
{
	struct mlx5_priv *priv = dev->data->dev_private;

	if (priv->consec_tx_mem.umem_obj) {
		mlx5_os_umem_dereg(priv->consec_tx_mem.umem_obj);
		priv->consec_tx_mem.umem_obj = NULL;
	}
	if (priv->consec_tx_mem.umem) {
		mlx5_free(priv->consec_tx_mem.umem);
		priv->consec_tx_mem.umem = NULL;
	}
	/* Queues information will not be reset. */
	if (on_stop) {
		/* Reset to 0s for re-setting up queues. */
		priv->consec_tx_mem.sq_cur_off = 0;
		priv->consec_tx_mem.cq_cur_off = 0;
	}
}

#define SAVE_RTE_ERRNO_AND_STOP(ret, dev) do {	\
	ret = rte_errno;			\
	(dev)->data->dev_started = 0;		\
} while (0)

/**
 * DPDK callback to start the device.
 *
 * Simulate device start by attaching all configured flows.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 *   The following error values are defined:
 *
 *   - -EAGAIN: If port representor cannot be started,
 *     because transfer proxy port is not started.
 */
int
mlx5_dev_start(struct rte_eth_dev *dev)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	int ret;
	int fine_inline;

	DRV_LOG(DEBUG, "port %u starting device", dev->data->port_id);
#ifdef HAVE_MLX5_HWS_SUPPORT
	if (priv->sh->config.dv_flow_en == 2) {
		struct rte_flow_error error = { 0, };

		/*
		 * If steering is disabled, then:
		 * - There are no limitations regarding port start ordering,
		 *   since no flow rules need to be created as part of port start.
		 * - Non template API initialization will be skipped.
		 */
		if (mlx5_flow_is_steering_disabled())
			goto continue_dev_start;
		/*If previous configuration does not exist. */
		if (!(priv->dr_ctx)) {
			ret = flow_hw_init(dev, &error);
			if (ret) {
				DRV_LOG(ERR, "Failed to start port %u %s: %s",
					dev->data->port_id, dev->data->name,
					error.message);
				return ret;
			}
		}
		/* If there is no E-Switch, then there are no start/stop order limitations. */
		if (!priv->sh->config.dv_esw_en)
			goto continue_dev_start;
		/* If master is being started, then it is always allowed. */
		if (priv->master)
			goto continue_dev_start;
		if (mlx5_hw_representor_port_allowed_start(dev))
			return -rte_errno;
	}
continue_dev_start:
#endif
	fine_inline = rte_mbuf_dynflag_lookup
		(RTE_PMD_MLX5_FINE_GRANULARITY_INLINE, NULL);
	if (fine_inline >= 0)
		rte_net_mlx5_dynf_inline_mask = RTE_BIT64(fine_inline);
	else
		rte_net_mlx5_dynf_inline_mask = 0;
	if (dev->data->nb_rx_queues > 0) {
		uint32_t max_lro_msg_size = priv->max_lro_msg_size;

		if (max_lro_msg_size < MLX5_LRO_SEG_CHUNK_SIZE) {
			uint32_t i;
			struct mlx5_rxq_priv *rxq;

			for (i = 0; i != priv->rxqs_n; ++i) {
				rxq = mlx5_rxq_get(dev, i);
				if (rxq && rxq->ctrl && rxq->ctrl->rxq.lro) {
					DRV_LOG(ERR, "port %u invalid max LRO size",
						dev->data->port_id);
					rte_errno = EINVAL;
					return -rte_errno;
				}
			}
		}
		ret = mlx5_dev_configure_rss_reta(dev);
		if (ret) {
			DRV_LOG(ERR, "port %u reta config failed: %s",
				dev->data->port_id, strerror(rte_errno));
			return -rte_errno;
		}
	}
	ret = mlx5_txpp_start(dev);
	if (ret) {
		DRV_LOG(ERR, "port %u Tx packet pacing init failed: %s",
			dev->data->port_id, strerror(rte_errno));
		SAVE_RTE_ERRNO_AND_STOP(ret, dev);
		goto error;
	}
	if (mlx5_devx_obj_ops_en(priv->sh) &&
	    priv->obj_ops.lb_dummy_queue_create) {
		ret = priv->obj_ops.lb_dummy_queue_create(dev);
		if (ret) {
			SAVE_RTE_ERRNO_AND_STOP(ret, dev);
			goto txpp_stop;
		}
	}
	ret = mlx5_dev_allocate_consec_tx_mem(dev);
	if (ret) {
		DRV_LOG(ERR, "port %u Tx queues memory allocation failed: %s",
			dev->data->port_id, strerror(rte_errno));
		SAVE_RTE_ERRNO_AND_STOP(ret, dev);
		goto lb_dummy_queue_release;
	}

	dev->data->mu_sq_log_grp_size = priv->config.mu_sq_log_grp_size;
	ret = mlx5_txq_start(dev);
	if (ret) {
		DRV_LOG(ERR, "port %u Tx queue allocation failed: %s",
			dev->data->port_id, strerror(rte_errno));
		SAVE_RTE_ERRNO_AND_STOP(ret, dev);
		goto free_consec_tx_mem;
	}
	if (priv->config.std_delay_drop || priv->config.hp_delay_drop) {
		if (!priv->sh->dev_cap.vf && !priv->sh->dev_cap.sf &&
		    !priv->representor) {
			ret = mlx5_get_flag_dropless_rq(dev);
			if (ret < 0)
				DRV_LOG(WARNING,
					"port %u cannot query dropless flag",
					dev->data->port_id);
			else if (!ret)
				DRV_LOG(WARNING,
					"port %u dropless_rq OFF, no rearming",
					dev->data->port_id);
		} else {
			DRV_LOG(DEBUG,
				"port %u doesn't support dropless_rq flag",
				dev->data->port_id);
		}
	}

/*
	if (mlx5_doca_flow_init(dev, "vnf") == 0) {
		uint16_t port_id = dev->data->port_id;

		doca_ports[port_id] = mlx5_create_doca_flow_port(dev);
		if (doca_ports[port_id] != NULL) {
			if (mlx5_create_doca_esp_spi_pipe(doca_ports[port_id],
							  &doca_esp_spi_pipes[port_id]) != 0 ||
			    mlx5_add_esp_spi_entries(doca_ports[port_id],
						     doca_esp_spi_pipes[port_id],
						     priv->rxqs_n) != 0) {
				mlx5_doca_flow_teardown(dev);
				goto rxq_start;
			}
			if (mlx5_create_doca_root_basic_pipe(doca_ports[port_id],
							     doca_esp_spi_pipes[port_id],
							     &doca_root_pipes[port_id]) != 0 ||
			    mlx5_add_doca_root_entry(doca_ports[port_id],
						     doca_root_pipes[port_id]) != 0)
				mlx5_doca_flow_teardown(dev);
		}
	}
*/

	eth_rxq_regular_receive("mlx5_1", true);


	ret = mlx5_rxq_start(dev);
	if (ret) {
		DRV_LOG(ERR, "port %u Rx queue allocation failed: %s",
			dev->data->port_id, strerror(rte_errno));
		SAVE_RTE_ERRNO_AND_STOP(ret, dev);
		goto txq_stop;
	}
	/*
	 * Such step will be skipped if there is no hairpin TX queue configured
	 * with RX peer queue from the same device.
	 */
	ret = mlx5_hairpin_auto_bind(dev);
	if (ret) {
		DRV_LOG(ERR, "port %u hairpin auto binding failed: %s",
			dev->data->port_id, strerror(rte_errno));
		SAVE_RTE_ERRNO_AND_STOP(ret, dev);
		goto rxq_stop;
	}
	/* Set started flag here for the following steps like control flow. */
	dev->data->dev_started = 1;
	ret = mlx5_rx_intr_vec_enable(dev);
	if (ret) {
		DRV_LOG(ERR, "port %u Rx interrupt vector creation failed",
			dev->data->port_id);
		SAVE_RTE_ERRNO_AND_STOP(ret, dev);
		goto rxq_stop;
	}
	mlx5_os_stats_init(dev);
	/*
	 * Attach indirection table objects detached on port stop.
	 * They may be needed to create RSS in non-isolated mode.
	 */
	ret = mlx5_action_handle_attach(dev);
	if (ret) {
		DRV_LOG(ERR,
			"port %u failed to attach indirect actions: %s",
			dev->data->port_id, rte_strerror(rte_errno));
		SAVE_RTE_ERRNO_AND_STOP(ret, dev);
		goto rx_intr_vec_disable;
	}
#ifdef HAVE_MLX5_HWS_SUPPORT
	if (priv->sh->config.dv_flow_en == 2) {
		ret = flow_hw_table_update(dev, NULL);
		if (ret) {
			DRV_LOG(ERR, "port %u failed to update HWS tables",
				dev->data->port_id);
			SAVE_RTE_ERRNO_AND_STOP(ret, dev);
			goto action_handle_detach;
		}
	}
#endif
	ret = mlx5_traffic_enable(dev);
	if (ret) {
		DRV_LOG(ERR, "port %u failed to set defaults flows",
			dev->data->port_id);
		SAVE_RTE_ERRNO_AND_STOP(ret, dev);
		goto action_handle_detach;
	}
	/* Set dynamic fields and flags into Rx queues. */
	mlx5_flow_rxq_dynf_set(dev);
	/* Set flags and context to convert Rx timestamps. */
	mlx5_rxq_timestamp_set(dev);
	/* Set a mask and offset of scheduling on timestamp into Tx queues. */
	mlx5_txq_dynf_timestamp_set(dev);
	/*
	 * In non-cached mode, it only needs to start the default mreg copy
	 * action and no flow created by application exists anymore.
	 * But it is worth wrapping the interface for further usage.
	 */
	ret = mlx5_flow_start_default(dev);
	if (ret) {
		DRV_LOG(DEBUG, "port %u failed to start default actions: %s",
			dev->data->port_id, strerror(rte_errno));
		SAVE_RTE_ERRNO_AND_STOP(ret, dev);
		goto traffic_disable;
	}
	if (mlx5_dev_ctx_shared_mempool_subscribe(dev) != 0) {
		DRV_LOG(ERR, "port %u failed to subscribe for mempool life cycle: %s",
			dev->data->port_id, rte_strerror(rte_errno));
		SAVE_RTE_ERRNO_AND_STOP(ret, dev);
		goto stop_default;
	}
	if (mlx5_flow_is_steering_disabled())
		mlx5_flow_rxq_mark_flag_set(dev);
	rte_wmb();
	dev->tx_pkt_burst = mlx5_select_tx_function(dev);
	dev->rx_pkt_burst = mlx5_select_rx_function(dev);
	/* Enable datapath on secondary process. */
	mlx5_mp_os_req_start_rxtx(dev);
	if (rte_intr_fd_get(priv->sh->intr_handle) >= 0) {
		priv->sh->port[priv->dev_port - 1].ih_port_id =
					(uint32_t)dev->data->port_id;
	} else {
		DRV_LOG(INFO, "port %u starts without RMV interrupts.",
			dev->data->port_id);
		dev->data->dev_conf.intr_conf.rmv = 0;
	}
	if (rte_intr_fd_get(priv->sh->intr_handle_nl) >= 0) {
		priv->sh->port[priv->dev_port - 1].nl_ih_port_id =
					(uint32_t)dev->data->port_id;
	} else {
		DRV_LOG(INFO, "port %u starts without LSC interrupts.",
			dev->data->port_id);
		dev->data->dev_conf.intr_conf.lsc = 0;
	}
	if (rte_intr_fd_get(priv->sh->intr_handle_devx) >= 0)
		priv->sh->port[priv->dev_port - 1].devx_ih_port_id =
					(uint32_t)dev->data->port_id;
	return 0;
stop_default:
	mlx5_flow_stop_default(dev);
traffic_disable:
	mlx5_traffic_disable(dev);
action_handle_detach:
	mlx5_action_handle_detach(dev);
rx_intr_vec_disable:
	mlx5_rx_intr_vec_disable(dev);
rxq_stop:
	mlx5_rxq_stop(dev);
txq_stop:
	mlx5_doca_flow_teardown(dev);
	mlx5_txq_stop(dev);
free_consec_tx_mem:
	mlx5_dev_free_consec_tx_mem(dev, false);
lb_dummy_queue_release:
	if (priv->obj_ops.lb_dummy_queue_release)
		priv->obj_ops.lb_dummy_queue_release(dev);
txpp_stop:
	mlx5_txpp_stop(dev);
error:
	rte_errno = ret;
	return -rte_errno;
}

#ifdef HAVE_MLX5_HWS_SUPPORT
/**
 * Check if stopping transfer proxy port is allowed.
 *
 * If transfer proxy port is configured for HWS, then it is allowed to stop it
 * if and only if all other representor ports are stopped.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 *
 * @return
 *   If stopping transfer proxy port is allowed, then 0 is returned.
 *   Otherwise rte_errno is set, and negative errno value is returned.
 */
static int
mlx5_hw_proxy_port_allowed_stop(struct rte_eth_dev *dev)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	bool representor_started = false;
	uint16_t port_id;

	MLX5_ASSERT(priv->sh->config.dv_flow_en == 2);
	MLX5_ASSERT(priv->sh->config.dv_esw_en);
	MLX5_ASSERT(priv->master);
	/* If transfer proxy port was not configured for HWS, then stopping it is allowed. */
	if (!priv->dr_ctx)
		return 0;
	MLX5_ETH_FOREACH_DEV(port_id, dev->device) {
		const struct rte_eth_dev *port_dev = &rte_eth_devices[port_id];
		const struct mlx5_priv *port_priv = port_dev->data->dev_private;

		if (port_id != dev->data->port_id &&
		    port_priv->domain_id == priv->domain_id &&
		    port_dev->data->dev_started)
			representor_started = true;
	}
	if (representor_started) {
		DRV_LOG(ERR, "Failed to stop port %u: attached representor ports"
			     " must be stopped before stopping transfer proxy port",
			     dev->data->port_id);
		rte_errno = EBUSY;
		return -rte_errno;
	}
	return 0;
}
#endif

/**
 * DPDK callback to stop the device.
 *
 * Simulate device stop by detaching all configured flows.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 *   The following error values are defined:
 *
 *   - -EBUSY: If transfer proxy port cannot be stopped,
 *     because other port representors are still running.
 */
int
mlx5_dev_stop(struct rte_eth_dev *dev)
{
	struct mlx5_priv *priv = dev->data->dev_private;

#ifdef HAVE_MLX5_HWS_SUPPORT
	if (priv->sh->config.dv_flow_en == 2) {
		/*
		 * If steering is disabled,
		 * then there are no limitations regarding port stop ordering,
		 * since no flow rules need to be destroyed as part of port stop.
		 */
		if (mlx5_flow_is_steering_disabled())
			goto continue_dev_stop;
		/* If there is no E-Switch, then there are no start/stop order limitations. */
		if (!priv->sh->config.dv_esw_en)
			goto continue_dev_stop;
		/* If representor is being stopped, then it is always allowed. */
		if (priv->representor)
			goto continue_dev_stop;
		if (mlx5_hw_proxy_port_allowed_stop(dev)) {
			dev->data->dev_started = 1;
			return -rte_errno;
		}
	}
continue_dev_stop:
#endif
	dev->data->dev_started = 0;
	/* Prevent crashes when queues are still in use. */
	dev->rx_pkt_burst = rte_eth_pkt_burst_dummy;
	dev->tx_pkt_burst = rte_eth_pkt_burst_dummy;
	rte_wmb();
	/* Disable datapath on secondary process. */
	mlx5_mp_os_req_stop_rxtx(dev);
	rte_delay_us_sleep(1000 * priv->rxqs_n);
	DRV_LOG(DEBUG, "port %u stopping device", dev->data->port_id);
	if (mlx5_flow_is_steering_disabled())
		mlx5_flow_rxq_flags_clear(dev);
	mlx5_flow_stop_default(dev);
	/* Control flows for default traffic can be removed firstly. */
	mlx5_traffic_disable(dev);
	mlx5_doca_flow_teardown(dev);
	/* All RX queue flags will be cleared in the flush interface. */
	mlx5_flow_list_flush(dev, MLX5_FLOW_TYPE_GEN, true);
	mlx5_flow_meter_rxq_flush(dev);
	mlx5_action_handle_detach(dev);
#ifdef HAVE_MLX5_HWS_SUPPORT
	mlx5_flow_hw_cleanup_ctrl_rx_templates(dev);
#endif
	mlx5_rx_intr_vec_disable(dev);
	priv->sh->port[priv->dev_port - 1].ih_port_id = RTE_MAX_ETHPORTS;
	priv->sh->port[priv->dev_port - 1].devx_ih_port_id = RTE_MAX_ETHPORTS;
	priv->sh->port[priv->dev_port - 1].nl_ih_port_id = RTE_MAX_ETHPORTS;
	mlx5_txq_stop(dev);
	mlx5_rxq_stop(dev);
	mlx5_dev_free_consec_tx_mem(dev, true);
	if (priv->obj_ops.lb_dummy_queue_release)
		priv->obj_ops.lb_dummy_queue_release(dev);
	mlx5_txpp_stop(dev);

	return 0;
}

#ifdef HAVE_MLX5_HWS_SUPPORT

static int
mlx5_traffic_enable_hws(struct rte_eth_dev *dev)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	struct mlx5_sh_config *config = &priv->sh->config;
	uint64_t flags = 0;
	unsigned int i;
	int ret = 0;

	for (i = 0; i < priv->txqs_n; ++i) {
		struct mlx5_txq_ctrl *txq = mlx5_txq_get(dev, i);
		uint32_t queue;

		if (!txq)
			continue;
		queue = mlx5_txq_get_sqn(txq);
		if ((priv->representor || priv->master) &&
		    config->dv_esw_en &&
		    config->fdb_def_rule) {
			if (mlx5_flow_hw_esw_create_sq_miss_flow(dev, queue, false)) {
				mlx5_txq_release(dev, i);
				goto error;
			}
		}
		if (config->dv_esw_en) {
			if (mlx5_flow_hw_create_tx_repr_matching_flow(dev, queue, false)) {
				mlx5_txq_release(dev, i);
				goto error;
			}
		}
		if (mlx5_vport_tx_metadata_passing_enabled(priv->sh)) {
			if (mlx5_flow_hw_create_nic_tx_default_mreg_copy_flow(dev, queue)) {
				mlx5_txq_release(dev, i);
				goto error;
			}
		}
		mlx5_txq_release(dev, i);
	}
	if (config->fdb_def_rule) {
		if ((priv->master || priv->representor) && config->dv_esw_en) {
			if (!mlx5_flow_hw_esw_create_default_jump_flow(dev))
				priv->fdb_def_rule = 1;
			else
				goto error;
		}
	} else {
		DRV_LOG(INFO, "port %u FDB default rule is disabled", dev->data->port_id);
	}
	if (!priv->sh->config.lacp_by_user && priv->pf_bond >= 0 && priv->master)
		if (mlx5_flow_hw_lacp_rx_flow(dev))
			goto error;
	if (priv->isolated)
		return 0;
	ret = mlx5_flow_hw_create_ctrl_rx_tables(dev);
	if (ret) {
		DRV_LOG(ERR, "Failed to set up Rx control flow templates for port %u, %d",
			dev->data->port_id, -ret);
		goto error;
	}
	if (dev->data->promiscuous)
		flags |= MLX5_CTRL_PROMISCUOUS;
	if (dev->data->all_multicast)
		flags |= MLX5_CTRL_ALL_MULTICAST;
	else
		flags |= MLX5_CTRL_BROADCAST | MLX5_CTRL_IPV4_MULTICAST | MLX5_CTRL_IPV6_MULTICAST;
	flags |= MLX5_CTRL_DMAC;
	if (priv->vlan_filter_n)
		flags |= MLX5_CTRL_VLAN_FILTER;
	return mlx5_flow_hw_ctrl_flows(dev, flags);
error:
	ret = rte_errno;
	mlx5_flow_hw_flush_ctrl_flows(dev);
	mlx5_flow_hw_cleanup_ctrl_rx_tables(dev);
	rte_errno = ret;
	return -rte_errno;
}

#endif

/**
 * Enable traffic flows configured by control plane
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
mlx5_traffic_enable(struct rte_eth_dev *dev)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	struct rte_flow_item_eth bcast = {
		.hdr.dst_addr.addr_bytes = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
	};
	struct rte_flow_item_eth ipv6_multi_spec = {
		.hdr.dst_addr.addr_bytes = { 0x33, 0x33, 0x00, 0x00, 0x00, 0x00 },
	};
	struct rte_flow_item_eth ipv6_multi_mask = {
		.hdr.dst_addr.addr_bytes = { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
	};
	struct rte_flow_item_eth unicast = {
		.hdr.src_addr.addr_bytes = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	};
	struct rte_flow_item_eth unicast_mask = {
		.hdr.dst_addr.addr_bytes = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
	};
	const unsigned int vlan_filter_n = priv->vlan_filter_n;
	const struct rte_ether_addr cmp = {
		.addr_bytes = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	};
	unsigned int i;
	unsigned int j;
	int ret;

	if (mlx5_flow_is_steering_disabled())
		return 0;

#ifdef HAVE_MLX5_HWS_SUPPORT
	if (priv->sh->config.dv_flow_en == 2)
		return mlx5_traffic_enable_hws(dev);
#endif
	/*
	 * Hairpin txq default flow should be created no matter if it is
	 * isolation mode. Or else all the packets to be sent will be sent
	 * out directly without the TX flow actions, e.g. encapsulation.
	 */
	for (i = 0; i != priv->txqs_n; ++i) {
		struct mlx5_txq_ctrl *txq_ctrl = mlx5_txq_get(dev, i);
		if (!txq_ctrl)
			continue;
		/* Only Tx implicit mode requires the default Tx flow. */
		if (txq_ctrl->is_hairpin &&
		    txq_ctrl->hairpin_conf.tx_explicit == 0 &&
		    txq_ctrl->hairpin_conf.peers[0].port ==
		    priv->dev_data->port_id) {
			ret = mlx5_ctrl_flow_source_queue(dev,
					mlx5_txq_get_sqn(txq_ctrl));
			if (ret) {
				mlx5_txq_release(dev, i);
				goto error;
			}
		}
		if (priv->sh->config.dv_esw_en) {
			uint32_t q = mlx5_txq_get_sqn(txq_ctrl);

			if (mlx5_flow_create_devx_sq_miss_flow(dev, q) == 0) {
				mlx5_txq_release(dev, i);
				DRV_LOG(ERR,
					"Port %u Tx queue %u SQ create representor devx default miss rule failed.",
					dev->data->port_id, i);
				goto error;
			}
		}
		mlx5_txq_release(dev, i);
	}
	if (priv->sh->config.fdb_def_rule) {
		if (priv->sh->config.dv_esw_en) {
			if (mlx5_flow_create_esw_table_zero_flow(dev))
				priv->fdb_def_rule = 1;
			else
				DRV_LOG(INFO, "port %u FDB default rule cannot be configured - only Eswitch group 0 flows are supported.",
					dev->data->port_id);
		}
	} else {
		DRV_LOG(INFO, "port %u FDB default rule is disabled",
			dev->data->port_id);
	}
	if (!priv->sh->config.lacp_by_user && priv->pf_bond >= 0 && priv->master) {
		ret = mlx5_flow_lacp_miss(dev);
		if (ret)
			DRV_LOG(INFO, "port %u LACP rule cannot be created - "
				"forward LACP to kernel.", dev->data->port_id);
		else
			DRV_LOG(INFO, "LACP traffic will be missed in port %u.",
				dev->data->port_id);
	}
	if (priv->isolated)
		return 0;
	if (dev->data->promiscuous) {
		struct rte_flow_item_eth promisc = {
			.hdr.dst_addr.addr_bytes = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
			.hdr.src_addr.addr_bytes = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
			.hdr.ether_type = 0,
		};

		ret = mlx5_ctrl_flow(dev, &promisc, &promisc);
		if (ret)
			goto error;
	}
	if (dev->data->all_multicast) {
		struct rte_flow_item_eth multicast = {
			.hdr.dst_addr.addr_bytes = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 },
			.hdr.src_addr.addr_bytes = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
			.hdr.ether_type = 0,
		};

		ret = mlx5_ctrl_flow(dev, &multicast, &multicast);
		if (ret)
			goto error;
	} else {
		/* Add broadcast/multicast flows. */
		for (i = 0; i != vlan_filter_n; ++i) {
			uint16_t vlan = priv->vlan_filter[i];

			struct rte_flow_item_vlan vlan_spec = {
				.hdr.vlan_tci = rte_cpu_to_be_16(vlan),
			};
			struct rte_flow_item_vlan vlan_mask =
				rte_flow_item_vlan_mask;

			ret = mlx5_ctrl_flow_vlan(dev, &bcast, &bcast,
						  &vlan_spec, &vlan_mask);
			if (ret)
				goto error;
			ret = mlx5_ctrl_flow_vlan(dev, &ipv6_multi_spec,
						  &ipv6_multi_mask,
						  &vlan_spec, &vlan_mask);
			if (ret)
				goto error;
		}
		if (!vlan_filter_n) {
			ret = mlx5_ctrl_flow(dev, &bcast, &bcast);
			if (ret)
				goto error;
			ret = mlx5_ctrl_flow(dev, &ipv6_multi_spec,
					     &ipv6_multi_mask);
			if (ret) {
				/* Do not fail on IPv6 broadcast creation failure. */
				DRV_LOG(WARNING,
					"IPv6 broadcast is not supported");
				ret = 0;
			}
		}
	}
	/* Add MAC address flows. */
	for (i = 0; i != MLX5_MAX_MAC_ADDRESSES; ++i) {
		struct rte_ether_addr *mac = &dev->data->mac_addrs[i];

		/* Add flows for unicast and multicast mac addresses added by API. */
		if (!memcmp(mac, &cmp, sizeof(*mac)) ||
		    !BITFIELD_ISSET(priv->mac_own, i) ||
		    (dev->data->all_multicast && rte_is_multicast_ether_addr(mac)))
			continue;
		memcpy(&unicast.hdr.dst_addr.addr_bytes,
		       mac->addr_bytes,
		       RTE_ETHER_ADDR_LEN);
		for (j = 0; j != vlan_filter_n; ++j) {
			uint16_t vlan = priv->vlan_filter[j];

			struct rte_flow_item_vlan vlan_spec = {
				.hdr.vlan_tci = rte_cpu_to_be_16(vlan),
			};
			struct rte_flow_item_vlan vlan_mask =
				rte_flow_item_vlan_mask;

			ret = mlx5_ctrl_flow_vlan(dev, &unicast,
						  &unicast_mask,
						  &vlan_spec,
						  &vlan_mask);
			if (ret)
				goto error;
		}
		if (!vlan_filter_n) {
			ret = mlx5_ctrl_flow(dev, &unicast, &unicast_mask);
			if (ret)
				goto error;
		}
	}
	return 0;
error:
	ret = rte_errno; /* Save rte_errno before cleanup. */
	mlx5_traffic_disable_legacy(dev);
	rte_errno = ret; /* Restore rte_errno. */
	return -rte_errno;
}

static void
mlx5_traffic_disable_legacy(struct rte_eth_dev *dev)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	struct mlx5_ctrl_flow_entry *entry;
	struct mlx5_ctrl_flow_entry *tmp;

	/*
	 * Free registered control flow rules first,
	 * to free the memory allocated for list entries
	 */
	entry = LIST_FIRST(&priv->hw_ctrl_flows);
	while (entry != NULL) {
		tmp = LIST_NEXT(entry, next);
		mlx5_legacy_ctrl_flow_destroy(dev, entry);
		entry = tmp;
	}

	mlx5_flow_list_flush(dev, MLX5_FLOW_TYPE_CTL, false);
}

/**
 * Disable traffic flows configured by control plane
 *
 * @param dev
 *   Pointer to Ethernet device private data.
 */
void
mlx5_traffic_disable(struct rte_eth_dev *dev)
{
	if (mlx5_flow_is_steering_disabled())
		return;

#ifdef HAVE_MLX5_HWS_SUPPORT
	struct mlx5_priv *priv = dev->data->dev_private;

	if (priv->sh->config.dv_flow_en == 2) {
		/* Device started flag was cleared before, this is used to derefer the Rx queues. */
		priv->hws_rule_flushing = true;
		mlx5_flow_hw_flush_ctrl_flows(dev);
		mlx5_flow_hw_cleanup_ctrl_rx_tables(dev);
		priv->hws_rule_flushing = false;
	}
	else
#endif
		mlx5_traffic_disable_legacy(dev);
}

/**
 * Restart traffic flows configured by control plane
 *
 * @param dev
 *   Pointer to Ethernet device private data.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
mlx5_traffic_restart(struct rte_eth_dev *dev)
{
	if (mlx5_flow_is_steering_disabled())
		return 0;

	if (dev->data->dev_started) {
		mlx5_traffic_disable(dev);
#ifdef HAVE_MLX5_HWS_SUPPORT
		mlx5_flow_hw_cleanup_ctrl_rx_templates(dev);
#endif
		return mlx5_traffic_enable(dev);
	}
	return 0;
}

static bool
mac_flows_update_needed(struct rte_eth_dev *dev)
{
	struct mlx5_priv *priv = dev->data->dev_private;

	if (mlx5_flow_is_steering_disabled())
		return false;
	if (!dev->data->dev_started)
		return false;
	if (dev->data->promiscuous)
		return false;
	if (priv->isolated)
		return false;

	return true;
}

static int
traffic_dmac_create(struct rte_eth_dev *dev, const struct rte_ether_addr *addr)
{
	struct mlx5_priv *priv = dev->data->dev_private;

	if (priv->sh->config.dv_flow_en == 2)
		return mlx5_flow_hw_ctrl_flow_dmac(dev, addr);
	else
		return mlx5_legacy_dmac_flow_create(dev, addr);
}

static int
traffic_dmac_destroy(struct rte_eth_dev *dev, const struct rte_ether_addr *addr)
{
	struct mlx5_priv *priv = dev->data->dev_private;

	if (priv->sh->config.dv_flow_en == 2)
		return mlx5_flow_hw_ctrl_flow_dmac_destroy(dev, addr);
	else
		return mlx5_legacy_dmac_flow_destroy(dev, addr);
}

static int
traffic_dmac_vlan_create(struct rte_eth_dev *dev,
			 const struct rte_ether_addr *addr,
			 const uint16_t vid)
{
	struct mlx5_priv *priv = dev->data->dev_private;

	if (priv->sh->config.dv_flow_en == 2)
		return mlx5_flow_hw_ctrl_flow_dmac_vlan(dev, addr, vid);
	else
		return mlx5_legacy_dmac_vlan_flow_create(dev, addr, vid);
}

static int
traffic_dmac_vlan_destroy(struct rte_eth_dev *dev,
			 const struct rte_ether_addr *addr,
			 const uint16_t vid)
{
	struct mlx5_priv *priv = dev->data->dev_private;

	if (priv->sh->config.dv_flow_en == 2)
		return mlx5_flow_hw_ctrl_flow_dmac_vlan_destroy(dev, addr, vid);
	else
		return mlx5_legacy_dmac_vlan_flow_destroy(dev, addr, vid);
}

/**
 * Adjust Rx control flow rules to allow traffic on provided MAC address.
 */
int
mlx5_traffic_mac_add(struct rte_eth_dev *dev, const struct rte_ether_addr *addr)
{
	struct mlx5_priv *priv = dev->data->dev_private;

	if (!mac_flows_update_needed(dev))
		return 0;

	if (priv->vlan_filter_n > 0) {
		unsigned int i;

		for (i = 0; i < priv->vlan_filter_n; ++i) {
			uint16_t vlan = priv->vlan_filter[i];
			int ret;

			if (mlx5_ctrl_flow_uc_dmac_vlan_exists(dev, addr, vlan))
				continue;

			ret = traffic_dmac_vlan_create(dev, addr, vlan);
			if (ret != 0)
				return ret;
		}

		return 0;
	}

	if (mlx5_ctrl_flow_uc_dmac_exists(dev, addr))
		return 0;

	return traffic_dmac_create(dev, addr);
}

/**
 * Adjust Rx control flow rules to disallow traffic with removed MAC address.
 */
int
mlx5_traffic_mac_remove(struct rte_eth_dev *dev, const struct rte_ether_addr *addr)
{
	struct mlx5_priv *priv = dev->data->dev_private;

	if (!mac_flows_update_needed(dev))
		return 0;

	if (priv->vlan_filter_n > 0) {
		unsigned int i;

		for (i = 0; i < priv->vlan_filter_n; ++i) {
			uint16_t vlan = priv->vlan_filter[i];
			int ret;

			if (!mlx5_ctrl_flow_uc_dmac_vlan_exists(dev, addr, vlan))
				continue;

			ret = traffic_dmac_vlan_destroy(dev, addr, vlan);
			if (ret != 0)
				return ret;
		}

		return 0;
	}

	if (!mlx5_ctrl_flow_uc_dmac_exists(dev, addr))
		return 0;

	return traffic_dmac_destroy(dev, addr);
}

/**
 * Adjust Rx control flow rules to allow traffic on provided VLAN.
 *
 * Assumptions:
 * - Called when VLAN is added.
 * - At least one VLAN is enabled before function call.
 *
 * This functions assumes that VLAN is new and was not included in
 * Rx control flow rules set up before calling it.
 */
int
mlx5_traffic_vlan_add(struct rte_eth_dev *dev, const uint16_t vid)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	unsigned int i;
	int ret;

	if (!mac_flows_update_needed(dev))
		return 0;

	/* Add all unicast DMAC flow rules with new VLAN attached. */
	for (i = 0; i != MLX5_MAX_MAC_ADDRESSES; ++i) {
		struct rte_ether_addr *mac = &dev->data->mac_addrs[i];

		if (rte_is_zero_ether_addr(mac))
			continue;

		ret = traffic_dmac_vlan_create(dev, mac, vid);
		if (ret != 0)
			return ret;
	}

	if (priv->vlan_filter_n == 1) {
		/*
		 * Adding first VLAN. Need to remove unicast DMAC rules before adding new rules.
		 * Removing after creating VLAN rules so that traffic "gap" is not introduced.
		 */

		for (i = 0; i != MLX5_MAX_MAC_ADDRESSES; ++i) {
			struct rte_ether_addr *mac = &dev->data->mac_addrs[i];

			if (rte_is_zero_ether_addr(mac))
				continue;

			ret = traffic_dmac_destroy(dev, mac);
			if (ret != 0)
				return ret;
		}
	}

	return 0;
}

/**
 * Adjust Rx control flow rules to disallow traffic with removed VLAN.
 *
 * Assumptions:
 *
 * - VLAN was really removed.
 */
int
mlx5_traffic_vlan_remove(struct rte_eth_dev *dev, const uint16_t vid)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	unsigned int i;
	int ret;

	if (!mac_flows_update_needed(dev))
		return 0;

	if (priv->vlan_filter_n == 0) {
		/*
		 * If there are no VLANs as a result, unicast DMAC flow rules must be recreated.
		 * Recreating first to ensure no traffic "gap".
		 */

		for (i = 0; i != MLX5_MAX_MAC_ADDRESSES; ++i) {
			struct rte_ether_addr *mac = &dev->data->mac_addrs[i];

			if (rte_is_zero_ether_addr(mac))
				continue;

			ret = traffic_dmac_create(dev, mac);
			if (ret != 0)
				return ret;
		}
	}

	/* Remove all unicast DMAC flow rules with this VLAN. */
	for (i = 0; i != MLX5_MAX_MAC_ADDRESSES; ++i) {
		struct rte_ether_addr *mac = &dev->data->mac_addrs[i];

		if (rte_is_zero_ether_addr(mac))
			continue;

		ret = traffic_dmac_vlan_destroy(dev, mac, vid);
		if (ret != 0)
			return ret;
	}

	return 0;
}

