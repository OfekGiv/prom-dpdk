/*
 * Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of NVIDIA CORPORATION &
 * AFFILIATES (the "Company") and all right, title, and interest in and to the
 * software product, including all associated intellectual property rights, are
 * and shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 *
 */

#ifndef DOCA_FLOW_MP_H_
#define DOCA_FLOW_MP_H_

#include <doca_compat.h>
#include <doca_error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief pipeline configuration
 */
struct doca_flow_pipe_cfg;

/**
 * @brief doca flow pipe
 */
struct doca_flow_pipe;

/**
 * @brief doca flow pipe entry
 */
struct doca_flow_pipe_entry;

/**
 * @brief doca flow target
 */
struct doca_flow_target;

/**
 * @brief doca flow rdma transport domain
 */
enum doca_flow_rdma_transport {
	DOCA_FLOW_RDMA_TRANSPORT_INGRESS,
	/**< RDMA transport domain ingress */
	DOCA_FLOW_RDMA_TRANSPORT_EGRESS,
	/**< RDMA transport domain egress */
};

/**
 * @brief doca flow target type for QP actions
 */
enum doca_flow_target_qp_action_type {
	DOCA_FLOW_QP_ACTION_TYPE_TRIM_NACK = 100, /* Keep space for future target types */
};

/**
 * @brief Set the pipe in RDMA transport domain.
 *
 * @param [in] cfg
 * DOCA Flow pipe configuration.
 * @param [in] domain
 * DOCA_FLOW_RDMA_TRANSPORT_INGRESS / DOCA_FLOW_RDMA_TRANSPORT_EGRESS.
 * @return
 * DOCA_SUCCESS - in case of success.
 * Error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_ALREADY_EXIST - domain was already set.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_flow_pipe_cfg_set_rdma_transport_domain(struct doca_flow_pipe_cfg *cfg,
							  enum doca_flow_rdma_transport domain);

/**
 * @brief Disable the entry in the hash pipe.
 *
 * @note Completion is not required for this operation if the algorithm is round robin.
 *
 * @param [in] pipe_queue
 * DOCA Flow pipe queue.
 * @param [in] pipe
 * DOCA Flow pipe.
 * @param [in] entry
 * DOCA Flow entry.
 * @return
 * DOCA_SUCCESS - in case of success.
 * Error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NOT_SUPPORTED - update operation is not supported.
 * - DOCA_ERROR_AGAIN - resource temporarily unavailable, try again
 */
DOCA_EXPERIMENTAL
doca_error_t doca_flow_hash_pipe_entry_disable(uint16_t pipe_queue,
					       struct doca_flow_pipe *pipe,
					       struct doca_flow_pipe_entry *entry);

/**
 * @brief Enable the entry in the hash pipe.
 *
 * @note Completion is not required for this operation if the algorithm is round robin.
 *
 * @param [in] pipe_queue
 * DOCA Flow pipe queue.
 * @param [in] pipe
 * DOCA Flow pipe.
 * @param [in] entry
 * DOCA Flow entry.
 * @return
 * DOCA_SUCCESS - in case of success.
 * Error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NOT_SUPPORTED - update operation is not supported.
 * - DOCA_ERROR_AGAIN - resource temporarily unavailable, try again
 */
DOCA_EXPERIMENTAL
doca_error_t doca_flow_hash_pipe_entry_enable(uint16_t pipe_queue,
					      struct doca_flow_pipe *pipe,
					      struct doca_flow_pipe_entry *entry);

/**
 * @brief Get doca flow forward target for QP actions.
 *
 * @param [in] type
 * Target type.
 * @param [out] target
 * Target handler on success
 * @return
 * DOCA_SUCCESS - in case of success.
 * Error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NOT_SUPPORTED - unsupported type.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_flow_get_target_qp_actions(enum doca_flow_target_qp_action_type type,
					     struct doca_flow_target **target);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOCA_FLOW_MP_H_ */
