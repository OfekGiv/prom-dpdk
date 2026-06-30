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

#include <nv_hws.h>

#ifdef __cplusplus
extern "C" {
#endif

struct flexio_process;
struct nv_hws_dev_context;
struct nv_hws_dev_table;
struct nv_hws_dev_matcher;
struct nv_hws_dev_rule;
struct nv_hws_dev_action;
struct nv_hws_dev_resource_queue;

/**
 * @brief Bind HWS context to device.
 *
 * Bind device to an existing host context, the binded device context can be
 * used with the HWS device API (nv_hws_dev..). Device context is allocated
 * on the device memory space of the provided flexio process.
 *
 * @param[in] flexio_process Flexio process.
 * @param[in] ctx Host context to bind to.
 * @param[in] queues Number of queues to open on device context.
 * @param[in] queues_size Queue size for device context.
 * @return Device context on success or NULL with errno set.
 */
struct nv_hws_dev_context *
nv_hws_host_dev_bind_context(struct flexio_process *flexio_process,
			     struct nv_hws_context *ctx,
			     uint16_t queues,
			     uint16_t queue_size);

/**
 * @brief Un-Bind HWS context from device.
 *
 * Un-bind device context from context, this call will also free the dev_ctx.
 *
 * @param[in] ctx Host context used for binding to device.
 * @param[in] dev_ctx Device context to unbind.
 * @return Zero success or NULL with errno set.
 */
int nv_hws_host_dev_unbind_context(struct nv_hws_context *ctx,
				   struct nv_hws_dev_context *dev_ctx);

/**
 * @brief Bind HWS Table to device.
 *
 * Bind device to an existing host table.
 *
 * @return Device table on success or NULL with errno set.
 */
struct nv_hws_dev_table *
nv_hws_host_dev_bind_table(struct nv_hws_table *tbl);

/**
 * @brief Un-Bind HWS Table from device.
 *
 * Un-bind device table from table, this call will also free the dev_tbl.
 *
 * @param[in] tbl Host Table used for binding to device.
 * @param[in] dev_tbl Device table to unbind.
 * @return Zero success or NULL with errno set.
 */
int nv_hws_host_dev_unbind_table(struct nv_hws_table *tbl,
				 struct nv_hws_dev_table *dev_tbl);

/**
 * @brief Bind HWS Matcher to device.
 *
 * Bind device to an existing host matcher, the binded device matcher can be
 * used with the HWS device API (nv_hws_dev..). Device matcher is allocated
 * on the device memory space of the provided flexio process .
 *
 * @param[in] matcher Host matcher to bind to.
 * @return Device matcher on success or NULL with errno set.
 */
struct nv_hws_dev_matcher *
nv_hws_host_dev_bind_matcher(struct nv_hws_matcher *matcher);

/**
 * @brief Un-Bind HWS Matcher from device.
 *
 * Un-bind device matcher from matcher, this call will also free the dev_matcher.
 *
 * @param[in] matcher Host matcher used for binding to device.
 * @param[in] dev_matcher Device matcher to unbind.
 * @return Zero success or NULL with errno set.
 */
int nv_hws_host_dev_unbind_matcher(struct nv_hws_matcher *matcher,
				   struct nv_hws_dev_matcher *dev_matcher);

/**
 * @brief Bind HWS destination action to device.
 *
 * Bind device to an existing host dest action, the binded device action can be
 * used with the HWS device API (nv_hws_dev..). Device action is allocated
 * on the device memory space of the provided flexio process.
 *
 * @param[in] action Host action to bind to.
 * @return Device action on success or NULL with errno set.
 */
struct nv_hws_dev_action *
nv_hws_host_dev_bind_dest_action(struct nv_hws_action *action);

/**
 * @brief Un-Bind HWS Action from device.
 *
 * Un-bind device action from action, this call will also free the dev_action.
 *
 * @param[in] action Host action used for binding to device.
 * @param[in] dev_action Device action to unbind.
 * @return Zero success or NULL with errno set.
 */
int
nv_hws_host_dev_unbind_dest_action(struct nv_hws_action *action,
				   struct nv_hws_dev_action *dev_action);

/**
 * @brief Bind HWS rules to device.
 *
 * Bind device to an existing host rules, the binded device rules can be
 * used with the HWS device API (nv_hws_dev..). Device rules are allocated
 * on the device memory space of the provided flexio process.
 *
 * @param[in] rules Host rules array to bind.
 * @param[in] num_of_rules Number of host rules to bind.
 * @return Device rules on success or NULL with errno set.
 */
struct nv_hws_dev_rule *
nv_hws_host_dev_bind_rules(struct nv_hws_rule *rules[],
			   uint32_t num_of_rules);

/**
 * @brief Un-Bind HWS Action from device.
 *
 * Un-bind device rules from rules, this call will also free the dev_rules.
 *
 * @param[in] rules Host rules used for binding to device.
 * @param[in] dev_rules Device rules to unbind.
 * @param[in] num_of_rules Number of host rules to unbind.
 * @return Zero success or NULL with errno set.
 */
int nv_hws_host_dev_unbind_rules(struct nv_hws_rule *rules[],
				 struct nv_hws_dev_rule *dev_rules,
				 uint32_t num_of_rules);

/**
 * @brief Get device rule size from host.
 *
 * @return size of a single dev rule struct.
 */
size_t nv_hws_host_dev_rule_get_handle_size(void);

/**
 * @brief Bind HWS resource to device.
 *
 * Bind device to an existing host resource, the binded device resource can be
 * used with the HWS device API (nv_hws_dev..).
 * Device resources are allocated on the device memory space of the provided
 * flexio process.
 *
 * @param[in] resource Host resource to bind to the device.
 * @return Device resource on success or NULL with errno set.
 */
struct nv_hws_dev_resource *
nv_hws_host_dev_bind_resource(struct nv_hws_resource *resource);

/**
 * @brief Un-Bind HWS resource from device.
 *
 * Un-bind device resource from the host resource,
 * this call will also free the dev_resource.
 *
 * @param[in] resource Host resource used for binding to device.
 * @param[in] dev_resource Device resource to unbind.
 * @return Zero on success or NULL with errno set.
 */
int
nv_hws_host_dev_unbind_resource(struct nv_hws_resource *resource,
				struct nv_hws_dev_resource *dev_resource);

/**
 * @brief Create a device resource queue.
 *
 * The returned pointer is in device address space, and as such unusable on the
 * host except for the purposes of freeing it. The resource queue object must
 * be freed using the nv_hws_host_dev_destroy_object function.
 *
 * @param[in] ctx The context of the device.
 * @param[in] attr Attributes controlling resource queue creation.
 * @return Pointer to the device resource queue on success, or NULL with errno
 * set on failure.
 */
struct nv_hws_dev_resource_queue *
nv_hws_host_dev_resource_queue_create(struct nv_hws_context *ctx,
				      struct nv_hws_resource_queue_attr *attr);

/**
 * @brief Destroy device object.
 *
 * Generic destroy function for device objects. The supported objects for
 * destruction should declare support in the creation function description.
 *
 * @param[in] ctx The context in which the object was allocated.
 * @param[in] dev_ptr Pointer to the object in the device address space.
 * @return Zero success or negative with errno set.
 */
int nv_hws_host_dev_destroy_object(struct nv_hws_context *ctx, void *dev_obj);

#ifdef __cplusplus
}
#endif

#endif /* NV_HWS_HOST_H */
