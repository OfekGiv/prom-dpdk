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

#ifndef DOCA_FLOW_EXTERNAL_ACTION_INDEX_SELECTOR_H_
#define DOCA_FLOW_EXTERNAL_ACTION_INDEX_SELECTOR_H_

#include <doca_compat.h>
#include <doca_error.h>
#include <doca_flow.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief doca flow external resource index selector.
 */
struct doca_flow_external_resource_index_selector;

/**
 * @brief Index selector external action.
 * This action is used to select an index utilizing the index selector.
 */
struct doca_flow_external_action_index_selector {
	struct doca_flow_desc_field output; /**< The output field of the external action index selector which will
					     *   be set to the index selected by the index selector.
					     *	 @note The first value returned by the index selector is 1 */
	bool is_dynamic;		    /**< Whether the index selector external action is dynamic. */
};

/**
 * @brief Index selector policy.
 * This policy is used to select the index.
 */
enum doca_flow_external_resource_index_selector_policy {
	DOCA_FLOW_EXTERNAL_RESOURCE_INDEX_SELECTOR_POLICY_ROUND_ROBIN, /**< Round robin policy */
};

/**
 * @brief Index selector configuration.
 */
struct doca_flow_external_resource_index_selector_cfg {
	uint32_t num_resources;					       /**< Number of resources to create */
	enum doca_flow_external_resource_index_selector_policy policy; /**< Policy to use for selecting the index */
	bool hw_load_balancer;					       /**< Whether to use the hardware load balancer */
};

/**
 * @brief Index selector operations.
 * These operations are used to modify the index selector.
 */
enum doca_flow_external_resource_index_selector_operation {
	DOCA_FLOW_EXTERNAL_RESOURCE_INDEX_SELECTOR_OP_ENABLE,	  /**< Enable selection of the index */
	DOCA_FLOW_EXTERNAL_RESOURCE_INDEX_SELECTOR_OP_SKIP_ONCE,  /**< Skip selection of the index once */
	DOCA_FLOW_EXTERNAL_RESOURCE_INDEX_SELECTOR_OP_SKIP_TWICE, /**< Skip selection of the index twice */
	DOCA_FLOW_EXTERNAL_RESOURCE_INDEX_SELECTOR_OP_DISABLE,	  /**< Disable selection of the index */
};

/**
 * @brief Register the index selector external action - Should be called before DOCA Flow initialization.
 *
 * @param [out] type
 * Pointer to the type for the index selector external action.
 * @return
 * DOCA_SUCCESS - in case of success.
 * Error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_flow_external_action_index_selector_register(uint32_t *type);

/**
 * @brief Create an index selector external resource.
 *
 * @param [in] cfg
 * Configuration for the index selector.
 * @param [in] port
 * Pointer to the port on which the index selector is created.
 * @param [out] index_selector
 * Pointer to the created index selector external resource.
 * @return
 * DOCA_SUCCESS - in case of success.
 * Error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NO_MEMORY - memory allocation failed.
 * - DOCA_ERROR_UNKNOWN - otherwise.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_flow_external_resource_index_selector_create(
	struct doca_flow_external_resource_index_selector_cfg *cfg,
	struct doca_flow_port *port,
	struct doca_flow_external_resource_index_selector **index_selector);

/**
 * @brief Destroy an index selector external resource.
 *
 * @param [in] index_selector
 * Pointer to the index selector external resource to destroy.
 */
DOCA_EXPERIMENTAL
void doca_flow_external_resource_index_selector_destroy(
	struct doca_flow_external_resource_index_selector *index_selector);

/**
 * @brief Get the ID of the first resource in the index selector created.
 * This ID should be passed to the external action.
 *
 * @param [in] resource
 * Pointer to the index selector.
 * @param [out] resource_id
 * Pointer to the resource base ID of the index selector.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_flow_external_resource_index_selector_get_id(
	struct doca_flow_external_resource_index_selector *resource,
	uint32_t *resource_id);

/**
 * @brief Get the maximum index of the index selector.
 *
 * @param [in] index_selector
 * Pointer to the index selector.
 * @param [out] max_index
 * Pointer to the maximum index of the index selector.
 * @return
 * DOCA_SUCCESS - in case of success.
 * Error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_flow_external_resource_index_selector_get_max_index(
	struct doca_flow_external_resource_index_selector *index_selector,
	uint32_t *max_index);

/**
 * @brief Modify the index selector.
 *
 * This function modifies the state of a single index of a given index selector.
 *
 * @param [in] port
 * Pointer to the port.
 * @param [in] resource
 * Pointer to the index selector.
 * @param [in] resource_offset
 * The offset of the resource to modify.
 * @param [in] operation
 * Operation to perform on the index.
 * @param [in] index
 * The index to modify.
 * @return
 * DOCA_SUCCESS - in case of success.
 * Error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_UNKNOWN - otherwise.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_flow_external_resource_index_selector_modify(
	struct doca_flow_port *port,
	struct doca_flow_external_resource_index_selector *resource,
	uint32_t resource_offset,
	enum doca_flow_external_resource_index_selector_operation operation,
	uint32_t index);

/**
 * @brief Modify the index selector.
 *
 * This function modifies the state of a given range of indices of a given index selector.
 *
 * @param [in] port
 * Pointer to the port.
 * @param [in] resource
 * Pointer to the index selector.
 * @param [in] resource_offset
 * The offset of the resource to modify.
 * @param [in] operation
 * Operation to perform on the index.
 * @param [in] start_idx
 * The first index to modify.
 * @param [in] end_idx
 * The last index to modify.
 * @return
 * DOCA_SUCCESS - in case of success.
 * Error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_UNKNOWN - otherwise.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_flow_external_resource_index_selector_modify_range(
	struct doca_flow_port *port,
	struct doca_flow_external_resource_index_selector *resource,
	uint32_t resource_offset,
	enum doca_flow_external_resource_index_selector_operation operation,
	uint32_t start_idx,
	uint32_t end_idx);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOCA_FLOW_EXTERNAL_ACTION_INDEX_SELECTOR_H_ */
