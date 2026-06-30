/*
 * Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#ifndef DOCA_FLOW_EXTERNAL_ACTION_ARRAY_H_
#define DOCA_FLOW_EXTERNAL_ACTION_ARRAY_H_

#include <doca_compat.h>
#include <doca_error.h>
#include <doca_flow.h>
#include <doca_flow_external_actions.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief doca flow external resource array.
 */
struct doca_flow_external_resource_array;

/**
 * @brief External action array operations.
 * These operations are used to modify and/or query the array.
 */
enum doca_flow_external_action_array_operation {
	DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_LOAD,
	/**< Load from a given array index */
	DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_STORE,
	/**< Store to a given array index */
	DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_ADD,
	/**< Perform an addition to a given array index */
	DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_BITWISE_XOR,
	/**< Perform a bitwise XOR to a given array index */
	DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_BITWISE_OR,
	/**< Perform a bitwise OR to a given array index */
	DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_BITWISE_AND,
	/**< Perform a bitwise AND to a given array index */
	DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_FFS_ID_MSB,
	/**< Find first set bit in the array index and return the bit offset (starting from MSB) */
	DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_FFS_SET_BITMAP_MSB,
	/**< Find first set bit in the array index and return only this set bit (starting from MSB) */
	DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_FFS_ID_LSB,
	/**< Find first set bit in the array index and return the bit offset (starting from LSB) */
	DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_FFS_SET_BITMAP_LSB,
	/**< Find first set bit in the array index and return only this set bit (starting from LSB) */
	DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_RESET,
	/**< Reset the array index value and return the original value */
	DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_FIND_FIRST_SET_AND_RESET_ID_MSB,
	/**< Clear the most significant set bit, and return the bit offset */
	DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_FIND_FIRST_SET_AND_RESET_BITMAP_MSB,
	/**< Clear the most significant set bit, and return only this set bit (starting from MSB) */
	DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_FIND_FIRST_SET_AND_RESET_ID_LSB,
	/**< Clear the least significant set bit, and return the bit offset (starting from LSB) */
	DOCA_FLOW_EXTERNAL_ACTION_ARRAY_OP_FIND_FIRST_SET_AND_RESET_BITMAP_LSB,
	/**< Clear the least significant set bit, and return only this set bit (starting from LSB) */
};

/**
 * @brief External action array.
 * This action is used to act upon information stored in an array.
 */
struct doca_flow_external_action_array {
	enum doca_flow_external_action_array_operation operation;
	/**< Operation to perform on the array */
	struct doca_flow_desc_field input_output;
	/**< The input/output field of the external action array, will be set to the input/output of the operation. */
	uint32_t index;
	/**< Index of the array to act upon */
	bool is_dynamic;
	/**< Whether the external action array is dynamic. */
};

/**
 * @brief External resource array configuration.
 */
struct doca_flow_external_resource_array_cfg {
	size_t size;
	/**< Size of the array */
	uint32_t num_resources;
	/**< Number of resources to create */
	enum doca_flow_external_resource_granularity granularity;
	/**< Granularity of the array */
};

/**
 * @brief Register the array external action - Should be called before DOCA Flow initialization.
 *
 * @param [out] type
 * Pointer to the type for the array external action.
 * @return
 * DOCA_SUCCESS - in case of success.
 * Error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_flow_external_action_array_register(uint32_t *type);

/**
 * @brief Create an external action array resource.
 *
 * @param [in] cfg
 * Configuration for the external action array.
 * @param [in] port
 * Pointer to the port on which the external action array is created.
 * @param [out] array
 * Pointer to the created external action array resource.
 * @return
 * DOCA_SUCCESS - in case of success.
 * Error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NO_MEMORY - memory allocation failed.
 * - DOCA_ERROR_UNKNOWN - otherwise.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_flow_external_resource_array_create(struct doca_flow_external_resource_array_cfg *cfg,
						      struct doca_flow_port *port,
						      struct doca_flow_external_resource_array **array);

/**
 * @brief Destroy an external action array resource.
 *
 * @param [in] array
 * Pointer to the external action array resource to destroy.
 */
DOCA_EXPERIMENTAL
void doca_flow_external_resource_array_destroy(struct doca_flow_external_resource_array *array);

/**
 * @brief Update the external array resource.
 *
 * This function modifies the stored value at a given index of a given external array resource.
 *
 * @param [in] port
 * Pointer to the port.
 * @param [in] array
 * Pointer to the external array resource.
 * @param [in] resource_offset
 * The offset of the resource to modify.
 * @param [in] index
 * Index of the value to update.
 * @param [in] value
 * Updated value.
 * @return
 * DOCA_SUCCESS - in case of success.
 * Error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_UNKNOWN - otherwise.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_flow_external_resource_array_update(struct doca_flow_port *port,
						      struct doca_flow_external_resource_array *array,
						      uint32_t resource_offset,
						      uint32_t index,
						      uint64_t value);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOCA_FLOW_EXTERNAL_ACTION_ARRAY_H_ */
