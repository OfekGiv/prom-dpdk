/* SPDX-FileCopyrightText: NVIDIA CORPORATION & AFFILIATES
 * Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef NV_HWS_HOST_H
#define NV_HWS_HOST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nv_hws_dev_context;
struct nv_hws_dev_table;
struct nv_hws_dev_matcher;
struct nv_hws_dev_rule;
struct nv_hws_dev_action;
struct nv_hws_dev_resource;
struct nv_hws_dev_resource_queue;

enum nv_hws_dev_send_op_status {
	NV_HWS_DEV_SEND_OP_SUCCESS,
	NV_HWS_DEV_SEND_OP_ERROR,
};

enum nv_hws_dev_queue_op {
	/* Start executing all pending queued operations. */
	NV_HWS_DEV_QUEUE_OP_DRAIN_ASYNC = 1 << 0,
	/* Start executing all pending queued operations wait till completion. */
	NV_HWS_DEV_QUEUE_OP_DRAIN_SYNC = 1 << 1,
};

struct nv_hws_dev_send_op_result {
	/* Returns the status of the operation that this completion signals */
	enum nv_hws_dev_send_op_status status;
	/* The user data that will be returned on the completion events */
	void *user_data;
};

struct nv_hws_dev_action_data {
	union {
		struct {
			/* PRM format, control must be 0 (data only) */
			uint64_t data;
		} inline_action;

		struct {
			uint32_t resource_offset;
		} counter;
	};
};

struct nv_hws_dev_rule_attr {
	uint16_t queue_id;
	/* The user data that will be returned on the completion events */
	void *user_data;
	/* Indicates the index in the matcher, the rule will be inserted to */
	uint32_t rule_idx;
	uint32_t burst:1;
	uint32_t comp_mask;
};

struct nv_hws_dev_send_aso_wqe_attr {
	/* Returned user data in the completion entry */
	void *user_data;
	/* Resource offset */
	uint32_t offset;
	/* Batching multiple operations */
	uint32_t burst:1;
	uint32_t comp_mask;
};

struct nv_hws_dev_resource_queue_send_attr {
	/* Non-null signals the end of a batch. */
	void *user_data;
	/* Optional. Any requests that fail will be reported by a bit of 1 in
	 * this bitmap.
	 */
	uint64_t *result_bitmap;
	uint8_t resource_opmod;
	uint8_t fence:1;
};

struct nv_hws_dev_resource_queue_result {
	void *user_data;
	uint16_t num_failures;
	uint16_t num_successes;
};

/**
 * @brief Update rule action data by index.
 *
 * This function update action data on specific index without getting the rule itself.
 * The rule is inserted by the index value in the attr.
 * @param[in] dev_matcher The dev matcher that the rule will be update on.
 * @param[in] at_idx Rule update attributes.
 * @param[in] action_data Data corresponding to the action template.
 * @param[in] dest_action The destination action in case of match.
 * @param[in] attr Rule update attributes.
 * @return zero on successful enqueue non zero with errno set otherwise.
 */
int
nv_hws_dev_rule_action_data_update_by_idx(struct nv_hws_dev_matcher *dev_matcher,
					  uint8_t at_idx,
					  struct nv_hws_dev_action_data actions_data[],
					  struct nv_hws_dev_action *dest_action,
					  struct nv_hws_dev_rule_attr *attr);

/**
 * @brief Update rule action data.
 *
 * This function update action data on specific index without getting the rule itself.
 * The rule is inserted by the index value in the attr.
 * @param[in] dev_rule The dev rule to update.
 * @param[in] dev_matcher The dev matcher that the rule will be update on.
 * @param[in] at_idx Rule update attributes.
 * @param[in] action_data Data corresponding to the action template.
 * @param[in] dest_action The destination action in case of match.
 * @param[in] attr Rule update attributes.
 * @return zero on successful enqueue non zero with errno set otherwise.
 */
int
nv_hws_dev_rule_action_data_update(struct nv_hws_dev_rule *dev_rule,
				   struct nv_hws_dev_matcher *dev_matcher,
				   uint8_t at_idx,
				   struct nv_hws_dev_action_data actions_data[],
				   struct nv_hws_dev_action *dest_action,
				   struct nv_hws_dev_rule_attr *attr);

/**
 * @brief Poll queue for operation completions.
 *
 * @param[in] dev_ctx The dev context to which the queue belong to.
 * @param[in] queue_id The id of the queue to poll.
 * @param[out] res Result completion array.
 * @param[in] res_nb Maximum number of results to return.
 * @return Negative number on failure with errno set.
 *         The number of completions otherwise.
 */
int nv_hws_dev_queue_poll(struct nv_hws_dev_context *dev_ctx,
			  uint16_t queue_id,
			  struct nv_hws_dev_send_op_result res[],
			  uint32_t res_nb);

/**
 * @brief Drain a queue, make sure all the works there were sent to the HW.
 *
 * @param[in] dev_ctx The dev context to which the queue belong to.
 * @param[in] queue_id The id of the queue to poll.
 * @return int zero on successful drain non zero otherwise.
 */
int nv_hws_dev_send_queue_drain(struct nv_hws_dev_context *dev_ctx,
				uint16_t queue_id);

/**
 * @brief Get device rule size from device.
 *
 * @return size of a single dev rule struct.
 */
uint64_t nv_hws_dev_rule_get_handle_size(void);

/* Future
int nv_hws_dev_rule_create(struct nv_hws_dev_matcher *dev_matcher,
			   struct nv_hws_dev_rule *dev_rule);

int nv_hws_dev_rule_delete(struct nv_hws_dev_matcher *dev_matcher,
			   struct nv_hws_dev_rule *dev_rule);
*/

/**
 * @brief Post a raw ASO WQE to the queue.
 *
 * Posting ASO WQE into the queue (raw).
 *
 * @param[in] dev_ctx The dev context to which the queue belong to.
 * @param[in] resource The resource object.
 * @param[in] queue_id The id of the queue to poll.
 * @param[in] aso_wqe Raw ASO CTRL and DATA segments for enqueue in BE format.
 * @param[in] wqe_len The size of the wqe to be posted.
 * @param[in] attr ASO WQE attributes.
 * @return Zero on success non zero otherwise.
 */
int nv_hws_dev_send_aso_wqe(struct nv_hws_dev_context *dev_ctx,
			    struct nv_hws_dev_resource *resource,
			    uint16_t queue_id,
			    uint32_t *aso_wqe,
			    uint32_t wqe_len,
			    struct nv_hws_dev_send_aso_wqe_attr *attr);

/**
 * @brief Start a new request for a resource queue.
 *
 * @param[in] queue The resource queue to start a request for.
 * @return Pointer to the WQE data of the request.
 */
void *nv_hws_dev_resource_queue_start(struct nv_hws_dev_resource_queue *queue);

/**
 * @brief Post the current WQE to the resource queue.
 *
 * @param[in] queue The resource queue to post the WQE to.
 * @param[in] attr Send attribute for the WQE.
 * @return Zero on success, non zero with errno set otherwise.
 */
int
nv_hws_dev_resource_queue_end(struct nv_hws_dev_resource_queue *queue,
			      struct nv_hws_dev_resource_queue_send_attr *attr);

/**
 * @brief Poll a resource queue for completions.
 *
 * A resource queue only posts exactly one result for each batch. The result
 * user_data will match the one passed with the request that ended the batch.
 *
 * @param[in] queue The resource queue to poll.
 * @param[out] results Result array.
 * @param[in] num_results Maximum number of results to return.
 * @return The number of completions on success. Otherwise, negative number with
 * errno set.
 */
int nv_hws_dev_resource_queue_poll(struct nv_hws_dev_resource_queue *queue,
				   struct nv_hws_dev_resource_queue_result *results,
				   uint32_t num_results);

/**
 * @brief Execute an operation on the resource queue.
 *
 * @param[in] queue The resource queue to operate on.
 * @param[in] queue_op Queue operation to perform.
 * @param[in] user_data The user_data to associate with the current open batch.
 * @param[in] result_bitmap The optional status bitmap the current batch will
 * report errors to.
 * @return Zero on success, non-zero with errno set otherwise.
 */
int nv_hws_dev_resource_queue_execute_op(struct nv_hws_dev_resource_queue *queue,
					 uint32_t queue_op,
					 void *user_data,
					 uint64_t *result_bitmap);

/**
 * @brief Reset the resource queue wqe template.
 *
 * Reset the template data in the resource queue on the device. Useful for when
 * this data is not available on the host and needs to be populated from the
 * device instead. This operation is optional. If it is used, it must be
 * executed before any other device-side usage of the resource queue. The wqe
 * template must conform to the same size requirements as the one documented in
 * struct nv_hws_resource_queue_attr.
 *
 * @param[in] queue The resource queue.
 * @param[in] wqe_tmpl The template to set to all the resource queue WQEs.
 * @return Zero on success, non zero with errno set otherwise.
 */
int
nv_hws_dev_resource_queue_reset_template(struct nv_hws_dev_resource_queue *queue,
					 void *wqe_tmpl);

#ifdef __cplusplus
}
#endif

#endif /* NV_HWS_HOST_H */
