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

#include <doca_bitfield.h>
#include <doca_flow.h>
#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_eth_rxq.h>
#include <doca_eth_rxq_cpu_data_path.h>
#include <doca_error.h>
#include <doca_log.h>

#include "common.h"
#include "eth_common.h"
#include "eth_flow_common.h"
#include "eth_rxq_regular_receive_sample.h"

#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_memcpy.h>
#include <rte_mempool.h>

DOCA_LOG_REGISTER(ETH_RXQ_REGULAR_RECEIVE);

#define SLEEP_IN_NANOS (10 * 1000)  /* sample the task every 10 microseconds  */
#define MAX_BURST_SIZE 256	    /* Max burst size to set for eth_rxq */
#define MAX_PKT_SIZE 1600	    /* Max packet size to set for eth_rxq */
#define BUFS_NUM 1		    /* Number of DOCA buffers */
#define TASKS_NUM 1		    /* Tasks number */
#define RECV_TASK_USER_DATA 0x43210 /* User data for receive task */

static volatile sig_atomic_t g_stop_capture = 0;

/* Shared flow state for the LSB-demux pipes, owned by eth_rxq_install_lsb_demux_flow. */
static struct eth_flow_common_resources g_demux_flow_resources;
static struct doca_flow_pipe *g_demux_root_pipe;
static struct doca_flow_pipe_entry *g_demux_root_entry;
static struct doca_flow_pipe *g_demux_pipe;
static struct doca_flow_pipe_entry *g_demux_entries[2];
static bool g_demux_flow_inited;

static void handle_stop_signal(int signo)
{
    (void)signo;
    g_stop_capture = 1;
}

struct eth_rxq_sample_objects {
	struct eth_core_resources core_resources;	 /* A struct to hold ETH core resources */
	struct eth_flow_common_resources flow_resources; /* A struct to hold flow resources */
	struct doca_eth_rxq *eth_rxq;			 /* DOCA ETH RXQ context */
	struct doca_buf *packet_buf;			 /* DOCA buffer to contain received packet */
	struct doca_eth_rxq_task_recv *recv_task;	 /* Receive task */
	uint32_t inflight_tasks;			 /* Inflight tasks count */
	uint16_t rxq_queue_id;				 /* DOCA ETH RXQ's queue ID */
	bool timestamp_enable;				 /* timestamp enable */
	struct rte_mempool *mp;				 /* Mempool for delivering received pkts to DPDK */
	struct rte_mbuf *pending[MAX_BURST_SIZE];	 /* Mbufs filled by callback, drained by poll */
	uint16_t pending_count;
	uint16_t *rss_queues;				 /* Owned by handle when opened via eth_rxq_open */
};

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
	(void)task_user_data;

	status = doca_eth_rxq_task_recv_get_pkt(task_recv, &pkt);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get packet of a receive task, err: %s", doca_error_get_name(status));
		doca_task_free(doca_eth_rxq_task_recv_as_doca_task(task_recv));
		return;
	}

	task_status = doca_task_get_status(doca_eth_rxq_task_recv_as_doca_task(task_recv));

	if (task_status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to receive a packet, err: %s", doca_error_get_name(task_status));
	} else if (state->mp != NULL) {
		void *pkt_data = NULL;
		size_t pkt_len = 0;

		if (doca_buf_get_data(pkt, &pkt_data) == DOCA_SUCCESS &&
		    doca_buf_get_data_len(pkt, &pkt_len) == DOCA_SUCCESS &&
		    pkt_len > 0 && state->pending_count < MAX_BURST_SIZE) {
			struct rte_mbuf *m = rte_pktmbuf_alloc(state->mp);

			if (m != NULL) {
				char *dst = rte_pktmbuf_append(m, (uint16_t)pkt_len);

				if (dst != NULL) {
					rte_memcpy(dst, pkt_data, pkt_len);
					state->pending[state->pending_count++] = m;
				} else {
					rte_pktmbuf_free(m);
				}
			}
		}
	} else {
		DOCA_LOG_INFO("Receive task user data is 0x%lx", task_user_data.u64);
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

	status = doca_eth_rxq_apply_queue_id(state->eth_rxq, state->rxq_queue_id);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to apply queue ID of RXQ, err: %s", doca_error_get_name(status));
		goto stop_ctx;
	}

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

/*
 * Run ETH RXQ regular mode receive
 *
 * @ib_dev_name [in]: IB device name of a doca device
 * @timestamp_enable [in]: timestamp enable
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
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

	status = eth_flow_common_init_flow();
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

/*
 * Non-blocking init for use as a DPDK Rx burst backend.
 * Creates one DOCA ETH RXQ context bound to flow queue id `queue_idx`.
 * Caller is responsible for installing flow steering separately
 * (e.g. via eth_rxq_install_lsb_demux_flow once both handles are open).
 */
doca_error_t eth_rxq_open(struct eth_rxq_sample_objects **out_handle,
			  const char *ib_dev_name,
			  bool timestamp_enable,
			  uint16_t queue_idx,
			  struct rte_mempool *mp)
{
	struct eth_rxq_sample_objects *state;
	struct doca_log_backend *sdk_log;
	struct eth_core_config cfg;
	doca_error_t status;

	if (out_handle == NULL || mp == NULL)
		return DOCA_ERROR_INVALID_VALUE;

	state = calloc(1, sizeof(*state));
	if (state == NULL)
		return DOCA_ERROR_NO_MEMORY;

	state->timestamp_enable = timestamp_enable;
	state->mp = mp;
	state->rxq_queue_id = queue_idx;

	memset(&cfg, 0, sizeof(cfg));
	cfg.mmap_size = MAX_PKT_SIZE * BUFS_NUM;
	cfg.inventory_num_elements = BUFS_NUM;
	cfg.check_device = check_device;
	cfg.ibdev_name = ib_dev_name;

	/* Best-effort log backend init; ignore "already exists" failures. */
	(void)doca_log_backend_create_standard();
	if (doca_log_backend_create_with_file_sdk(stderr, &sdk_log) == DOCA_SUCCESS)
		(void)doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_INFO);

	status = allocate_eth_core_resources(&cfg, &state->core_resources);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed allocate core resources, err: %s", doca_error_get_name(status));
		goto fail;
	}

	status = create_eth_rxq_ctx(state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create/start ETH RXQ context, err: %s", doca_error_get_name(status));
		goto fail;
	}

	status = create_eth_rxq_packet_buffer(state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create packet buffer, err: %s", doca_error_get_name(status));
		goto fail;
	}

	status = create_eth_rxq_tasks(state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create receive task, err: %s", doca_error_get_name(status));
		(void)destroy_eth_rxq_packet_buffers(state);
		goto fail;
	}

	status = submit_eth_rxq_tasks(state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit receive task, err: %s", doca_error_get_name(status));
		destroy_eth_rxq_tasks(state);
		(void)destroy_eth_rxq_packet_buffers(state);
		goto fail;
	}

	*out_handle = state;
	return DOCA_SUCCESS;

fail:
	eth_rxq_cleanup(state);
	free(state->rss_queues);
	free(state);
	*out_handle = NULL;
	return status;
}

/*
 * Drive doca_pe_progress() once, drain any callback-deposited mbufs, resubmit
 * a fresh receive task, and return packets to the caller. Designed to be the
 * implementation behind dev->rx_pkt_burst.
 */
uint16_t eth_rxq_poll(struct eth_rxq_sample_objects *state,
		      struct rte_mbuf **mbufs,
		      uint16_t nb_pkts)
{
	uint16_t out;

	if (state == NULL || nb_pkts == 0)
		return 0;

	(void)doca_pe_progress(state->core_resources.core_objs.pe);

	if (state->inflight_tasks == 0) {
		if (create_eth_rxq_packet_buffer(state) != DOCA_SUCCESS)
			goto drain;
		if (create_eth_rxq_tasks(state) != DOCA_SUCCESS) {
			(void)destroy_eth_rxq_packet_buffers(state);
			goto drain;
		}
		if (submit_eth_rxq_tasks(state) != DOCA_SUCCESS) {
			destroy_eth_rxq_tasks(state);
			(void)destroy_eth_rxq_packet_buffers(state);
			goto drain;
		}
	}

drain:
	out = state->pending_count < nb_pkts ? state->pending_count : nb_pkts;
	for (uint16_t i = 0; i < out; i++)
		mbufs[i] = state->pending[i];

	if (out < state->pending_count) {
		uint16_t left = state->pending_count - out;

		memmove(&state->pending[0], &state->pending[out],
			left * sizeof(state->pending[0]));
		state->pending_count = left;
	} else {
		state->pending_count = 0;
	}

	return out;
}

void eth_rxq_close(struct eth_rxq_sample_objects *state)
{
	struct timespec ts = {.tv_sec = 0, .tv_nsec = SLEEP_IN_NANOS};

	if (state == NULL)
		return;

	while (state->inflight_tasks != 0) {
		(void)doca_pe_progress(state->core_resources.core_objs.pe);
		nanosleep(&ts, &ts);
	}

	for (uint16_t i = 0; i < state->pending_count; i++)
		rte_pktmbuf_free(state->pending[i]);
	state->pending_count = 0;

	eth_rxq_cleanup(state);
	free(state->rss_queues);
	free(state);
}

/*
 * Build the child pipe: matches tun.esp_sn with mask htobe(1), two entries
 * forwarding to the two RXQs based on the LSB.
 */
__attribute__((unused))
static doca_error_t build_demux_child_pipe(struct eth_rxq_sample_objects **handles)
{
	doca_error_t status;
	struct doca_flow_match match = {0};
	struct doca_flow_match match_mask = {0};
	struct doca_flow_actions actions = {0};
	struct doca_flow_actions *actions_arr[1] = {&actions};
	struct doca_flow_pipe_cfg *pipe_cfg = NULL;
	struct doca_flow_fwd fwd_miss = {.type = DOCA_FLOW_FWD_DROP};
	struct doca_flow_fwd fwd_default = {.type = DOCA_FLOW_FWD_DROP};

	match.tun.type = DOCA_FLOW_TUN_ESP;
	match.tun.esp_sn = 0;
	match_mask.tun.esp_sn = DOCA_HTOBE32(1u);

	status = doca_flow_pipe_cfg_create(&pipe_cfg, g_demux_flow_resources.df_port);
	if (status != DOCA_SUCCESS)
		return status;
	if ((status = doca_flow_pipe_cfg_set_name(pipe_cfg, "ESP_SN_LSB_CHILD")) != DOCA_SUCCESS ||
	    (status = doca_flow_pipe_cfg_set_type(pipe_cfg, DOCA_FLOW_PIPE_BASIC)) != DOCA_SUCCESS ||
	    (status = doca_flow_pipe_cfg_set_is_root(pipe_cfg, false)) != DOCA_SUCCESS ||
	    (status = doca_flow_pipe_cfg_set_match(pipe_cfg, &match, &match_mask)) != DOCA_SUCCESS ||
	    (status = doca_flow_pipe_cfg_set_actions(pipe_cfg, actions_arr, NULL, NULL, 1)) != DOCA_SUCCESS)
		goto destroy_cfg;

	status = doca_flow_pipe_create(pipe_cfg, &fwd_default, &fwd_miss, &g_demux_pipe);
	if (status != DOCA_SUCCESS)
		goto destroy_cfg;
	doca_flow_pipe_cfg_destroy(pipe_cfg);
	pipe_cfg = NULL;

	for (uint16_t i = 0; i < 2; i++) {
		struct doca_flow_match entry_match = {0};
		struct doca_flow_fwd entry_fwd = {0};

		entry_match.tun.type = DOCA_FLOW_TUN_ESP;
		entry_match.tun.esp_sn = DOCA_HTOBE32((uint32_t)i);

		entry_fwd.type = DOCA_FLOW_FWD_RSS;
		entry_fwd.rss_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;
		entry_fwd.rss.queues_array = &handles[1 - i]->rxq_queue_id;
		entry_fwd.rss.nr_queues = 1;

		status = doca_flow_pipe_basic_add_entry(0, g_demux_pipe, &entry_match, 0,
							&actions, NULL, &entry_fwd, 0, NULL,
							&g_demux_entries[i]);
		if (status != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add demux child entry %u, err: %s",
				     i, doca_error_get_name(status));
			doca_flow_pipe_destroy(g_demux_pipe);
			g_demux_pipe = NULL;
			return status;
		}
	}

	return DOCA_SUCCESS;

destroy_cfg:
	doca_flow_pipe_cfg_destroy(pipe_cfg);
	return status;
}

/*
 * Build the root pipe: exact-matches outer ESP and forwards everything to the
 * already-created child pipe.
 */
__attribute__((unused))
static doca_error_t build_demux_root_pipe(struct eth_rxq_sample_objects **handles)
{
	doca_error_t status;
	struct doca_flow_match match = {0};
	struct doca_flow_actions actions = {0};
	struct doca_flow_actions *actions_arr[1] = {&actions};
	struct doca_flow_pipe_cfg *pipe_cfg = NULL;
	struct doca_flow_fwd fwd_miss = {.type = DOCA_FLOW_FWD_DROP};
	struct doca_flow_fwd fwd_to_child = {
		.type = DOCA_FLOW_FWD_PIPE,
		.next_pipe = g_demux_pipe,
	};

	(void)handles;

	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_ESP;

	status = doca_flow_pipe_cfg_create(&pipe_cfg, g_demux_flow_resources.df_port);
	if (status != DOCA_SUCCESS)
		return status;
	if ((status = doca_flow_pipe_cfg_set_name(pipe_cfg, "ESP_SN_LSB_ROOT")) != DOCA_SUCCESS ||
	    (status = doca_flow_pipe_cfg_set_type(pipe_cfg, DOCA_FLOW_PIPE_BASIC)) != DOCA_SUCCESS ||
	    (status = doca_flow_pipe_cfg_set_is_root(pipe_cfg, true)) != DOCA_SUCCESS ||
	    (status = doca_flow_pipe_cfg_set_match(pipe_cfg, &match, NULL)) != DOCA_SUCCESS ||
	    (status = doca_flow_pipe_cfg_set_actions(pipe_cfg, actions_arr, NULL, NULL, 1)) != DOCA_SUCCESS)
		goto destroy_cfg;

	status = doca_flow_pipe_create(pipe_cfg, &fwd_to_child, &fwd_miss, &g_demux_root_pipe);
	if (status != DOCA_SUCCESS)
		goto destroy_cfg;
	doca_flow_pipe_cfg_destroy(pipe_cfg);

	status = doca_flow_pipe_basic_add_entry(0, g_demux_root_pipe, &match, 0, &actions,
						NULL, NULL, 0, NULL, &g_demux_root_entry);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add root entry, err: %s", doca_error_get_name(status));
		doca_flow_pipe_destroy(g_demux_root_pipe);
		g_demux_root_pipe = NULL;
	}
	return status;

destroy_cfg:
	doca_flow_pipe_cfg_destroy(pipe_cfg);
	return status;
}

/*
 * Single-pipe LSB demux using PIPE_CONTROL (per-entry mask).
 * Root pipe with two entries; each entry exact-matches outer ESP and masks
 * tun.esp_sn down to the LSB, forwarding to its RXQ.
 */
static doca_error_t build_lsb_demux_single_pipe(struct eth_rxq_sample_objects **handles)
{
	doca_error_t status;
	struct doca_flow_pipe_cfg *pipe_cfg = NULL;
	struct doca_flow_fwd fwd_miss = {.type = DOCA_FLOW_FWD_DROP};

	status = doca_flow_pipe_cfg_create(&pipe_cfg, g_demux_flow_resources.df_port);
	if (status != DOCA_SUCCESS)
		return status;
	if ((status = doca_flow_pipe_cfg_set_name(pipe_cfg, "ESP_SN_LSB_DEMUX")) != DOCA_SUCCESS ||
	    (status = doca_flow_pipe_cfg_set_type(pipe_cfg, DOCA_FLOW_PIPE_CONTROL)) != DOCA_SUCCESS ||
	    (status = doca_flow_pipe_cfg_set_is_root(pipe_cfg, true)) != DOCA_SUCCESS)
		goto destroy_cfg;

	status = doca_flow_pipe_create(pipe_cfg, NULL, &fwd_miss, &g_demux_root_pipe);
	if (status != DOCA_SUCCESS)
		goto destroy_cfg;
	doca_flow_pipe_cfg_destroy(pipe_cfg);

	for (uint16_t i = 0; i < 2; i++) {
		struct doca_flow_match entry_match = {0};
		struct doca_flow_match entry_mask = {0};
		struct doca_flow_fwd entry_fwd = {0};

		entry_match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
		entry_match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_ESP;
		entry_match.tun.type = DOCA_FLOW_TUN_ESP;
		entry_match.tun.esp_sn = DOCA_HTOBE32((uint32_t)i);

		entry_mask.parser_meta.outer_l3_type = (uint8_t)0xff;
		entry_mask.parser_meta.outer_l4_type = (uint8_t)0xff;
		entry_mask.tun.type = (uint32_t)0xffffffff;
		entry_mask.tun.esp_sn = DOCA_HTOBE32(1u);

		entry_fwd.type = DOCA_FLOW_FWD_RSS;
		entry_fwd.rss_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;
		entry_fwd.rss.queues_array = &handles[i]->rxq_queue_id;
		entry_fwd.rss.nr_queues = 1;
		entry_fwd.rss.outer_flags = DOCA_FLOW_RSS_ESP;

		status = doca_flow_pipe_control_add_entry(0,
							  g_demux_root_pipe,
							  &entry_match,
							  &entry_mask,
							  NULL, /* condition */
							  NULL, /* actions */
							  NULL, /* actions_mask */
							  NULL, /* action_descs */
							  NULL, /* monitor */
							  0,    /* priority */
							  &entry_fwd,
							  NULL, /* usr_ctx */
							  &g_demux_entries[i]);
		if (status != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add control demux entry %u, err: %s",
				     i, doca_error_get_name(status));
			doca_flow_pipe_destroy(g_demux_root_pipe);
			g_demux_root_pipe = NULL;
			return status;
		}
	}
	return DOCA_SUCCESS;

destroy_cfg:
	doca_flow_pipe_cfg_destroy(pipe_cfg);
	return status;
}

/*
 * DIAGNOSTIC: install a single root pipe that fwds all ESP to RXQ 0. If WQEs
 * appear on q0, the root pipe is matching and we know the issue is in the
 * downstream demux. If no WQEs, traffic isn't even reaching our pipe.
 *
 * Original two-stage implementation kept below in build_demux_*_pipe — re-enable
 * once the root-only path is confirmed working.
 */
doca_error_t eth_rxq_install_lsb_demux_flow(struct eth_rxq_sample_objects **handles)
{
	doca_error_t status;

	if (handles == NULL || handles[0] == NULL || handles[1] == NULL)
		return DOCA_ERROR_INVALID_VALUE;
	if (g_demux_flow_inited)
		return DOCA_ERROR_IN_USE;

	status = eth_flow_common_init_flow();
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init doca flow, err: %s", doca_error_get_name(status));
		return status;
	}

	status = eth_flow_common_create_flow_port(handles[0]->core_resources.core_objs.dev,
						  0, &g_demux_flow_resources);
	if (status != DOCA_SUCCESS)
		goto cleanup_flow;

	status = build_lsb_demux_single_pipe(handles);
	if (status != DOCA_SUCCESS)
		goto destroy_port;

	status = doca_flow_entries_process(g_demux_flow_resources.df_port, 0, 10000, 4);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to process demux entries, err: %s", doca_error_get_name(status));
		goto destroy_root;
	}

	g_demux_flow_inited = true;
	DOCA_LOG_INFO("Installed single-pipe ESP SN LSB demux (rxq %u, %u)",
		      handles[0]->rxq_queue_id, handles[1]->rxq_queue_id);
	return DOCA_SUCCESS;

destroy_root:
	doca_flow_pipe_destroy(g_demux_root_pipe);
	g_demux_root_pipe = NULL;
destroy_port:
	(void)eth_flow_common_destroy_flow_port(&g_demux_flow_resources);
cleanup_flow:
	eth_flow_common_cleanup_flow();
	return status;
}

void eth_rxq_uninstall_demux_flow(void)
{
	if (!g_demux_flow_inited)
		return;
	if (g_demux_root_pipe != NULL) {
		doca_flow_pipe_destroy(g_demux_root_pipe);
		g_demux_root_pipe = NULL;
	}
	if (g_demux_pipe != NULL) {
		doca_flow_pipe_destroy(g_demux_pipe);
		g_demux_pipe = NULL;
	}
	if (g_demux_flow_resources.df_port != NULL)
		(void)eth_flow_common_destroy_flow_port(&g_demux_flow_resources);
	eth_flow_common_cleanup_flow();
	g_demux_root_entry = NULL;
	g_demux_entries[0] = NULL;
	g_demux_entries[1] = NULL;
	g_demux_flow_inited = false;
}

