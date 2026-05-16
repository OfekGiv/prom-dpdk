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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <mlx5_utils.h>
#include <doca_bitfield.h>
#include <doca_log.h>
#include <doca_flow.h>

#include "eth_flow_common.h"

DOCA_LOG_REGISTER(ETH::FLOW::COMMON);

#define COUNTERS_NUM (1 << 19)
#define DEFAULT_METADATA 1234
#define ACTIONS_MEMORY_SIZE 0

doca_error_t eth_flow_common_init_flow(uint16_t nb_queues)
{
	doca_error_t result, tmp_result;
	struct doca_flow_cfg *flow_cfg;

	result = doca_flow_cfg_create(&flow_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_cfg, err: %s", doca_error_get_name(result));
		return result;
	}
    if (nb_queues == 0)
        nb_queues = 1;
    result = doca_flow_cfg_set_pipe_queues(flow_cfg, nb_queues);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set pipe_queues, err: %s", doca_error_get_name(result));
		goto destroy_cfg;
	}
	result = doca_flow_cfg_set_mode_args(flow_cfg, "vnf");
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set mode_args, err: %s", doca_error_get_name(result));
		goto destroy_cfg;
	}
	result = doca_flow_cfg_set_nr_counters(flow_cfg, COUNTERS_NUM);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set nr_counters, err: %s", doca_error_get_name(result));
		goto destroy_cfg;
	}

	result = doca_flow_init(flow_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init doca flow, err: %s", doca_error_get_name(result));
		goto destroy_cfg;
	}

destroy_cfg:
	tmp_result = doca_flow_cfg_destroy(flow_cfg);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy doca_flow_cfg, err: %s", doca_error_get_name(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}

void eth_flow_common_cleanup_flow(void)
{
	doca_flow_destroy();
}

/*
 * Start DOCA Flow with desired port ID
 *
 * @dev [in]: The doca device
 * @df_port [out]: DOCA Flow port to start
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t start_doca_flow_port(struct doca_dev *dev, uint16_t port_id, struct doca_flow_port **df_port)
{
	doca_error_t status, tmp_result;
	struct doca_flow_port_cfg *port_cfg;

	status = doca_flow_port_cfg_create(&port_cfg);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_port_cfg, err: %s", doca_error_get_name(status));
		return status;
	}

	status = doca_flow_port_cfg_set_port_id(port_cfg, port_id);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_port_cfg port ID, err: %s", doca_error_get_name(status));
		goto destroy_port_cfg;
	}

    status = doca_flow_port_cfg_set_actions_mem_size(port_cfg, ACTIONS_MEMORY_SIZE);
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_port_cfg actions memory size, err: %s",
                 doca_error_get_name(status));
        goto destroy_port_cfg;
    }

	status = doca_flow_port_cfg_set_dev(port_cfg, dev);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_port_cfg dev, err: %s", doca_error_get_name(status));
		goto destroy_port_cfg;
	}

    status = doca_flow_port_cfg_set_devargs(port_cfg, "th_win_us=0");
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_port_cfg devargs, err: %s", doca_error_get_name(status));
        goto destroy_port_cfg;
    }

	status = doca_flow_port_start(port_cfg, df_port);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start doca flow, err: %s", doca_error_get_name(status));
		goto destroy_port_cfg;
	}

destroy_port_cfg:
	tmp_result = doca_flow_port_cfg_destroy(port_cfg);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set start doca_flow port: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(status, tmp_result);
	}
	return status;
}

/*
 * Create root pipe and add an entry into desired RXQ queue
 *
 * @df_port [in]: DOCA Flow port to create root pipe in
 * @rxq_queue_ids [in]: Pointer to RXQ queue IDs array
 * @nb_queues [in]: Number of queues IDs in the array
 * @root_pipe [out]: DOCA Flow pipe to create
 * @root_entry [out]: DOCA Flow port entry to create
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t create_root_pipe(struct doca_flow_port *df_port,
				     uint16_t *rxq_queue_ids,
				     uint16_t nb_queues,
				     struct doca_flow_pipe **root_pipe,
				     struct doca_flow_pipe_entry **root_entry)
{
    doca_error_t status;
    struct doca_flow_pipe_cfg *pipe_cfg;
    const char *pipe_name = "ROOT_PIPE";
	uint32_t log_queue_num = log2above(nb_queues);
	uint32_t calc_match_mask = (1U << log_queue_num) - 1; /* Mask for the highest bit of the queue number, used to split queues into two groups in the example. */
	uint32_t nb_rules = nb_queues; /* Number of rules to add, one per queue in this example. */
	int i;

    /* Default fwd for miss/non-overridden cases */
    struct doca_flow_fwd pipe_default_fwd = {
        .type = DOCA_FLOW_FWD_RSS,
        .rss_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED,
        .rss = {
            .queues_array = rxq_queue_ids,
            .nr_queues = nb_queues,
            .outer_flags = DOCA_FLOW_RSS_ESP,
        },
    };

    struct doca_flow_fwd fwd_miss = {
        .type = DOCA_FLOW_FWD_DROP,
    };

    /* Pipe-level template match/action */
    struct doca_flow_match match_tmpl;
    struct doca_flow_match match_mask;
    struct doca_flow_actions actions_tmpl, *actions_arr[1];

    /* Rule table: ESP SN -> target queue */
    struct esp_sn_rule_cfg {
        uint32_t esp_sn;
        uint16_t queue_id;
    };

    /* Per-entry objects must stay valid until entries_process */
    struct esp_sn_rule_cfg *rules;
    struct doca_flow_match *rule_match;
    struct doca_flow_actions *rule_actions;
    struct doca_flow_fwd *rule_fwd;
    struct doca_flow_pipe_entry **rule_entry;
    uint16_t (*one_queue)[1];

    if (nb_rules == 0)
        return DOCA_ERROR_INVALID_VALUE;

    rules = calloc(nb_rules, sizeof(*rules));
    rule_match = calloc(nb_rules, sizeof(*rule_match));
    rule_actions = calloc(nb_rules, sizeof(*rule_actions));
    rule_fwd = calloc(nb_rules, sizeof(*rule_fwd));
    rule_entry = calloc(nb_rules, sizeof(*rule_entry));
    one_queue = calloc(nb_rules, sizeof(*one_queue));
    if (rules == NULL || rule_match == NULL || rule_actions == NULL || rule_fwd == NULL ||
        rule_entry == NULL || one_queue == NULL) {
        DOCA_LOG_ERR("Failed to allocate root pipe rule arrays");
        status = DOCA_ERROR_NO_MEMORY;
        goto cleanup_arrays;
    }

    for (i = 0; i < nb_rules; i++) {
        rules[i].esp_sn = i;
        rules[i].queue_id = rxq_queue_ids[i];
    }

    memset(&match_tmpl, 0, sizeof(match_tmpl));
    memset(&match_mask, 0, sizeof(match_mask));
    memset(&actions_tmpl, 0, sizeof(actions_tmpl));

    /* Template declares we match IPv4+ESP+ESP tunnel fields (including esp_sn per entry) */
    match_tmpl.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
    match_tmpl.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_ESP;
    match_tmpl.tun.type = DOCA_FLOW_TUN_ESP;
    match_tmpl.tun.esp_sn = UINT32_MAX; /* wildcard in template */

	match_mask.tun.esp_sn =  DOCA_HTOBE32(calc_match_mask);

    actions_tmpl.meta.pkt_meta = DOCA_HTOBE32(DEFAULT_METADATA);
    actions_arr[0] = &actions_tmpl;

    status = doca_flow_pipe_cfg_create(&pipe_cfg, df_port);
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create doca_flow_pipe_cfg, err: %s", doca_error_get_name(status));
        goto cleanup_arrays;
    }

    status = doca_flow_pipe_cfg_set_name(pipe_cfg, pipe_name);
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set pipe name, err: %s", doca_error_get_name(status));
        goto destroy_pipe_cfg;
    }

    status = doca_flow_pipe_cfg_set_type(pipe_cfg, DOCA_FLOW_PIPE_BASIC);
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set pipe type, err: %s", doca_error_get_name(status));
        goto destroy_pipe_cfg;
    }

    status = doca_flow_pipe_cfg_set_is_root(pipe_cfg, true);
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set pipe root, err: %s", doca_error_get_name(status));
        goto destroy_pipe_cfg;
    }

    status = doca_flow_pipe_cfg_set_match(pipe_cfg, &match_tmpl, &match_mask);
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set pipe match, err: %s", doca_error_get_name(status));
        goto destroy_pipe_cfg;
    }

    status = doca_flow_pipe_cfg_set_actions(pipe_cfg, actions_arr, NULL, NULL, 1);
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set pipe actions, err: %s", doca_error_get_name(status));
        goto destroy_pipe_cfg;
    }

    status = doca_flow_pipe_create(pipe_cfg, &pipe_default_fwd, &fwd_miss, root_pipe);
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create root pipe, err: %s", doca_error_get_name(status));
        goto destroy_pipe_cfg;
    }
    doca_flow_pipe_cfg_destroy(pipe_cfg);

    /* Add one entry per ESP SN with per-entry RSS queue */
    for (i = 0; i < nb_rules; i++) {
        rule_match[i].parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
        rule_match[i].parser_meta.outer_l4_type = DOCA_FLOW_L4_META_ESP;
        rule_match[i].tun.type = DOCA_FLOW_TUN_ESP;
        rule_match[i].tun.esp_sn = DOCA_HTOBE32(rules[i].esp_sn);

        rule_actions[i].meta.pkt_meta = DOCA_HTOBE32(DEFAULT_METADATA + i);

        one_queue[i][0] = rules[i].queue_id;
        rule_fwd[i].type = DOCA_FLOW_FWD_RSS;
        rule_fwd[i].rss_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;
        rule_fwd[i].rss.queues_array = one_queue[i];
        rule_fwd[i].rss.nr_queues = 1;
        rule_fwd[i].rss.outer_flags = DOCA_FLOW_RSS_ESP;

        status = doca_flow_pipe_basic_add_entry(0, *root_pipe,
                               &rule_match[i], NULL,
                               &rule_actions[i], NULL,
                               &rule_fwd[i],
                               0, NULL, &rule_entry[i]);
        if (status != DOCA_SUCCESS) {
            doca_flow_pipe_destroy(*root_pipe);
            DOCA_LOG_ERR("Failed to add entry %u, err: %s", i, doca_error_get_name(status));
            goto cleanup_arrays;
        }
    }

    status = doca_flow_entries_process(df_port, 0, 10000, nb_rules);
    if (status != DOCA_SUCCESS) {
        doca_flow_pipe_destroy(*root_pipe);
        DOCA_LOG_ERR("Failed to process entries, err: %s", doca_error_get_name(status));
        goto cleanup_arrays;
    }

    /* Keep first handle for compatibility with existing API */
    if (root_entry != NULL)
        *root_entry = rule_entry[0];

    DOCA_LOG_INFO("Created Pipe with %u ESP-SN rules: %s", nb_rules, pipe_name);

cleanup_arrays:
    free(one_queue);
    free(rule_entry);
    free(rule_fwd);
    free(rule_actions);
    free(rule_match);
    free(rules);
    return status;

destroy_pipe_cfg:
    doca_flow_pipe_cfg_destroy(pipe_cfg);
    goto cleanup_arrays;
}

doca_error_t eth_flow_common_create_flow_port(struct doca_dev *dev,
					      uint16_t port_id,
					      struct eth_flow_common_resources *resources)
{
	doca_error_t status;

	if (dev == NULL)
		return DOCA_ERROR_INVALID_VALUE;
	if (resources == NULL)
		return DOCA_ERROR_INVALID_VALUE;

	status = start_doca_flow_port(dev, port_id, &(resources->df_port));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate eth_flow_common_resources: failed to init DOCA flow port, err: %s",
			     doca_error_get_name(status));
		return status;
	}

	return DOCA_SUCCESS;
}

doca_error_t eth_flow_common_destroy_flow_port(struct eth_flow_common_resources *resources)
{
	doca_error_t status = DOCA_SUCCESS;

	if (resources->df_port != NULL) {
		status = doca_flow_port_stop(resources->df_port);
		if (status != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to stop DOCA flow port, err: %s", doca_error_get_name(status));
			return status;
		}
	}

	return status;
}

doca_error_t eth_flow_common_create_flow_pipe(struct eth_flow_common_config *cfg,
					      struct eth_flow_common_resources *resources)
{
	doca_error_t status;

	status = create_root_pipe(resources->df_port,
				  cfg->rxq_queue_ids,
				  cfg->nb_queues,
				  &(resources->root_pipe),
				  &(resources->root_entry));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate eth_flow_common_resources: failed to create root pipe, err: %s",
			     doca_error_get_name(status));
		return status;
	}

	return DOCA_SUCCESS;
}

void eth_flow_common_destroy_flow_pipe(struct eth_flow_common_resources *resources)
{
	if (resources->root_pipe != NULL)
		doca_flow_pipe_destroy(resources->root_pipe);
}
