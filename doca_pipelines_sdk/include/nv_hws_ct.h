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

#ifndef NV_HWS_CT_H
#define NV_HWS_CT_H

#include <nv_hws.h>

#ifdef __cplusplus
extern "C" {
#endif

enum nv_hws_ct_type {
	NV_HWS_CT_TYPE_V4,
	NV_HWS_CT_TYPE_V6,
};

struct nv_hws_ct_matcher_attr {
	struct nv_hws_matcher_attr *attr;
	enum nv_hws_ct_type ct_type;
	/* CT matcher supports bi directional CT rules */
	bool bi_direction;
};

struct nv_hws_ct_rule_attr {
	struct nv_hws_rule_attr *attr;
	/* In rule for CT app we can add double sides of rule : A->B, B->A */
	bool bi_direction;
};

enum nv_hws_ct_modify_header_mode {
	/* No modify-header action at all */
	NV_HWS_CT_MH_MODE_DATA_NONE,
	/* One action only, will be inserted inline the WQE */
	NV_HWS_CT_MH_MODE_DATA_OPTIMIZED,
	/* Set of actions, the data was written before rule insertion call */
	NV_HWS_CT_MH_MODE_DATA_PRE_WRITTEN,
	/* Set of actions, the data was not written yet */
	NV_HWS_CT_MH_MODE_DATA_INLINE,
};

struct nv_hws_ctv4_match {
	__be16 src_port;
	__be16 dst_port;
	__be32 src_addr;
	__be32 dst_addr;
	__be32 metadata;
	uint8_t protocol;
};

struct nv_hws_ctv6_match {
	__be16 src_port;
	__be16 dst_port;
	uint8_t src_addr[16];
	uint8_t dst_addr[16];
	__be32 metadata;
	uint8_t protocol;
};

struct nv_hws_ct_match {
	union {
		struct nv_hws_ctv4_match ctv4;
		struct nv_hws_ctv6_match ctv6;
	};
};

struct nv_hws_ct_action_data {
	struct {
		enum nv_hws_ct_modify_header_mode mode;
		struct nv_hws_action *action;
		struct nv_hws_action *action_reverse;
		union {
			struct {
				__be32 modify_value;
				__be32 modify_reverse_value;
			} optimized;
			struct {
				uint32_t modify_offset;
				uint32_t modify_reverse_offset;
				uint16_t data_size;
				uint8_t *modify_data;
				uint8_t *modify_reverse_data;
			} data;
		} info;
	} mh;
	/* Optional counter action */
	struct {
		struct nv_hws_action *action;
		uint32_t offset;
	} ctr;
	/* Optional tag action */
	struct {
		struct nv_hws_action *action;
		__be32 tag_value;
	} tag;
	/* Destination actions */
	struct nv_hws_action *dest_table;
	struct nv_hws_action *dest_table_reverse;
};

/**
 * @brief Create CT matcher for specific ct type.
 *
 * Connection tracking (CT) matcher implements a specific set of action and
 * match fields allowing more efficient rule creation and match.
 * The details of CT rules, match fields and action are described in other
 * nv_hws_ct APIs below.
 * CT matcher can be used only with nv_hws_ct API.
 *
 * @param[in] tbl Table that will contain the CT matcher.
 * @param[in] mt Match template matching of mendatory CT fields.
 * @param[in] ct_attr Attributes for matcher creation.
 * @return Matcher on success or NULL with errno set.
 */
struct nv_hws_matcher *
nv_hws_ct_matcher_create(struct nv_hws_table *tbl,
			 struct nv_hws_mt *mt,
			 struct nv_hws_ct_matcher_attr *ct_attr);

/**
 * @brief Destroy CT matcher.
 *
 * Destroy an existing CT matcher.
 *
 * @param[in] matcher CT Matcher to destroy.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_ct_matcher_destroy(struct nv_hws_matcher *matcher);

/**
 * @brief Bind array of actions to specific CT matcher.
 *
 * As part of using the CT API, before using an action it should be binded to
 * the matcher that is going to use it. CT action binding is not needed for
 * destination actions.
 *
 * @param[in] matcher CT matcher to bind the action to.
 * @param[in] actions Array of action pointers, to bind to the CT matcher.
 * @param[in] num_of_actions The size of that array.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_ct_actions_bind(struct nv_hws_matcher *matcher,
			   struct nv_hws_action *actions[],
			   size_t num_of_actions);

/**
 * @brief Unbind array of actions from specific CT matcher.
 *
 * Unbind actions previously binded by CT action bind function.
 *
 * @param[in] matcher CT matcher to unbind the action from.
 * @param[in] actions Array of action pointers, to unbind from the CT matcher.
 * @param[in] num_of_actions The size of that array.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_ct_actions_unbind(struct nv_hws_matcher *matcher,
			     struct nv_hws_action *actions[],
			     size_t num_of_actions);

/**
 * @brief Insert rule into matcher for CT application.
 *
 * @param[in] matcher with the CT matching fields (IPv4 orIPv6)
 * @param[in] match_val for that rule.
 * @param[in] actions_data for the actions to that rule.
 * @param[in] ct_attr for the rule insertion.
 * @param[in, out] rule returned as a handle to the user.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_ct_rule_create(struct nv_hws_matcher *matcher,
			  struct nv_hws_ct_match *match_val,
			  struct nv_hws_ct_action_data *actions_data,
			  struct nv_hws_ct_rule_attr *ct_attr,
			  struct nv_hws_rule *rule);

/**
 * @brief Destroy a rule out of a CT matcher.
 *
 * @param[in] rule handle that includes the rule details.
 * @param[in] ct_attr for the rule deletion.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_ct_rule_destroy(struct nv_hws_rule *rule,
			   struct nv_hws_ct_rule_attr *ct_attr);

/**
 * @brief Update an existing CT rule actions.
 *
 * @param[in] matcher with the CT matching fields (IPv4 orIPv6)
 * @param[in] actions_data new actions to that rule.
 * @param[in] attr for the rule insertion.
 * @param[in] ct_rule returned as a handle to the user.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_ct_rule_update(struct nv_hws_matcher *matcher,
			  struct nv_hws_ct_action_data *actions_data,
			  struct nv_hws_ct_rule_attr *ct_attr,
			  struct nv_hws_rule *rule);

/**
 * @brief Query existing rule for its match values.
 *
 * @param[in] rule handle that includes the rule details.
 * @param[out] match_val previously used for rule creation.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_ct_rule_query(struct nv_hws_rule *rule,
			 struct nv_hws_ct_match *match_val);

#ifdef __cplusplus
}
#endif

#endif /* NV_HWS_CT_H */
