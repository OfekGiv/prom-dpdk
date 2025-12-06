
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

#include "generic/rte_spinlock.h"
#include "mlx5.h"
//#include "mlx5_common_devx.h"
//#include "mlx5_autoconf.h"
//#include "mlx5_rxtx.h"
//#include "mlx5_trace.h"

enum mlx5_qp_dir {
	MLX5_QP_DIR_TX = 1 << 0,
	MLX5_QP_DIR_RX = 1 << 1,
	MLX5_QP_DIR_TXRQ = MLX5_QP_DIR_TX | MLX5_QP_DIR_RX,
};

/* TX queue descriptor. */
__extension__
struct __rte_cache_aligned mlx5_qp_data {

	uint16_t sq_elts_head; /* Current counter in (*elts)[]. */
	uint16_t sq_elts_tail; /* Counter of first element awaiting completion. */
	uint16_t sq_elts_comp; /* elts index since last completion request. */
	uint16_t sq_elts_s; /* Number of mbuf elements. */
	uint16_t sq_elts_m; /* Mask for mbuf elements indices. */
	/* Fields related to elts mbuf storage. */
	uint16_t cq_ci; /* Consumer index for completion queue. */
	uint16_t cq_pi; /* Production index for completion queue. */
	uint16_t cqe_s; /* Number of CQ elements. */
	uint16_t cqe_m; /* Mask for CQ indices. */
	/* CQ related fields. */
	uint16_t sq_elts_n:4; /* elts[] length (in log2). */
	uint16_t sq_cqe_n; /* Number of CQ elements (in log2). */
	uint16_t rq_cqe_n; /* Number of CQ elements (in log2). */
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
	uint64_t sq_offloads; /* Offloads for Tx Queue. */
	uint64_t rq_offloads; /* Offloads for Tx Queue. */
	/* SQ / send WQ. */
	uint16_t sq_wqe_ci; /* Consumer index for work queue. */
	uint16_t sq_wqe_pi; /* Producer index for work queue. */
	uint16_t sq_wqe_s; /* Number of WQ elements. */
	uint16_t sq_wqe_m; /* Mask Number for WQ elements. */
	uint16_t sq_wqe_n; /* Number of WQ elements (in log2). */
	uint16_t sq_wqe_comp; /* WQE index since last completion request. */
	uint16_t sq_wqe_thres; /* WQE threshold to request completion in CQ. */
	struct mlx5_wqe *sq_wqes;
	struct mlx5_wqe *sq_wqes_end;
	/* RQ / recv WQ. */
	uint16_t rq_wqe_ci; /* Consumer index for work queue. */
	uint16_t rq_wqe_pi; /* Producer index for work queue. */
	uint16_t rq_wqe_s; /* Number of WQ elements. */
	uint16_t rq_wqe_m; /* Mask Number for WQ elements. */
	uint16_t rq_wqe_n; /* Number of WQ elements (in log2). */
	uint16_t rq_wqe_comp; /* WQE index since last completion request. */
	uint16_t rq_wqe_thres; /* WQE threshold to request completion in CQ. */
	struct mlx5_wqe *rq_wqes;
	struct mlx5_wqe *rq_wqes_end;
	uint32_t rq_mem_len;
	/* Send CQ. */
	uint16_t sq_cq_ci; /* Consumer index for completion queue. */
	uint16_t sq_cq_pi; /* Production index for completion queue. */
	uint16_t sq_cqe_s; /* Number of CQ elements. */
	uint16_t sq_cqe_m; /* Mask for CQ indices. */
	volatile struct mlx5_cqe *sq_cqes;
	volatile uint32_t *sq_cq_db;
	uint32_t sq_cq_mem_len;
	/* Recv CQ. */
	uint16_t rq_cq_ci; /* Consumer index for completion queue. */
	uint16_t rq_cq_pi; /* Production index for completion queue. */
	uint16_t rq_cqe_s; /* Number of CQ elements. */
	uint16_t rq_cqe_m; /* Mask for CQ indices. */
	volatile struct mlx5_cqe *rq_cqes;
	volatile uint32_t *rq_cq_db;
	uint32_t rq_cq_mem_len;
	/* Doorbells / UAR */
	volatile uint32_t *sq_db;
	volatile uint32_t *rq_db;
	struct mlx5_uar_data uar_data;
	/* Identity */
	uint32_t qp_num; /* QP number */
	uint16_t port_id;
	uint16_t qp_idx; /* QP index in qps[] array */
	/* Direction */
	uint8_t has_sq;
	uint8_t has_rq;
	struct mlx5_mr_ctrl mr_ctrl; /* MR control descriptor. */
	struct rte_mbuf *sq_elts[];
};


struct mlx5_qp_ctrl {

	uint8_t direction;

	LIST_ENTRY(mlx5_qp_ctrl) next; /* for priv->qpsctrl */
	RTE_ATOMIC(uint32_t) refcnt; /* Reference counter. */
	struct mlx5_priv *priv; /* Back pointer to private data. */
	unsigned int socket; /* CPU socket ID for allocations. */
	unsigned int max_inline_data; /* Max inline data. */
	unsigned int max_tso_header; /* Max TSO header size. */
	struct mlx5_qp_obj *obj;
	off_t uar_mmap_offset; /* UAR mmap offset for non-primary process. */
	uint16_t dump_file_n; /* Number of dump files. */
	struct mlx5_qp_data qp;
};



struct mlx5_qp_ctrl * mlx5_qp_get(struct rte_eth_dev *dev, uint16_t idx);
void qp_alloc_elts(struct mlx5_qp_ctrl *qp_ctrl);
int mlx5_qp_releasable(struct rte_eth_dev *dev, uint16_t idx);
int mlx5_qp_release(struct rte_eth_dev *dev, uint16_t idx);
static void qp_set_params(struct mlx5_qp_ctrl *qp_ctrl);
static int mlx5_qp_pre_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t *desc);
struct mlx5_qp_ctrl * mlx5_qp_new(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc, unsigned int socket, const struct rte_eth_txconf *conf);
int mlx5_qp_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc, unsigned int socket, const struct rte_eth_txconf *conf);

#endif /* RTE_PMD_MLX5_QP_H_ */
