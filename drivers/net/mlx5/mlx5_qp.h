
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
#include "mlx5_common_devx.h"
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

	/* Identity */
	uint32_t qp_num; /* QP number */
	uint16_t port_id;
	uint16_t qp_idx; /* QP index in qps[] array */

	/* Direction */
	uint8_t has_sq;
	uint8_t has_rq;

	/* SQ / send WQ. */
	uint16_t sq_wqe_ci; /* Consumer index for work queue. */
	uint16_t sq_wqe_pi; /* Producer index for work queue. */
	uint16_t sq_wqe_s; /* Number of WQ elements. */
	uint16_t sq_wqe_m; /* Mask Number for WQ elements. */
	uint16_t sq_wqe_n; /* Number of WQ elements (in log2). */
	struct mlx5_wqe *sq_wqes;
	struct mlx5_wqe *sq_wqes_end;
	uint32_t sq_mem_len;
	/* RQ / recv WQ. */
	uint16_t rq_wqe_ci; /* Consumer index for work queue. */
	uint16_t rq_wqe_pi; /* Producer index for work queue. */
	uint16_t rq_wqe_s; /* Number of WQ elements. */
	uint16_t rq_wqe_m; /* Mask Number for WQ elements. */
	uint16_t rq_wqe_n; /* Number of WQ elements (in log2). */
	struct mlx5_wqe *rq_wqes;
	struct mlx5_wqe *rq_wqes_end;
	uint32_t rq_mem_len;
	/* Send CQ. */
	uint16_t sq_cq_ci; /* Consumer index for completion queue. */
	uint16_t sq_cq_pi; /* Production index for completion queue. */
	uint16_t sq_cqe_s; /* Number of CQ elements. */
	uint16_t sq_cqe_m; /* Mask for CQ indices. */
	uint16_t sq_cqe_n; /* Number of CQ elements (in log2). */
	volatile struct mlx5_cqe *sq_cqes;
	volatile uint32_t *sq_cq_db;
	uint32_t sq_cq_mem_len;
	/* Recv CQ. */
	uint16_t rq_cq_ci; /* Consumer index for completion queue. */
	uint16_t rq_cq_pi; /* Production index for completion queue. */
	uint16_t rq_cqe_s; /* Number of CQ elements. */
	uint16_t rq_cqe_m; /* Mask for CQ indices. */
	uint16_t rq_cqe_n; /* Number of CQ elements (in log2). */
	volatile struct mlx5_cqe *rq_cqes;
	volatile uint32_t *rq_cq_db;
	uint32_t rq_cq_mem_len;
	/* Doorbells / UAR */
	volatile uint32_t *sq_db;
	volatile uint32_t *rq_db;
	struct mlx5_uar_data uar_data;
};

struct mlx5_qp_obj {
	struct mlx5_devx_qp *qp_devx;  /* DevX QP object */
	struct mlx5_devx_cq sq_cq_obj; /* Devx send CQ */
	struct mlx5_devx_cq rq_cq_obj; /* DevX recv CQ */

	/* WQ descriptors */
	struct {
		void *wqes;
		uint32_t log_wq_n;
		uint32_t stride;
		volatile uint32_t *db_rec;
	} sq_wq, rq_wq;

	volatile uint32_t *qp_db_rec; /* DB record array for SQ/RQ */
};

struct mlx5_qp_ctrl {
	struct mlx5_qp_data qp;
	struct mlx5_qp_obj *obj;

	rte_spinlock_t lock;
	uint32_t refcnt;
	uint32_t flags;

	LIST_ENTRY(mlx5_qp_ctrl) next; /* for priv->qpsctrl */
};





#endif /* RTE_PMD_MLX5_QP_H_ */
