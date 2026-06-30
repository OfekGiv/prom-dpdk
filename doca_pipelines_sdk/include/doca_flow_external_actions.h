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

#ifndef DOCA_FLOW_EXTERNAL_ACTIONS_H_
#define DOCA_FLOW_EXTERNAL_ACTIONS_H_

#include <stdint.h>

#include <doca_error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Type of an ordered list external element
 */
enum doca_flow_ordered_list_element_type_external {
	DOCA_FLOW_ORDERED_LIST_ELEMENT_EXTERNAL_ACTIONS = 100,
	/**< Ordered list element is struct doca_flow_external_actions. */
};

/**
 * @brief doca flow external actions
 */
struct doca_flow_external_actions {
	uint32_t type;
	/**< Type of external action */
	void *resource;
	/**< Resource pointer of the external action */
	uint32_t resource_offset;
	/**< Resource offset of the external action */
	void *action;
	/**< The external action */
};

/**
 * @brief Ordered list external element
 * This struct is in alignment with the ordered list element struct.
 */
struct doca_flow_ordered_list_element_external {
	enum doca_flow_ordered_list_element_type_external type;
	/**< Type of the ordered list element */
	struct doca_flow_external_actions *external_actions;
	/**< Pointer to the external actions */
};

/**
 * @brief This is a Work-around for as long as the doca_flow_ordered_list_element_external struct is not public,
 *  to allow casting to the doca_flow_ordered_list_element_external struct.
 */
struct doca_flow_ordered_list_element_adjusted {
	union {
		struct doca_flow_ordered_list_element element;
		struct doca_flow_ordered_list_element_external external;
	};
};

/**
 * @brief doca flow external resource granularity
 */
enum doca_flow_external_resource_granularity {
	DOCA_FLOW_EXTERNAL_RESOURCE_GRANULARITY_8BIT = 8,   /**< 8 bit granularity */
	DOCA_FLOW_EXTERNAL_RESOURCE_GRANULARITY_16BIT = 16, /**< 16 bit granularity */
	DOCA_FLOW_EXTERNAL_RESOURCE_GRANULARITY_32BIT = 32, /**< 32 bit granularity */
	DOCA_FLOW_EXTERNAL_RESOURCE_GRANULARITY_64BIT = 64, /**< 64 bit granularity */
};

/**
 * @brief Get the meta u32 offset indices of the dynamic external action input fields
 *
 * @note The "meta_indices" output variable is a bitmask of the meta u32 offset indices that can be used, where
 * the bit position is the meta u32 offset index.
 *
 * @param [in] port
 * DOCA Flow port.
 * @param [out] meta_indices
 * Pointer to the output bitflag that holds the meta u32 offset indices
 * @return
 * DOCA_SUCCESS - in case of success.
 * Error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NOT_SUPPORTED - dynamic action is not supported.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_flow_external_action_get_dynamic_meta_indices(struct doca_flow_port *port, uint64_t *meta_indices);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOCA_FLOW_EXTERNAL_ACTIONS_H_ */
