
#ifndef RTE_PMD_MLX5_QP_H_
#define RTE_PMD_MLX5_QP_H_

#include <stdint.h>
#include <sys/queue.h>

#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_common.h>
#include <rte_spinlock.h>
#include <rte_trace_point.h>

#include <mlx5_common.h>
#include <mlx5_common_mr.h>

#include "mlx5.h"
#include "mlx5_autoconf.h"
#include "mlx5_rxtx.h"
#include "mlx5_trace.h"

__extension__
struct elts {
	uint16_t elts_head; /* Current counter in (*elts)[]. */
	uint16_t elts_tail; /* Counter of first element awaiting completion. */
	uint16_t elts_comp; /* elts index since last completion request. */
	uint16_t elts_s; /* Number of mbuf elements. */
	uint16_t elts_m; /* Mask for mbuf elements indices. */
};

/* TX queue descriptor. */
__extension__
struct __rte_cache_aligned mlx5_qp_data {
	struct elts sq_elts;
	struct elts rq_elts;
	/* Fields related to sq/rq elts mbuf storage. */
	uint16_t wqe_ci; /* Consumer index for work queue. */
	uint16_t wqe_pi; /* Producer index for work queue. */
	uint16_t wqe_s; /* Number of WQ elements. */
	uint16_t wqe_m; /* Mask Number for WQ elements. */
	uint16_t wqe_comp; /* WQE index since last completion request. */
	uint16_t wqe_thres; /* WQE threshold to request completion in CQ. */
	/* WQ related fields. */
	uint16_t cq_ci; /* Consumer index for completion queue. */
	uint16_t cq_pi; /* Production index for completion queue. */
	uint16_t cqe_s; /* Number of CQ elements. */
	uint16_t cqe_m; /* Mask for CQ indices. */
	/* CQ related fields. */
	uint16_t elts_n:4; /* elts[] length (in log2). */
	uint16_t cqe_n:4; /* Number of CQ elements (in log2). */
	uint16_t wqe_n:4; /* Number of WQ elements (in log2). */
	uint16_t tso_en:1; /* When set hardware TSO is enabled. */
	uint16_t tunnel_en:1;
	/* When set TX offload for tunneled packets are supported. */
	uint16_t swp_en:1; /* Whether SW parser is enabled. */
	uint16_t vlan_en:1; /* VLAN insertion in WQE is supported. */
	uint16_t db_nc:1; /* Doorbell mapped to non-cached region. */
	uint16_t db_heu:1; /* Doorbell heuristic write barrier. */
	uint16_t rt_timestamp:1; /* Realtime timestamp format. */
	uint16_t wait_on_time:1; /* WQE with timestamp is supported. */
	uint16_t fast_free:1; /* mbuf fast free on Tx is enabled. */
	uint16_t inlen_send; /* Ordinary send data inline size. */
	uint16_t inlen_empw; /* eMPW max packet size to inline. */
	uint16_t inlen_mode; /* Minimal data length to inline. */
	uint8_t tx_aggr_affinity; /* TxQ affinity configuration. */
	uint32_t qp_num_8s; /* QP number shifted by 8. */
	uint32_t sq_mem_len; /* Length of TxQ for WQEs */
	uint64_t offloads; /* Offloads for Tx Queue. */
	struct mlx5_mr_ctrl mr_ctrl; /* MR control descriptor. */
	struct mlx5_wqe *wqes; /* Work queue. */
	struct mlx5_wqe *wqes_end; /* Work queue array limit. */
#ifdef RTE_LIBRTE_MLX5_DEBUG
	uint32_t *fcqs; /* Free completion queue (debug extended). */
#else
	uint16_t *fcqs; /* Free completion queue. */
#endif
	volatile struct mlx5_cqe *cqes; /* Completion queue. */
	volatile uint32_t *qp_db; /* Work queue doorbell. */
	volatile uint32_t *cq_db; /* Completion queue doorbell. */
	uint16_t port_id; /* Port ID of device. */
	uint16_t idx; /* Queue index. */
	uint64_t rt_timemask; /* Scheduling timestamp mask. */
	uint64_t ts_mask; /* Timestamp flag dynamic mask. */
	uint64_t ts_last; /* Last scheduled timestamp. */
	int32_t ts_offset; /* Timestamp field dynamic offset. */
	uint32_t cq_mem_len; /* Length of TxQ for CQEs */
	struct mlx5_dev_ctx_shared *sh; /* Shared context. */
	struct mlx5_txq_stats stats; /* TX queue counters. */
	struct mlx5_txq_stats stats_reset; /* stats on last reset. */
	struct mlx5_uar_data uar_data;
	struct rte_mbuf *elts[];
	/* Storage for queued packets, must be the last field. */
};




#endif /* RTE_PMD_MLX5_QP_H_ */
