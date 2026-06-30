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

#ifndef NV_HWS_CAPS_H
#define NV_HWS_CAPS_H

#include "nv_hws.h"

#ifdef __cplusplus
extern "C" {
#endif

enum nv_hws_caps_type {
	NV_HWS_CAPS_TYPE_CONTEXT,
	NV_HWS_CAPS_TYPE_TABLE,
	NV_HWS_CAPS_TYPE_MATCHER,
	NV_HWS_CAPS_TYPE_MT,
	NV_HWS_CAPS_TYPE_RESOURCE,
	NV_HWS_CAPS_TYPE_ACTION,
};

enum nv_hws_caps_context {
	NV_HWS_CAPS_CONTEXT_HWS_SUPPORTED = 0x0,/* bool */
};

enum nv_hws_caps_table {
	NV_HWS_CAPS_TABLE_SUPPORTED = 0x0,/* bool */
};

enum nv_hws_caps_matcher {
	NV_HWS_CAPS_MATCHER_SUPPORTED = 0x0,/* bool */
	NV_HWS_CAPS_MATCHER_SUPPORT_RESIZE = 0x1,/* bool */
	NV_HWS_CAPS_MATCHER_HTABLE_MIN_LOG_SZ = 0x2,/* uint32_t */
	NV_HWS_CAPS_MATCHER_HTABLE_MAX_LOG_SZ = 0x3,/* uint32_t */
	NV_HWS_CAPS_MATCHER_HTABLE_MAX_LOG_COL = 0x4,/* uint32_t */
	NV_HWS_CAPS_MATCHER_RULES_MIN_LOG = 0x5,/* uint32_t */
	NV_HWS_CAPS_MATCHER_RULES_MAX_LOG = 0x6,/* uint32_t */
};

enum nv_hws_caps_matcher_type {
	NV_HWS_CAPS_MATCHER_TYPE_MATCH_REGULAR = 0x0,
	NV_HWS_CAPS_MATCHER_TYPE_MATCH_HASH_SPLIT = 0x1,
	NV_HWS_CAPS_MATCHER_TYPE_MATCH_LINEAR_LOOKUP = 0x2,
	NV_HWS_CAPS_MATCHER_TYPE_MATCH_STE_ARRAY = 0x3,
};

enum nv_hws_caps_mt {
	NV_HWS_CAPS_MT_SUPPORTED = 0x0,/* bool */
	NV_HWS_CAPS_MT_MAX_TAG_SZ = 0x1,/* uint32_t */
	NV_HWS_CAPS_MT_MAX_NUM_OF_FIELDS = 0x2,/* uint32_t */
	NV_HWS_CAPS_MT_FIELD_MASK_RESTRECTION = 0x3,/* uint32_t */
	NV_HWS_CAPS_MT_OPERATION_TYPE = 0x4,/* uint64_t */
	NV_HWS_CAPS_MT_COMPARISON_TYPE = 0x5,/* uint64_t */
};

enum nv_hws_caps_mt_type {
	NV_HWS_CAPS_MT_TYPE_MATCH = 0x0,
	NV_HWS_CAPS_MT_TYPE_COMPARE = 0x1,
	NV_HWS_CAPS_MT_TYPE_RANGE = 0x2,
};

enum nv_hws_caps_resource {
	NV_HWS_CAPS_RESOURCE_SUPPORTED = 0x0,/* bool */
	NV_HWS_CAPS_RESOURCE_MAX_OBJ_NUM = 0x1,/* uint64_t */
	NV_HWS_CAPS_RESOURCE_MAX_BULK_SZ = 0x2,/* uint32_t */
	NV_HWS_CAPS_RESOURCE_GRANULARITY = 0x3,/* uint32_t */
	NV_HWS_CAPS_RESOURCE_DEVX_OBJ_REQUIRED = 0x4,/* bool */
};

enum nv_hws_caps_action {
	NV_HWS_CAPS_ACTION_REFORMAT_TNL_L2_TO_L2 = 0x0,/* bool */
	NV_HWS_CAPS_ACTION_REFORMAT_L2_TO_TNL_L2 = 0x10,/* bool */
	NV_HWS_CAPS_ACTION_REFORMAT_TNL_L3_TO_L2 = 0x20,/* bool */
	NV_HWS_CAPS_ACTION_REFORMAT_L2_TO_TNL_L3 = 0x30,/* bool */
	NV_HWS_CAPS_ACTION_REFORMAT_TRAILER_IPSEC_INSERT = 0x40,/* bool */
	NV_HWS_CAPS_ACTION_REFORMAT_TRAILER_IPSEC_REMOVE = 0x41,/* bool */
	NV_HWS_CAPS_ACTION_REFORMAT_TRAILER_PSP_INSERT = 0x42,/* bool */
	NV_HWS_CAPS_ACTION_REFORMAT_TRAILER_PSP_REMOVE = 0x43,/* bool */
	NV_HWS_CAPS_ACTION_INSERT_HEADER = 0x50,/* bool */
	NV_HWS_CAPS_ACTION_INSERT_HEADER_MAX_SZ = 0x51,/* uint32_t */
	NV_HWS_CAPS_ACTION_INSERT_HEADER_MAX_OFFSET = 0x52,/* uint32_t */
	NV_HWS_CAPS_ACTION_INSERT_HEADER_ENCAP = 0x53,/* bool */
	NV_HWS_CAPS_ACTION_INSERT_HEADER_PUSH_ESP = 0x54,/* bool */
	NV_HWS_CAPS_ACTION_REMOVE_HEADER_BY_HEADER = 0x60,/* bool */
	NV_HWS_CAPS_ACTION_REMOVE_HEADER_BY_OFFSET = 0x61,/* bool */
	NV_HWS_CAPS_ACTION_REMOVE_HEADER_MAX_SZ = 0x62,/* uint32_t */
	NV_HWS_CAPS_ACTION_REMOVE_HEADER_MAX_OFFSET = 0x63,/* uint32_t */
	NV_HWS_CAPS_ACTION_COUNTER = 0x70,/* bool */
	NV_HWS_CAPS_ACTION_TAG = 0x80,/* bool */
	NV_HWS_CAPS_ACTION_MODIFY_HEADER = 0x90,/* bool */
	NV_HWS_CAPS_ACTION_POP_VLAN = 0xa0,/* bool */
	NV_HWS_CAPS_ACTION_PUSH_VLAN = 0xb0,/* bool */
	NV_HWS_CAPS_ACTION_BARRIER = 0xc0,/* bool */
	NV_HWS_CAPS_ACTION_ASO_METER = 0xd0,/* bool */
	NV_HWS_CAPS_ACTION_ASO_METER_RET_REG_ID_BIT_ARR = 0xd1,/* uint64_t */
	NV_HWS_CAPS_ACTION_ASO_METER_TAKE_FROM_REG_ID_BIT_ARR = 0xd2,/* uint64_t */
	NV_HWS_CAPS_ACTION_ASO_CT = 0xe0,/* bool */
	NV_HWS_CAPS_ACTION_ASO_CT_RET_REG_ID_BIT_ARR = 0xe1,/* uint64_t */
	NV_HWS_CAPS_ACTION_ASO_CT_TAKE_FROM_REG_ID_BIT_ARR = 0xe2,/* uint64_t */
	NV_HWS_CAPS_ACTION_ASO_IPSEC = 0xf0,/* bool */
	NV_HWS_CAPS_ACTION_ASO_IPSEC_RET_REG_ID_BIT_ARR = 0xf1,/* uint64_t */
	NV_HWS_CAPS_ACTION_ASO_IPSEC_TAKE_FROM_REG_ID_BIT_ARR = 0xf2,/* uint64_t */
	NV_HWS_CAPS_ACTION_ASO_FIRST_HIT = 0x100,/* bool */
	NV_HWS_CAPS_ACTION_ASO_FIRST_HIT_RET_REG_ID_BIT_ARR = 0x101,/* uint64_t */
	NV_HWS_CAPS_ACTION_ASO_FIRST_HIT_TAKE_FROM_REG_ID_BIT_ARR = 0x102,/* uint64_t */
	NV_HWS_CAPS_ACTION_ASO_QUEUE_MNG = 0x110,/* bool */
	NV_HWS_CAPS_ACTION_ASO_QUEUE_MNG_RET_REG_ID_BIT_ARR = 0x111,/* uint64_t */
	NV_HWS_CAPS_ACTION_ASO_QUEUE_MNG_TAKE_FROM_REG_ID_BIT_ARR = 0x112,/* uint64_t */
	NV_HWS_CAPS_ACTION_ASO_ENTROPY = 0x120,/* bool */
	NV_HWS_CAPS_ACTION_ASO_ENTROPY_RET_REG_ID_BIT_ARR = 0x121,/* uint64_t */
	NV_HWS_CAPS_ACTION_ASO_ENTROPY_TAKE_FROM_REG_ID_BIT_ARR = 0x122,/* uint64_t */
	NV_HWS_CAPS_ACTION_ASO_MEMORY = 0x130,/* bool */
	NV_HWS_CAPS_ACTION_ASO_MEMORY_RET_REG_ID_BIT_ARR = 0x131,/* uint64_t */
	NV_HWS_CAPS_ACTION_ASO_MEMORY_TAKE_FROM_REG_ID_BIT_ARR = 0x132,/* uint64_t */
	NV_HWS_CAPS_ACTION_ASO_FIFO = 0x140,/* bool */
	NV_HWS_CAPS_ACTION_ASO_FIFO_RET_REG_ID_BIT_ARR = 0x141,/* uint64_t */
	NV_HWS_CAPS_ACTION_ASO_FIFO_TAKE_FROM_REG_ID_BIT_ARR = 0x142,/* uint64_t */
	NV_HWS_CAPS_ACTION_NAT64 = 0x150,/* bool */
	NV_HWS_CAPS_ACTION_NAT64_MAX_NUM_OF_REGISTERS = 0x151,/* uint32_t */
	NV_HWS_CAPS_ACTION_CRYPTO_IPSEC_DECRYPT = 0x160,/* bool */
	NV_HWS_CAPS_ACTION_CRYPTO_IPSEC_ENCRYPT = 0x161,/* bool */
	NV_HWS_CAPS_ACTION_CRYPTO_PSP_DECRYPT = 0x162,/* bool */
	NV_HWS_CAPS_ACTION_CRYPTO_PSP_DECRYPT_W_KEY = 0x163,/* bool */
	NV_HWS_CAPS_ACTION_CRYPTO_PSP_ENCRYPT = 0x164,/* bool */
	NV_HWS_CAPS_ACTION_DEST_ARRAY = 0x170,/* bool */
	NV_HWS_CAPS_ACTION_DEST_ARRAY_MIN_NUM_OF_DEST = 0x171,/* uint32_t */
	NV_HWS_CAPS_ACTION_DEST_ARRAY_MAX_NUM_OF_DEST = 0x172,/* uint32_t */
	NV_HWS_CAPS_ACTION_DEST_DROP = 0x180,/* bool */
	NV_HWS_CAPS_ACTION_DEST_TIR = 0x190,/* bool */
	NV_HWS_CAPS_ACTION_DEST_TABLE = 0x1a0,/* bool */
	NV_HWS_CAPS_ACTION_DEST_TABLE_SUPP_JUMP_TO_FDB_RX = 0x1a1,/* bool */
	NV_HWS_CAPS_ACTION_DEST_VPORT = 0x1b0,/* bool */
	NV_HWS_CAPS_ACTION_DEST_VPORT_SUPPORT_WIRE_PORT_DEST = 0x1b1,/* bool */
	NV_HWS_CAPS_ACTION_DEST_VPORT_SUPPORT_ESWITCH_HAIRPIN = 0x1b2,/* bool */
	NV_HWS_CAPS_ACTION_DEST_MISS = 0x1c0,/* bool */
	NV_HWS_CAPS_ACTION_DEST_MATCHER = 0x1d0,/* bool */
	NV_HWS_CAPS_ACTION_DEST_MATCHER_SUPP_NON_ISOLATED_MATCHER = 0x1d1,/* bool */
	NV_HWS_CAPS_ACTION_DEST_RDMA_RESP = 0x1e0,/* bool */
	NV_HWS_CAPS_ACTION_DEST_RDMA_RESP_BIT_ARR = 0x1e1,/* uint64_t */
	NV_HWS_CAPS_ACTION_INLINE_MODIFY_HEADER_SET = 0x1f0,/* bool */
	NV_HWS_CAPS_ACTION_INLINE_MODIFY_HEADER_ADD = 0x200,/* bool */
	NV_HWS_CAPS_ACTION_INLINE_MODIFY_HEADER_ADD_FIELD = 0x210,/* bool */
	NV_HWS_CAPS_ACTION_INLINE_MODIFY_HEADER_COPY = 0x220,/* bool */
	NV_HWS_CAPS_ACTION_INLINE_INSERT_HEADER = 0x230,/* bool */
	NV_HWS_CAPS_ACTION_INLINE_INSERT_HEADER_ENCAP = 0x231,/* bool */
	NV_HWS_CAPS_ACTION_INLINE_INSERT_HEADER_PUSH_ESP = 0x232,/* bool */
	NV_HWS_CAPS_ACTION_GEN_CQE = 0x240,/* bool */
	NV_HWS_CAPS_ACTION_GEN_CQE_START_REG = 0x241,/* enum nv_hws_action_reg_c64 */
	NV_HWS_CAPS_ACTION_GEN_CQE_REG_COUNT = 0x242,/* uint32_t */
};

struct nv_hws_caps_query_in {
	/* Steering object type to query. */
	enum nv_hws_caps_type type;
	union {
	struct {
		enum nv_hws_caps_context query_cap;
	} ctx;
	struct {
		enum nv_hws_table_type type;
		enum nv_hws_caps_table query_cap;
	} tbl;
	struct {
		enum nv_hws_caps_matcher_type type;
		enum nv_hws_caps_matcher query_cap;
	} matcher;
	struct {
		enum nv_hws_caps_mt_type type;
		enum nv_hws_caps_mt query_cap;
	} mt;
	struct {
		enum nv_hws_resource_type type;
		enum nv_hws_caps_resource query_cap;
	} resource;
	struct {
		enum nv_hws_action_flags action_flag;
		enum nv_hws_caps_action query_cap;
	} action;
	};
};

/**
 * @brief Query HWS capabilities.
 *
 * Query a specific HWS object capabilities according query param which indicate the object type
 * and the relevant object query attributes.
 *
 * @param[in] ctx The context of the device.
 * @param[in] query Pointer to "struct nv_hws_caps_query_in".
 * @param[out] ret_cap_val pointer to the query return value.
 * @return Zero on success, non zero otherwise with errno set.
 */
int nv_hws_caps_query(struct nv_hws_context *ctx,
		      struct nv_hws_caps_query_in *query,
		      void *ret_cap_val);

#ifdef __cplusplus
}
#endif

#endif /* NV_HWS_CAPS_H */
