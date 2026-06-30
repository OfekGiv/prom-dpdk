/*
 * Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#ifndef NV_MRC_EXT_H_
#define NV_MRC_EXT_H_

#include <infiniband/verbs.h>
#include <infiniband/mlx5dv.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum nv_mrc_cc_hint_version {
	NV_MRC_CC_HINT_V1 = 1
};

struct nv_mrc_cc_hint {
	uint8_t version;
	uint8_t reserved[3];
	uint32_t init_rate;
	uint32_t min_rate;
	uint32_t max_rate;
};

int nv_mrc_format_qp_hint_attr_cc(struct mrc_qp_hint_attr *qp_hint_attr,
				  uint32_t init_rate, uint32_t min_rate,
				  uint32_t max_rate);

struct mrc_cq {
	struct ibv_context *context;
	struct mrc_comp_channel *channel;
	int cqe;
	struct mrc_context *mrc_ctx;
};

struct mrc_qp {
	struct ibv_context *context;
	struct ibv_pd *pd;
	struct mrc_cq *send_cq;
	struct mrc_cq *recv_cq;
	struct ibv_srq *srq; //Is always NULL.
	uint32_t qp_num;
	enum ibv_qp_state state;
	enum ibv_qp_type qp_type;
	struct mrc_context *mrc_ctx;
};

#ifdef __cplusplus
}
#endif

#endif
