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
#ifndef NV_HWS_PRM_H_
#define NV_HWS_PRM_H_

#include "nv_hws.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nv_hws_mh_pat;

enum nv_hws_mh_pat_flags {
	/* This pattern will be used for root tables. */
	NV_HWS_MH_PAT_FLAG_ROOT = 1 << 0,
};

struct nv_hws_mh_field {
	enum nv_hws_field_name name;
	enum nv_hws_field_header header;
};

struct nv_hws_mh_pat_attr {
	/* From enum nv_hws_mh_pat_flags. */
	uint32_t flags;
	uint32_t comp_mask;
};

/**
 * @brief Allocate a new modify header pattern.
 *
 * Creates a new pattern container that can be used to build modify header
 * operations. The pattern tracks actions like field modifications, header
 * insertions/removals. Once an action is added to the list, it can't be
 * modified.
 *
 * @param[in] ctx The context in which the pattern will be created.
 * @param[in] attr Attributes for the pattern.
 * @return Pointer to an empty allocated pattern on success, NULL with errno
 * otherwise.
 */
struct nv_hws_mh_pat *
nv_hws_mh_pat_alloc(struct nv_hws_context *ctx,
		    const struct nv_hws_mh_pat_attr *attr);

/**
 * @brief Free a modify header pattern.
 *
 * Releases all resources associated with the pattern, including any internally
 * allocated memory for pattern data.
 *
 * @param[in] pat The pattern to free.
 * @return Zero on success, negative error code with errno set otherwise.
 */
int nv_hws_mh_pat_free(struct nv_hws_mh_pat *pat);

/**
 * @brief Append a set value action to the pattern.
 *
 * Appends a set value action to the pattern. The action sets the specified
 * field to the given value.
 *
 * The value to be set is not part of the pattern, instead it will be specified
 * in the argument. Use nv_hws_mh_set_arg() to set the value.
 *
 * @param[in] pat The pattern to append the action to.
 * @param[in] field The field to set.
 * @param[in] offset The bit offset, counting from the least significant bit,
 * where the value should be set.
 * @param[in] length The length in bits of the value to set.
 * @return 0 on success, negative error code with errno set otherwise.
 */
int nv_hws_mh_append_action_set_value(struct nv_hws_mh_pat *pat,
				      struct nv_hws_mh_field field,
				      uint8_t offset,
				      uint8_t length);

/**
 * @brief Append an add value action to the pattern.
 *
 * Appends an add value action to the pattern. The action adds the given
 * value to the specified field.
 *
 * The value to be added is not part of the pattern, instead it will be
 * specified in the argument. Use nv_hws_mh_set_arg() to set the value.
 *
 * @param[in] pat The pattern to append the action to.
 * @param[in] field The field to add the value to.
 * @param[in] offset The bit offset, counting from the least significant bit,
 * where the value should be added.
 * @param[in] length The length in bits of the value to add.
 * @return 0 on success, negative error code with errno set otherwise.
 */
int nv_hws_mh_append_action_add_value(struct nv_hws_mh_pat *pat,
				      struct nv_hws_mh_field field,
				      uint8_t offset,
				      uint8_t length);

/**
 * @brief Append an add field action to the pattern.
 *
 * Appends an add field action to the pattern. The action adds the value of the
 * source field to the destination field.
 *
 * This type of action does not need any argument value. The source for the
 * addition is specified in the pattern, by src_field, src_offset and length.
 *
 * @param[in] pat The pattern to append the action to.
 * @param[in] src_field The field to add the value from.
 * @param[in] src_offset The bit offset, from the least significant bit, within
 * the source field to add the value from.
 * @param[in] length The length in bits of the value to add.
 * @param[in] dst_field The field to add the value to.
 * @param[in] dst_left_shift The bit count to left shift the destination field
 * by.
 * @return 0 on success, negative error code with errno set otherwise.
 */
int nv_hws_mh_append_action_add_field(struct nv_hws_mh_pat *pat,
				      struct nv_hws_mh_field src_field,
				      uint8_t src_offset,
				      uint8_t length,
				      struct nv_hws_mh_field dst_field,
				      uint8_t dst_left_shift);

/**
 * @brief Append a copy field action to the pattern.
 *
 * Appends a copy action to the pattern. The action copies the value of the
 * source field to the destination field.
 *
 * This type of action does not need any argument value. The source for the copy
 * is specified in the pattern, by src_field, src_offset and length.
 *
 * @param[in] pat The pattern to append the action to.
 * @param[in] src_field The field to copy the value from.
 * @param[in] offset The bit offset, counting from the least significant bit,
 * where the value should be copied from.
 * @param[in] length The length in bits of the value to copy.
 * @param[in] dst_field The field to copy the value to.
 * @param[in] dst_offset The bit offset, counting from the least significant
 * bit, where the value should be copied to.
 * @return 0 on success, negative error code on failure.
 */
int nv_hws_mh_append_action_copy_field(struct nv_hws_mh_pat *pat,
				       struct nv_hws_mh_field src_field,
				       uint8_t src_offset,
				       uint8_t length,
				       struct nv_hws_mh_field dst_field,
				       uint8_t dst_offset);

/**
 * @brief Append an insert 32-bit header action to the pattern.
 *
 * The value to be inserted is not part of the pattern, instead it will be
 * specified in the argument. Use nv_hws_mh_set_arg() to set the value.
 *
 * @param[in] pat The pattern to append the action to.
 * @param[in] anchor The anchor to insert the header at.
 * @param[in] offset The offset from the anchor to insert the header at. The
 * value is given in multiples of 16-bit words.
 * @return 0 on success, negative error code with errno set otherwise.
 */
int nv_hws_mh_append_action_insert_32b(struct nv_hws_mh_pat *pat,
				       enum nv_hws_action_anchor anchor,
				       uint8_t offset);

/**
 * @brief Append a remove header action to the pattern.
 *
 * This type of action does not need any argument value. There is no source
 * data, as this action only removes data.
 *
 * @param[in] pat The pattern to append the action to.
 * @param[in] start_anchor The anchor to remove the header from.
 * @param[in] end_anchor The anchor to remove the header to.
 * @param[in] decap Set if the header removal decapsulates the packet. This
 * signals the hardware to update offloads accordingly.
 * @return 0 on success, negative error code with errno set otherwise.
 */
int nv_hws_mh_append_action_remove_h2h(struct nv_hws_mh_pat *pat,
				       enum nv_hws_action_anchor start_anchor,
				       enum nv_hws_action_anchor end_anchor,
				       bool decap);

/**
 * @brief Append a remove words action to the pattern.
 *
 * This type of action does not need any argument value. There is no source
 * data, as this action only removes data.
 *
 * @param[in] pat The pattern to append the action to.
 * @param[in] start_anchor The anchor to remove the words from.
 * @param[in] offset The offset from the anchor to remove the words at. The
 * value is given in multiples of 16-bit words.
 * @param[in] length Number of 16-bit words to remove from the header.
 * @return 0 on success, negative error code with errno set otherwise.
 */
int nv_hws_mh_append_action_remove_words(struct nv_hws_mh_pat *pat,
					 enum nv_hws_action_anchor start_anchor,
					 uint8_t offset,
					 uint8_t length);

/**
 * @brief Finalize the pattern and optimize the order of actions.
 *
 * @param[in] pat The pattern to finalize.
 * @return 0 on success, negative error code with errno set otherwise.
 */
int nv_hws_mh_pat_finalize(struct nv_hws_mh_pat *pat);

/*
 * @brief Construct a new pattern by concatenating two existing patterns.
 *
 * Concatenate two finalized patterns p1 and p2 into a new pattern. The
 * internal action order of p1 will be kept in the finalized pattern.
 *
 * @param[in] p1 The first pattern, whose actions will not be reordered.
 * @param[in] p2 The second pattern, whose actions might be reordered.
 * @return A new pattern containing all the actions, or NULL with errno set.
 */
struct nv_hws_mh_pat *nv_hws_mh_pat_concat(const struct nv_hws_mh_pat *p1,
					   const struct nv_hws_mh_pat *p2);

/**
 * @brief Get the pattern data and size from a finalized pattern.
 *
 * This data is in PRM format and can be passed directly to
 * nv_hws_action_create_modify_header().
 *
 * The return pointer is valid only for the lifetime of the pattern and should
 * not be modified manually.
 *
 * @param[in] pat The pattern to get the data for.
 * @param[out] data The pattern data in PRM format.
 * @param[out] size The size of the pattern data.
 * @return 0 on success, negative error code with errno set otherwise.
 */
int nv_hws_mh_pat_get_data(struct nv_hws_mh_pat *pat,
			   __be64 **data,
			   size_t *size);

/**
 * @brief Get the physical index of a logical action.
 *
 * Maps a logical action index to its corresponding physical index in the
 * pattern data. During finalization, actions may be reordered to optimize
 * hardware execution. This function reveals the mapping between the logical
 * order (as actions were appended) and the physical order (after optimization).
 *
 * The pattern must be finalized before calling this function.
 *
 * @param[in] pat The finalized pattern to query.
 * @param[in] action_idx The logical action index (0 to num_actions-1).
 * @return The physical index on success, negative error code with errno set
 * otherwise.
 */
int nv_hws_mh_pat_get_phys_idx(const struct nv_hws_mh_pat *pat,
			       uint8_t action_idx);

/**
 * @brief Get the resource type (e.g. 64B, 128B) needed for a finalized pattern.
 *
 * @param[in] pat The pattern to get the resource type for.
 * @return Type of the resource to use with the pattern, or negative with errno
 * set.
 */
enum nv_hws_resource_type
nv_hws_mh_pat_get_resource_type(const struct nv_hws_mh_pat *pat);

/**
 * @brief Set an argument value into the buffer.
 *
 * Actions that require a argument must use this API to specify a value. Such
 * actions are otherwise incomplete.
 *
 * Use this function to set the value to a buffer that will later be shipped
 * with a rule's action_data, or written separately to the hardware using
 * nv_hws_action_enqueue_arg_write().
 *
 * @param[in] pat The pattern that this call refers to.
 * @param[in] arg_buf The argument buffer to set the value for.
 * @param[in] action_idx The index of the action to set the value for.
 * @param[in] val The value to set.
 * @return 0 on success, negative error code with errno set otherwise.
 */
int nv_hws_mh_set_arg(const struct nv_hws_mh_pat *pat,
		      void *arg_buf,
		      uint8_t action_idx,
		      __be32 val);

#ifdef __cplusplus
}
#endif

#endif /* NV_HWS_PRM_H_ */
