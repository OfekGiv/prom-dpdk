/*
 * Copyright (c) 2025 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright notice, this list of
 *       conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice, this list of
 *       conditions and the following disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the NVIDIA CORPORATION nor the names of its contributors may be used
 *       to endorse or promote products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NVIDIA CORPORATION BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TOR (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <time.h>
#include <unistd.h>
#include <signal.h>

#include <doca_flow.h>
#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_eth_rxq.h>
#include <doca_eth_rxq_cpu_data_path.h>
#include <doca_error.h>
#include <doca_log.h>
#include <doca_dpdk.h>

#include "common.h"
#include "eth_common.h"
#include "eth_flow_common.h"

#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_errno.h>
#include <eal_export.h>
#include "rte_pmd_mlx5.h"


DOCA_LOG_REGISTER(ETH_RXQ_REGULAR_RECEIVE);

#define SLEEP_IN_NANOS (10 * 1000)  /* sample the task every 10 microseconds  */
#define MAX_BURST_SIZE 256	    /* Max burst size to set for eth_rxq */
#define MAX_PKT_SIZE 1600	    /* Max packet size to set for eth_rxq */
#define BUFS_NUM 1		    /* Number of DOCA buffers */
#define TASKS_NUM 1		    /* Tasks number */
#define RECV_TASK_USER_DATA 0x43210 /* User data for receive task */

static volatile sig_atomic_t g_stop_capture = 0;
static struct eth_flow_common_resources g_flow_resources[RTE_MAX_ETHPORTS];
static bool g_flow_port_active[RTE_MAX_ETHPORTS];
static uint16_t g_flow_ports_count;

struct eth_rxq_sample_objects {
	struct eth_core_resources core_resources;	 /* A struct to hold ETH core resources */
	struct eth_flow_common_resources flow_resources; /* A struct to hold flow resources */
	struct doca_eth_rxq *eth_rxq;			 /* DOCA ETH RXQ context */
	struct doca_buf *packet_buf;			 /* DOCA buffer to contain received packet */
	struct doca_eth_rxq_task_recv *recv_task;	 /* Receive task */
	uint32_t inflight_tasks;			 /* Inflight tasks count */
	uint16_t rxq_queue_id;				 /* DOCA ETH RXQ's queue ID */
	bool timestamp_enable;				 /* timestamp enable */
};

struct doca_dev *doca_devs[RTE_MAX_ETHPORTS];
bool doca_devs_owned[RTE_MAX_ETHPORTS];
bool doca_devs_bridge_mapped[RTE_MAX_ETHPORTS];

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_mlx5_doca_bridge_port_prepare, 25.11)
int
rte_pmd_mlx5_doca_bridge_port_prepare(uint16_t port_id)
{
	doca_error_t status;
	struct doca_dev *dev = NULL;

	if (port_id >= RTE_MAX_ETHPORTS) {
		rte_errno = EINVAL;
		return -rte_errno;
	}
	status = doca_dpdk_port_as_dev(port_id, &dev);
	if (status != DOCA_SUCCESS || dev == NULL) {
		rte_errno = ENODEV;
		return -rte_errno;
	}
	doca_devs[port_id] = dev;
	doca_devs_owned[port_id] = false;
	doca_devs_bridge_mapped[port_id] = true;
	return 0;
}

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_mlx5_doca_bridge_probe_pci, 25.11)
int
rte_pmd_mlx5_doca_bridge_probe_pci(const char *pci_addr,
				    const char *probe_devargs,
				    uint16_t *port_id)
{
	struct doca_dev *dev = NULL;
	doca_error_t status;
	uint16_t resolved_port_id;

	if (pci_addr == NULL || pci_addr[0] == '\0') {
		rte_errno = EINVAL;
		return -rte_errno;
	}
	status = open_doca_device_with_pci(pci_addr, NULL, &dev);
	if (status != DOCA_SUCCESS || dev == NULL) {
		rte_errno = ENODEV;
		return -rte_errno;
	}
	status = doca_dpdk_port_probe(dev, probe_devargs != NULL ? probe_devargs : "");
	if (status != DOCA_SUCCESS) {
		(void)doca_dev_close(dev);
		rte_errno = EIO;
		return -rte_errno;
	}
	status = doca_dpdk_get_first_port_id(dev, &resolved_port_id);
	if (status != DOCA_SUCCESS || resolved_port_id >= RTE_MAX_ETHPORTS) {
		(void)doca_dev_close(dev);
		rte_errno = EIO;
		return -rte_errno;
	}
	if (doca_devs[resolved_port_id] != NULL) {
		(void)doca_dev_close(dev);
		rte_errno = EEXIST;
		return -rte_errno;
	}
	doca_devs[resolved_port_id] = dev;
	/*
	 * Bridge-probed ports should be treated like bridge-associated devices,
	 * i.e. not manually closed by mlx5 stop path.
	 */
	doca_devs_owned[resolved_port_id] = false;
	doca_devs_bridge_mapped[resolved_port_id] = true;
	if (port_id != NULL)
		*port_id = resolved_port_id;
	return 0;
}

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

static void print_esp_sn(struct doca_buf *pkt)
{
    void *data;
    size_t data_len;
    struct rte_ether_hdr *eth;
    struct rte_ipv4_hdr *ip4;
    uint8_t *esp;
	uint32_t esp_spi;
    uint32_t esp_sn;
    uint16_t ether_type;
    size_t l2_len = sizeof(struct rte_ether_hdr);
    size_t ip_len;
    size_t need_len;

    if (doca_buf_get_data(pkt, &data) != DOCA_SUCCESS ||
        doca_buf_get_data_len(pkt, &data_len) != DOCA_SUCCESS)
        return;

    if (data_len < l2_len + sizeof(struct rte_ipv4_hdr))
        return;

    eth = data;
    ether_type = rte_be_to_cpu_16(eth->ether_type);
    if (ether_type != RTE_ETHER_TYPE_IPV4)
        return;

    ip4 = (struct rte_ipv4_hdr *)((uint8_t *)data + l2_len);
    if (ip4->next_proto_id != IPPROTO_ESP)
        return;

    ip_len = (ip4->version_ihl & RTE_IPV4_HDR_IHL_MASK) * RTE_IPV4_IHL_MULTIPLIER;
    need_len = l2_len + ip_len + 8; /* ESP header starts with SPI(4) + SN(4) */
    if (data_len < need_len)
        return;

    esp = (uint8_t *)data + l2_len + ip_len;
	memcpy(&esp_spi, esp, sizeof(esp_spi));
    memcpy(&esp_sn, esp + 4, sizeof(esp_sn)); /* skip SPI, read SN */
	esp_spi = rte_be_to_cpu_32(esp_spi);
    esp_sn = rte_be_to_cpu_32(esp_sn);

	DOCA_LOG_INFO("ESP SPI: %u", esp_spi);
    DOCA_LOG_INFO("ESP sequence number: %u", esp_sn);
}

/*
 * ETH RXQ receive task common callback
 *
 * @task_recv [in]: Completed task
 * @task_user_data [in]: User provided data, used for identifying the task
 * @ctx_user_data [in]: User provided data, used to store sample state
 */
static void task_recv_common_cb(struct doca_eth_rxq_task_recv *task_recv,
				union doca_data task_user_data,
				union doca_data ctx_user_data)
{
	doca_error_t status, task_status;
	struct eth_rxq_sample_objects *state;
	struct doca_buf *pkt;
	size_t packet_size;
	uint32_t rx_hash;
	const uint32_t *metadata_array;
	uint64_t timestamp;

	state = ctx_user_data.ptr;
	state->inflight_tasks--;
	DOCA_LOG_INFO("Receive task user data is 0x%lx", task_user_data.u64);

	status = doca_eth_rxq_task_recv_get_pkt(task_recv, &pkt);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get packet of a receive task, err: %s", doca_error_get_name(status));
		doca_task_free(doca_eth_rxq_task_recv_as_doca_task(task_recv));
		return;
	}

	task_status = doca_task_get_status(doca_eth_rxq_task_recv_as_doca_task(task_recv));

	if (task_status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to receive a packet, err: %s", doca_error_get_name(task_status));
	} else {
		status = doca_eth_rxq_task_recv_get_metadata_array(task_recv, &metadata_array);
		if (status != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to get metadata_array, err: %s", doca_error_get_name(status));
		else
			DOCA_LOG_INFO("Received a packet with metadata %u", metadata_array[0]);

		status = doca_eth_rxq_task_recv_get_rx_hash(task_recv, &rx_hash);
		if (status != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to get rx_hash, err: %s", doca_error_get_name(status));
		else
			DOCA_LOG_INFO("Received a packet with rx_hash %u", rx_hash);

		if (state->timestamp_enable) {
			status = doca_eth_rxq_task_recv_get_timestamp(task_recv, &timestamp);
			if (status != DOCA_SUCCESS)
				DOCA_LOG_ERR("Failed to get timestamp, err: %s", doca_error_get_name(status));
			else
				DOCA_LOG_INFO("Received a packet with timestamp %lu", timestamp);
		}

		DOCA_LOG_INFO("Packet forwarded to RX queue %u", state->rxq_queue_id);

		status = doca_buf_get_data_len(pkt, &packet_size);
		if (status != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to get receive packet size, err: %s", doca_error_get_name(status));
		else
			DOCA_LOG_INFO("Received a packet of size %lu successfully", packet_size);

		print_esp_sn(pkt);
	}

	status = doca_buf_dec_refcount(pkt, NULL);
	if (status != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to free packet buf, err: %s", doca_error_get_name(status));

	doca_task_free(doca_eth_rxq_task_recv_as_doca_task(task_recv));
}

/*
 * Destroy ETH RXQ context related resources
 *
 * @state [in]: eth_rxq_sample_objects struct to destroy its ETH RXQ context
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
static doca_error_t destroy_eth_rxq_ctx(struct eth_rxq_sample_objects *state)
{
	doca_error_t status;
	enum doca_ctx_states ctx_state;
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	status = doca_ctx_stop(state->core_resources.core_objs.ctx);
	if (status == DOCA_ERROR_IN_PROGRESS) {
		while (state->inflight_tasks != 0) {
			(void)doca_pe_progress(state->core_resources.core_objs.pe);
			nanosleep(&ts, &ts);
		}

		status = doca_ctx_get_state(state->core_resources.core_objs.ctx, &ctx_state);
		if (status != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed get status of context, err: %s", doca_error_get_name(status));
			return status;
		}

		status = ctx_state == DOCA_CTX_STATE_IDLE ? DOCA_SUCCESS : DOCA_ERROR_BAD_STATE;
	}

	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to stop DOCA context, err: %s", doca_error_get_name(status));
		return status;
	}

	status = doca_eth_rxq_destroy(state->eth_rxq);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA ETH RXQ context, err: %s", doca_error_get_name(status));
		return status;
	}

	return DOCA_SUCCESS;
}

/*
 * Destroy DOCA buffers for the packets
 *
 * @state [in]: eth_rxq_sample_objects struct to destroy its packet DOCA buffers
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
static doca_error_t destroy_eth_rxq_packet_buffers(struct eth_rxq_sample_objects *state)
{
	doca_error_t status;

	status = doca_buf_dec_refcount(state->packet_buf, NULL);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy packet_buf buffer, err: %s", doca_error_get_name(status));
		return status;
	}

	return DOCA_SUCCESS;
}

/*
 * Submit ETH RXQ tasks
 *
 * @state [in/out]: eth_rxq_sample_objects struct to submit its tasks
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
static doca_error_t submit_eth_rxq_tasks(struct eth_rxq_sample_objects *state)
{
	doca_error_t status;

	status = doca_task_submit(doca_eth_rxq_task_recv_as_doca_task(state->recv_task));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit receive task, err: %s", doca_error_get_name(status));
		return status;
	}

	state->inflight_tasks++;

	return DOCA_SUCCESS;
}

/*
 * Destroy ETH RXQ tasks
 *
 * @state [in]: eth_rxq_sample_objects struct to destroy its tasks
 */
static void destroy_eth_rxq_tasks(struct eth_rxq_sample_objects *state)
{
	doca_task_free(doca_eth_rxq_task_recv_as_doca_task(state->recv_task));
}



/*
 * Create ETH RXQ context related resources
 *
 * @state [in/out]: eth_rxq_sample_objects struct to create its ETH RXQ context
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
static doca_error_t create_eth_rxq_ctx(struct eth_rxq_sample_objects *state)
{
	doca_error_t status, clean_status;
	union doca_data user_data;

	status = doca_eth_rxq_create(state->core_resources.core_objs.dev,
				     MAX_BURST_SIZE,
				     MAX_PKT_SIZE,
				     &(state->eth_rxq));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ETH RXQ context, err: %s", doca_error_get_name(status));
		return status;
	}

	status = doca_eth_rxq_set_type(state->eth_rxq, DOCA_ETH_RXQ_TYPE_REGULAR);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set type, err: %s", doca_error_get_name(status));
		goto destroy_eth_rxq;
	}

	status = doca_eth_rxq_task_recv_set_conf(state->eth_rxq, task_recv_common_cb, task_recv_common_cb, TASKS_NUM);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set receive task configuration, err: %s", doca_error_get_name(status));
		goto destroy_eth_rxq;
	}

	status = doca_eth_rxq_set_metadata_num(state->eth_rxq, 1);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to enable metadata, err: %s", doca_error_get_name(status));
		goto destroy_eth_rxq;
	}

	status = doca_eth_rxq_set_rx_hash(state->eth_rxq, 1);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to enable rx_hash, err: %s", doca_error_get_name(status));
		goto destroy_eth_rxq;
	}

	status = doca_eth_rxq_set_timestamp(state->eth_rxq, state->timestamp_enable);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set enable timestamp, err: %s", doca_error_get_name(status));
		goto destroy_eth_rxq;
	}

	state->core_resources.core_objs.ctx = doca_eth_rxq_as_doca_ctx(state->eth_rxq);
	if (state->core_resources.core_objs.ctx == NULL) {
		DOCA_LOG_ERR("Failed to retrieve DOCA ETH RXQ context as DOCA context, err: %s",
			     doca_error_get_name(status));
		goto destroy_eth_rxq;
	}

	status = doca_pe_connect_ctx(state->core_resources.core_objs.pe, state->core_resources.core_objs.ctx);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to connect PE, err: %s", doca_error_get_name(status));
		goto destroy_eth_rxq;
	}

	user_data.ptr = state;
	status = doca_ctx_set_user_data(state->core_resources.core_objs.ctx, user_data);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set user data for DOCA context, err: %s", doca_error_get_name(status));
		goto destroy_eth_rxq;
	}

	status = doca_ctx_start(state->core_resources.core_objs.ctx);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA context, err: %s", doca_error_get_name(status));
		goto destroy_eth_rxq;
	}

	status = doca_eth_rxq_apply_queue_id(state->eth_rxq, 0);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to apply queue ID of RXQ, err: %s", doca_error_get_name(status));
		goto stop_ctx;
	}
	state->rxq_queue_id = 0;

	return DOCA_SUCCESS;
stop_ctx:
	clean_status = doca_ctx_stop(state->core_resources.core_objs.ctx);
	state->core_resources.core_objs.ctx = NULL;

	if (clean_status != DOCA_SUCCESS)
		return status;
destroy_eth_rxq:
	clean_status = doca_eth_rxq_destroy(state->eth_rxq);
	state->eth_rxq = NULL;

	if (clean_status != DOCA_SUCCESS)
		return status;

	return status;
}



/*
 * Create ETH RXQ tasks
 *
 * @state [in/out]: eth_rxq_sample_objects struct to create tasks with its ETH RXQ context
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
static doca_error_t create_eth_rxq_tasks(struct eth_rxq_sample_objects *state)
{
	doca_error_t status;
	union doca_data user_data;

	user_data.u64 = RECV_TASK_USER_DATA;
	status =
		doca_eth_rxq_task_recv_allocate_init(state->eth_rxq, state->packet_buf, user_data, &(state->recv_task));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate receive task, err: %s", doca_error_get_name(status));
		return status;
	}

	return DOCA_SUCCESS;
}

/*
 * Create DOCA buffers for the packet
 *
 * @state [in/out]: eth_rxq_sample_objects struct to create its packet DOCA buffers
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
static doca_error_t create_eth_rxq_packet_buffer(struct eth_rxq_sample_objects *state)
{
	doca_error_t status;

	status = doca_buf_inventory_buf_get_by_addr(state->core_resources.core_objs.buf_inv,
						    state->core_resources.core_objs.src_mmap,
						    state->core_resources.mmap_addr,
						    MAX_PKT_SIZE,
						    &(state->packet_buf));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA buffer for ethernet frame, err: %s", doca_error_get_name(status));
		return status;
	}

	return DOCA_SUCCESS;
}

/*
 * Clean sample resources
 *
 * @state [in]: eth_rxq_sample_objects struct to clean
 */
static void eth_rxq_cleanup(struct eth_rxq_sample_objects *state)
{
	doca_error_t status;

	if (state->flow_resources.root_pipe != NULL)
		eth_flow_common_destroy_flow_pipe(&(state->flow_resources));

	if (state->flow_resources.df_port != NULL) {
		status = eth_flow_common_destroy_flow_port(&(state->flow_resources));
		if (status != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy flow port, err: %s", doca_error_get_name(status));
			return;
		}
	}

	eth_flow_common_cleanup_flow();

	if (state->eth_rxq != NULL) {
		status = destroy_eth_rxq_ctx(state);
		if (status != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy eth_rxq_ctx, err: %s", doca_error_get_name(status));
			return;
		}
	}

	if (state->core_resources.core_objs.dev != NULL) {
		status = destroy_eth_core_resources(&(state->core_resources));
		if (status != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy core_resources, err: %s", doca_error_get_name(status));
			return;
		}
	}
}

/*
 * Check if device supports needed capabilities
 *
 * @devinfo [in]: Device info for device to check
 * @return: DOCA_SUCCESS in case the device supports needed capabilities and DOCA_ERROR otherwise
 */
static doca_error_t check_device(struct doca_devinfo *devinfo)
{
	doca_error_t status;
	uint32_t max_supported_burst_size;
	uint32_t max_supported_packet_size;

	status = doca_eth_rxq_cap_get_max_burst_size(devinfo, &max_supported_burst_size);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get supported max burst size, err: %s", doca_error_get_name(status));
		return status;
	}

	if (max_supported_burst_size < MAX_BURST_SIZE)
		return DOCA_ERROR_NOT_SUPPORTED;

	status = doca_eth_rxq_cap_get_max_packet_size(devinfo, &max_supported_packet_size);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get supported max packet size, err: %s", doca_error_get_name(status));
		return status;
	}

	if (max_supported_packet_size < MAX_PKT_SIZE)
		return DOCA_ERROR_NOT_SUPPORTED;

	status =
		doca_eth_rxq_cap_is_type_supported(devinfo, DOCA_ETH_RXQ_TYPE_REGULAR, DOCA_ETH_RXQ_DATA_PATH_TYPE_CPU);
	if (status != DOCA_SUCCESS && status != DOCA_ERROR_NOT_SUPPORTED) {
		DOCA_LOG_ERR("Failed to check supported type, err: %s", doca_error_get_name(status));
		return status;
	}

	return status;
}

/*
 * Retrieve ETH RXQ tasks
 *
 * @state [in]: eth_rxq_sample_objects struct to retrieve tasks from
 */
static doca_error_t retrieve_rxq_recv_tasks(struct eth_rxq_sample_objects *state)
{
    doca_error_t status;
    struct timespec ts = {
        .tv_sec = 0,
        .tv_nsec = SLEEP_IN_NANOS,
    };

    while (!g_stop_capture) {
        (void)doca_pe_progress(state->core_resources.core_objs.pe);

        if (state->inflight_tasks == 0) {
            status = create_eth_rxq_packet_buffer(state);
            if (status != DOCA_SUCCESS) {
                DOCA_LOG_ERR("Failed to create packet buffer, err: %s", doca_error_get_name(status));
                return status;
            }

            status = create_eth_rxq_tasks(state);
            if (status != DOCA_SUCCESS) {
                DOCA_LOG_ERR("Failed to create receive task, err: %s", doca_error_get_name(status));
                (void)destroy_eth_rxq_packet_buffers(state);
                return status;
            }

            status = submit_eth_rxq_tasks(state);
            if (status != DOCA_SUCCESS) {
                DOCA_LOG_ERR("Failed to submit receive task, err: %s", doca_error_get_name(status));
                destroy_eth_rxq_tasks(state);
                (void)destroy_eth_rxq_packet_buffers(state);
                return status;
            }
        }

        nanosleep(&ts, &ts);
    }

    /* Drain any task that is already in flight before exit */
    while (state->inflight_tasks != 0) {
        (void)doca_pe_progress(state->core_resources.core_objs.pe);
        nanosleep(&ts, &ts);
    }

    return DOCA_SUCCESS;
}

doca_error_t rxq_doca_start(uint16_t dpdk_port_id, uint16_t nb_queues,
			   struct doca_dev *dev, bool bridge_mapped)
{
	doca_error_t result = DOCA_SUCCESS;
	struct doca_log_backend *sdk_log;
	doca_error_t status = DOCA_SUCCESS;
	struct eth_flow_common_config flow_cfg = {};
	bool flow_initialized = false;
	struct eth_flow_common_resources *resources;

	uint16_t *rss_queues = NULL;
	uint16_t port_id = dpdk_port_id;

	if (port_id >= RTE_MAX_ETHPORTS)
		return DOCA_ERROR_INVALID_VALUE;

	resources = &g_flow_resources[port_id];
	if (g_flow_port_active[port_id])
		return DOCA_SUCCESS;

	/* Register a logger backend */
	result = doca_log_backend_create_standard();
	if (result != DOCA_SUCCESS)
		goto rxq_cleanup;

	/* Register a logger backend for internal SDK errors and warnings */
	result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
	if (result != DOCA_SUCCESS)
		goto rxq_cleanup;
	result = doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_INFO);
	if (result != DOCA_SUCCESS)
		goto rxq_cleanup;

	if (dev == NULL) {
		DOCA_LOG_ERR("Invalid DOCA device for DPDK port %u", dpdk_port_id);
		status = DOCA_ERROR_INVALID_VALUE;
		goto rxq_cleanup;
	}
	if (!bridge_mapped) {
		DOCA_LOG_WARN("DPDK port %u is not associated with the DOCA-DPDK bridge; skipping DOCA RSS forwarding setup",
			      dpdk_port_id);
		return DOCA_ERROR_NOT_SUPPORTED;
	}

	status = eth_flow_common_init_flow(nb_queues);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init flow, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}
	flow_initialized = true;
	flow_cfg.dev = dev;

	status = eth_flow_common_create_flow_port(flow_cfg.dev, dpdk_port_id, resources);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create flow port, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}

	if (nb_queues == 0) {
		DOCA_LOG_ERR("Invalid nb_queues=0");
		status = DOCA_ERROR_INVALID_VALUE;
		goto rxq_cleanup;
	}

	rss_queues = calloc(nb_queues, sizeof(*rss_queues));
	if (!rss_queues) {
		DOCA_LOG_ERR("Failed to allocate memory for RSS queues");
		status = DOCA_ERROR_NO_MEMORY;
		goto rxq_cleanup;
	}
	for (uint16_t i = 0; i < nb_queues; i++)
		rss_queues[i] = i;

	flow_cfg.rxq_queue_ids = rss_queues;
	flow_cfg.nb_queues = nb_queues;

	status = eth_flow_common_create_flow_pipe(&flow_cfg, resources);
	if (status == DOCA_ERROR_NOT_FOUND) {
		/* Some bridge setups expose logical RXQ IDs starting at 1. Retry once. */
		for (uint16_t i = 0; i < nb_queues; i++)
			rss_queues[i] = i + 1;
		DOCA_LOG_INFO("Retrying flow pipe creation with RSS queue IDs base=1");
		status = eth_flow_common_create_flow_pipe(&flow_cfg, resources);
	}
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create flow pipe, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}

	g_flow_port_active[port_id] = true;
	g_flow_ports_count++;
/*
	status = create_eth_rxq_packet_buffer(&state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create packer buffer, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}

	status = create_eth_rxq_tasks(&state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create tasks, err: %s", doca_error_get_name(status));
		goto destroy_packet_buffers;
	}

	status = submit_eth_rxq_tasks(&state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit tasks, err: %s", doca_error_get_name(status));
		goto destroy_rxq_tasks;
	}

	status = retrieve_rxq_recv_tasks(&state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Receive loop failed, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}
*/
	return DOCA_SUCCESS;

//destroy_rxq_tasks:
//	destroy_eth_rxq_tasks(&state);
//destroy_packet_buffers:
//	clean_status = destroy_eth_rxq_packet_buffers(&state);
//	if (clean_status != DOCA_SUCCESS)
//		return status;
rxq_cleanup:
	DOCA_LOG_INFO("Finished");
	if (resources->root_pipe != NULL)
		eth_flow_common_destroy_flow_pipe(resources);
	if (resources->df_port != NULL)
		(void)eth_flow_common_destroy_flow_port(resources);
	if (flow_initialized)
		eth_flow_common_cleanup_flow();
	free(rss_queues);

	return status;
}

void rxq_doca_stop(uint16_t dpdk_port_id)
{
	struct eth_flow_common_resources *resources;

	if (dpdk_port_id >= RTE_MAX_ETHPORTS)
		return;
	if (!g_flow_port_active[dpdk_port_id])
		return;

	resources = &g_flow_resources[dpdk_port_id];
	eth_flow_common_destroy_flow_pipe(resources);
	(void)eth_flow_common_destroy_flow_port(resources);
	g_flow_port_active[dpdk_port_id] = false;
	if (g_flow_ports_count > 0)
		g_flow_ports_count--;
	if (g_flow_ports_count == 0)
		eth_flow_common_cleanup_flow();
}


/*
 * Run ETH RXQ regular mode receive
 *
 * @ib_dev_name [in]: IB device name of a doca device
 * @timestamp_enable [in]: timestamp enable
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
/*
doca_error_t eth_rxq_regular_receive(const char *ib_dev_name, bool timestamp_enable, uint16_t nb_queues)
{
	doca_error_t result = DOCA_SUCCESS;
	struct doca_log_backend *sdk_log;
	doca_error_t status, clean_status;
	struct eth_rxq_sample_objects state = {.timestamp_enable = timestamp_enable};
	struct eth_core_config cfg = {.mmap_size = MAX_PKT_SIZE * BUFS_NUM,
				      .inventory_num_elements = BUFS_NUM,
				      .check_device = check_device,
				      .ibdev_name = ib_dev_name};
	struct eth_flow_common_config flow_cfg = {};
	uint16_t *rss_queues = NULL;

	signal(SIGINT, handle_stop_signal);
	signal(SIGTERM, handle_stop_signal);

	result = doca_log_backend_create_standard();
	if (result != DOCA_SUCCESS)
		goto rxq_cleanup;

	result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
	if (result != DOCA_SUCCESS)
		goto rxq_cleanup;

	result = doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_INFO);
	if (result != DOCA_SUCCESS)
		goto rxq_cleanup;

	status = allocate_eth_core_resources(&cfg, &(state.core_resources));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed allocate core resources, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}

	status = create_eth_rxq_ctx(&state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create/start ETH RXQ context, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}

	status = eth_flow_common_init_flow(nb_queues);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init flow, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}

	flow_cfg.dev = state.core_resources.core_objs.dev;

	status = eth_flow_common_create_flow_port(flow_cfg.dev, 0, &(state.flow_resources));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create flow port, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}

	if (nb_queues == 0) {
		DOCA_LOG_ERR("Invalid nb_queues=0");
		status = DOCA_ERROR_INVALID_VALUE;
		goto rxq_cleanup;
	}

	if (nb_queues > 1) {
		DOCA_LOG_WARN("Only RX queue 0 is created in this sample; forcing RSS queue count from %u to 1",
			      nb_queues);
		nb_queues = 1;
	}

	rss_queues = calloc(nb_queues, sizeof(*rss_queues));
	if (!rss_queues) {
		DOCA_LOG_ERR("Failed to allocate memory for RSS queues");
		status = DOCA_ERROR_NO_MEMORY;
		goto rxq_cleanup;
	}
	for (uint16_t i = 0; i < nb_queues; i++)
		rss_queues[i] = i;

	flow_cfg.rxq_queue_ids = rss_queues;
	flow_cfg.nb_queues = nb_queues;

	status = eth_flow_common_create_flow_pipe(&flow_cfg, &(state.flow_resources));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create flow pipe, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}

	status = create_eth_rxq_packet_buffer(&state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create packer buffer, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}

	status = create_eth_rxq_tasks(&state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create tasks, err: %s", doca_error_get_name(status));
		goto destroy_packet_buffers;
	}

	status = submit_eth_rxq_tasks(&state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit tasks, err: %s", doca_error_get_name(status));
		goto destroy_rxq_tasks;
	}

	status = retrieve_rxq_recv_tasks(&state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Receive loop failed, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}

	goto rxq_cleanup;

destroy_rxq_tasks:
	destroy_eth_rxq_tasks(&state);
destroy_packet_buffers:
	clean_status = destroy_eth_rxq_packet_buffers(&state);
	if (clean_status != DOCA_SUCCESS)
		return status;
rxq_cleanup:
	DOCA_LOG_INFO("Finished");
	eth_rxq_cleanup(&state);
	free(rss_queues);

	return status;
}
*/
