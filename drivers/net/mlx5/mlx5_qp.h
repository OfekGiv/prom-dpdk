
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
#include <mlx5_trace.h>
#include <mlx5_rxtx.h>

#include "generic/rte_spinlock.h"
#include "mlx5.h"
//#include "mlx5_common_devx.h"
//#include "mlx5_autoconf.h"
//#include "mlx5_rxtx.h"
//#include "mlx5_trace.h"

/* Mbuf dynamic flag offset for inline. */
extern uint64_t rte_net_mlx5_dynf_inline_mask;
#define RTE_MBUF_F_TX_DYNF_NOINLINE rte_net_mlx5_dynf_inline_mask

enum mlx5_qp_dir {
	MLX5_QP_DIR_TX = 1 << 0,
	MLX5_QP_DIR_RX = 1 << 1,
	MLX5_QP_DIR_TXRQ = MLX5_QP_DIR_TX | MLX5_QP_DIR_RX,
};

/* TX burst subroutines return codes. */
enum mlx5_txcmp_code {
	MLX5_TXCMP_CODE_EXIT = 0,
	MLX5_TXCMP_CODE_ERROR,
	MLX5_TXCMP_CODE_SINGLE,
	MLX5_TXCMP_CODE_MULTI,
	MLX5_TXCMP_CODE_TSO,
	MLX5_TXCMP_CODE_EMPW,
};

#define MLX5_TXOFF_CONFIG_MULTI (1u << 0) /* Multi-segment packets.*/
#define MLX5_TXOFF_CONFIG_TSO (1u << 1) /* TCP send offload supported.*/
#define MLX5_TXOFF_CONFIG_SWP (1u << 2) /* Tunnels/SW Parser offloads.*/
#define MLX5_TXOFF_CONFIG_CSUM (1u << 3) /* Check Sums offloaded. */
#define MLX5_TXOFF_CONFIG_INLINE (1u << 4) /* Data inlining supported. */
#define MLX5_TXOFF_CONFIG_VLAN (1u << 5) /* VLAN insertion supported.*/
#define MLX5_TXOFF_CONFIG_METADATA (1u << 6) /* Flow metadata. */
#define MLX5_TXOFF_CONFIG_EMPW (1u << 8) /* Enhanced MPW supported.*/
#define MLX5_TXOFF_CONFIG_MPW (1u << 9) /* Legacy MPW supported.*/
#define MLX5_TXOFF_CONFIG_TXPP (1u << 10) /* Scheduling on timestamp.*/

/* The most common offloads groups. */
#define MLX5_TXOFF_CONFIG_NONE 0
#define MLX5_TXOFF_CONFIG_FULL (MLX5_TXOFF_CONFIG_MULTI | \
				MLX5_TXOFF_CONFIG_TSO | \
				MLX5_TXOFF_CONFIG_SWP | \
				MLX5_TXOFF_CONFIG_CSUM | \
				MLX5_TXOFF_CONFIG_INLINE | \
				MLX5_TXOFF_CONFIG_VLAN | \
				MLX5_TXOFF_CONFIG_METADATA)

#define MLX5_TXOFF_CONFIG(mask) (olx & MLX5_TXOFF_CONFIG_##mask)

struct mlx5_qp_txq_stats {
#ifdef MLX5_PMD_SOFT_COUNTERS
	uint64_t opackets; /**< Total of successfully sent packets. */
	uint64_t obytes; /**< Total of successfully sent bytes. */
#endif
	uint64_t oerrors; /**< Total number of failed transmitted packets. */
};

/* TX queue send local data. */
__extension__
struct mlx5_qp_txq_local {
	struct mlx5_wqe *wqe_last; /* last sent WQE pointer. */
	struct rte_mbuf *mbuf; /* first mbuf to process. */
	uint16_t pkts_copy; /* packets copied to elts. */
	uint16_t pkts_sent; /* packets sent. */
	uint16_t pkts_loop; /* packets sent on loop entry. */
	uint16_t elts_free; /* available elts remain. */
	uint16_t wqe_free; /* available wqe remain. */
	uint16_t mbuf_off; /* data offset in current mbuf. */
	uint16_t mbuf_nseg; /* number of remaining mbuf. */
	uint16_t mbuf_free; /* number of inline mbufs to free. */
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
	uint64_t rt_timemask; /* Scheduling timestamp mask. */
	uint64_t ts_mask; /* Timestamp flag dynamic mask. */
	uint64_t ts_last; /* Last scheduled timestamp. */
	int32_t ts_offset; /* Timestamp field dynamic offset. */
	uint32_t cq_mem_len; /* Length of TxQ for CQEs */
	struct mlx5_dev_ctx_shared *sh; /* Shared context. */
	struct mlx5_qp_txq_stats stats; /* TX queue counters. */
	struct mlx5_mr_ctrl mr_ctrl; /* MR control descriptor. */
	uint16_t *fcqs; /* Free completion queue. */
	struct rte_mbuf *sq_elts[];
};

__extension__
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

void mlx5_qp_tx_handle_completion(struct mlx5_qp_data *__rte_restrict qp_txq, unsigned int olx __rte_unused);


/**
 * Read real time clock counter directly from the device PCI BAR area.
 * The PCI BAR must be mapped to the process memory space at initialization.
 *
 * @param dev
 *   Device to read clock counter from
 *
 * @return
 *   0 - if HCA BAR is not supported or not mapped.
 *   !=0 - read 64-bit value of real-time in UTC formatv (nanoseconds)
 */
static __rte_always_inline uint64_t mlx5_read_pcibar_clock(struct rte_eth_dev *dev)
{
	struct mlx5_proc_priv *ppriv = dev->process_private;

	if (ppriv && ppriv->hca_bar) {
		struct mlx5_priv *priv = dev->data->dev_private;
		struct mlx5_dev_ctx_shared *sh = priv->sh;
		uint64_t *hca_ptr = (uint64_t *)(ppriv->hca_bar) +
				  __mlx5_64_off(initial_seg, real_time);
		uint64_t __rte_atomic *ts_addr;
		uint64_t ts;

		ts_addr = (uint64_t __rte_atomic *)hca_ptr;
		ts = rte_atomic_load_explicit(ts_addr, rte_memory_order_seq_cst);
		ts = rte_be_to_cpu_64(ts);
		ts = mlx5_txpp_convert_rx_ts(sh, ts);
		return ts;
	}
	return 0;
}

static __rte_always_inline uint64_t mlx5_read_pcibar_clock_from_qp_txq(struct mlx5_qp_data *qp_txq)
{
	struct mlx5_qp_ctrl *qp_txq_ctrl = container_of(qp_txq, struct mlx5_qp_ctrl, qp);
	struct rte_eth_dev *dev = ETH_DEV(qp_txq_ctrl->priv);

	return mlx5_read_pcibar_clock(dev);
}

/**
 * Free the mbufs from the linear array of pointers.
 *
 * @param txq
 *   Pointer to Tx queue structure.
 * @param pkts
 *   Pointer to array of packets to be free.
 * @param pkts_n
 *   Number of packets to be freed.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 */
static __rte_always_inline void
mlx5_qp_tx_free_mbuf(struct mlx5_qp_data *__rte_restrict qp_txq,
		  struct rte_mbuf **__rte_restrict pkts,
		  unsigned int pkts_n,
		  unsigned int olx __rte_unused)
{
	struct rte_mempool *pool = NULL;
	struct rte_mbuf **p_free = NULL;
	struct rte_mbuf *mbuf;
	unsigned int n_free = 0;

	/*
	 * The implemented algorithm eliminates
	 * copying pointers to temporary array
	 * for rte_mempool_put_bulk() calls.
	 */
	MLX5_ASSERT(pkts);
	MLX5_ASSERT(pkts_n);
	/*
	 * Free mbufs directly to the pool in bulk
	 * if fast free offload is engaged
	 */
	if (!MLX5_TXOFF_CONFIG(MULTI) && qp_txq->fast_free) {
		mbuf = *pkts;
		pool = mbuf->pool;
		rte_mempool_put_bulk(pool, (void *)pkts, pkts_n);
		return;
	}
	for (;;) {
		for (;;) {
			/*
			 * Decrement mbuf reference counter, detach
			 * indirect and external buffers if needed.
			 */
			mbuf = rte_pktmbuf_prefree_seg(*pkts);
			if (likely(mbuf != NULL)) {
				MLX5_ASSERT(mbuf == *pkts);
				if (likely(n_free != 0)) {
					if (unlikely(pool != mbuf->pool))
						/* From different pool. */
						break;
				} else {
					/* Start new scan array. */
					pool = mbuf->pool;
					p_free = pkts;
				}
				++n_free;
				++pkts;
				--pkts_n;
				if (unlikely(pkts_n == 0)) {
					mbuf = NULL;
					break;
				}
			} else {
				/*
				 * This happens if mbuf is still referenced.
				 * We can't put it back to the pool, skip.
				 */
				++pkts;
				--pkts_n;
				if (unlikely(n_free != 0))
					/* There is some array to free.*/
					break;
				if (unlikely(pkts_n == 0))
					/* Last mbuf, nothing to free. */
					return;
			}
		}
		for (;;) {
			/*
			 * This loop is implemented to avoid multiple
			 * inlining of rte_mempool_put_bulk().
			 */
			MLX5_ASSERT(pool);
			MLX5_ASSERT(p_free);
			MLX5_ASSERT(n_free);
			/*
			 * Free the array of pre-freed mbufs
			 * belonging to the same memory pool.
			 */
			rte_mempool_put_bulk(pool, (void *)p_free, n_free);
			if (unlikely(mbuf != NULL)) {
				/* There is the request to start new scan. */
				pool = mbuf->pool;
				p_free = pkts++;
				n_free = 1;
				--pkts_n;
				if (likely(pkts_n != 0))
					break;
				/*
				 * This is the last mbuf to be freed.
				 * Do one more loop iteration to complete.
				 * This is rare case of the last unique mbuf.
				 */
				mbuf = NULL;
				continue;
			}
			if (likely(pkts_n == 0))
				return;
			n_free = 0;
			break;
		}
	}
}

/**
 * Free the mbuf from the elts ring buffer till new tail.
 *
 * @param txq
 *   Pointer to Tx queue structure.
 * @param tail
 *   Index in elts to free up to, becomes new elts tail.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 */
static __rte_always_inline void
mlx5_qp_tx_free_elts(struct mlx5_qp_data *__rte_restrict qp_txq,
		  uint16_t tail,
		  unsigned int olx __rte_unused)
{
	uint16_t n_elts = tail - qp_txq->sq_elts_tail;

	MLX5_ASSERT(n_elts);
	MLX5_ASSERT(n_elts <= qp_txq->sq_elts_s);
	/*
	 * Implement a loop to support ring buffer wraparound
	 * with single inlining of mlx5_tx_free_mbuf().
	 */
	do {
		unsigned int part;

		part = qp_txq->sq_elts_s - (qp_txq->sq_elts_tail & qp_txq->sq_elts_m);
		part = RTE_MIN(part, n_elts);
		MLX5_ASSERT(part);
		MLX5_ASSERT(part <= qp_txq->sq_elts_s);
		mlx5_qp_tx_free_mbuf(qp_txq,
				  &qp_txq->sq_elts[qp_txq->sq_elts_tail & qp_txq->sq_elts_m],
				  part, olx);
		qp_txq->sq_elts_tail += part;
		n_elts -= part;
	} while (n_elts);
}


/**
 * Store the mbuf being sent into elts ring buffer.
 * On Tx completion these mbufs will be freed.
 *
 * @param txq
 *   Pointer to Tx queue structure.
 * @param pkts
 *   Pointer to array of packets to be stored.
 * @param pkts_n
 *   Number of packets to be stored.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 */
static __rte_always_inline void
mlx5_qp_tx_copy_elts(struct mlx5_qp_data *__rte_restrict qp_txq,
		  struct rte_mbuf **__rte_restrict pkts,
		  unsigned int pkts_n,
		  unsigned int olx __rte_unused)
{
	unsigned int part;
	struct rte_mbuf **elts = (struct rte_mbuf **)qp_txq->sq_elts;

	MLX5_ASSERT(pkts);
	MLX5_ASSERT(pkts_n);
	part = qp_txq->sq_elts_s - (qp_txq->sq_elts_head & qp_txq->sq_elts_m);
	MLX5_ASSERT(part);
	MLX5_ASSERT(part <= qp_txq->sq_elts_s);
	/* This code is a good candidate for vectorizing with SIMD. */
	rte_memcpy((void *)(elts + (qp_txq->sq_elts_head & qp_txq->sq_elts_m)),
		   (void *)pkts,
		   RTE_MIN(part, pkts_n) * sizeof(struct rte_mbuf *));
	qp_txq->sq_elts_head += pkts_n;
	if (unlikely(part < pkts_n))
		/* The copy is wrapping around the elts array. */
		rte_memcpy((void *)elts, (void *)(pkts + part),
			   (pkts_n - part) * sizeof(struct rte_mbuf *));
}


/**
 * Build the Control Segment with specified opcode:
 * - MLX5_OPCODE_SEND
 * - MLX5_OPCODE_ENHANCED_MPSW
 * - MLX5_OPCODE_TSO
 *
 * @param txq
 *   Pointer to TX queue structure.
 * @param loc
 *   Pointer to burst routine local context.
 * @param wqe
 *   Pointer to WQE to fill with built Control Segment.
 * @param ds
 *   Supposed length of WQE in segments.
 * @param opcode
 *   SQ WQE opcode to put into Control Segment.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 */
static __rte_always_inline void
mlx5_qp_tx_cseg_init(struct mlx5_qp_data *__rte_restrict qp_txq,
		  struct mlx5_qp_txq_local *__rte_restrict loc __rte_unused,
		  struct mlx5_wqe *__rte_restrict wqe,
		  unsigned int ds,
		  unsigned int opcode,
		  unsigned int olx)
{
	struct mlx5_wqe_cseg *__rte_restrict cs = &wqe->cseg;
	uint64_t real_time;

	/* For legacy MPW replace the EMPW by TSO with modifier. */
	if (MLX5_TXOFF_CONFIG(MPW) && opcode == MLX5_OPCODE_ENHANCED_MPSW)
		opcode = MLX5_OPCODE_TSO | MLX5_OPC_MOD_MPW << 24;
	cs->opcode = rte_cpu_to_be_32((qp_txq->sq_wqe_ci << 8) | opcode);
	cs->sq_ds = rte_cpu_to_be_32(qp_txq->qp_num_8s | ds);
	if (MLX5_TXOFF_CONFIG(TXPP) && __rte_trace_point_fp_is_enabled())
		cs->flags = RTE_BE32(MLX5_COMP_ALWAYS <<
				     MLX5_COMP_MODE_OFFSET);
	else
		cs->flags = RTE_BE32(MLX5_COMP_ONLY_FIRST_ERR <<
				     MLX5_COMP_MODE_OFFSET);
	cs->misc = RTE_BE32(0);
	if (__rte_trace_point_fp_is_enabled()) {
		real_time = mlx5_read_pcibar_clock_from_qp_txq(qp_txq);
		if (!loc->pkts_sent)
			rte_pmd_mlx5_trace_tx_entry(real_time, qp_txq->port_id, qp_txq->qp_idx);
		rte_pmd_mlx5_trace_tx_wqe(real_time, (qp_txq->sq_wqe_ci << 8) | opcode);
	}
}


/**
 * Build the Wait on Time Segment with specified timestamp value.
 *
 * @param txq
 *   Pointer to TX queue structure.
 * @param loc
 *   Pointer to burst routine local context.
 * @param wqe
 *   Pointer to WQE to fill with built Control Segment.
 * @param ts
 *   Timesatmp value to wait.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 */
static __rte_always_inline void
mlx5_qp_tx_wseg_init(struct mlx5_qp_data *restrict qp_txq,
		  struct mlx5_qp_txq_local *restrict loc __rte_unused,
		  struct mlx5_wqe *restrict wqe,
		  uint64_t ts,
		  unsigned int olx __rte_unused)
{
	struct mlx5_wqe_wseg *ws;

	ws = RTE_PTR_ADD(wqe, MLX5_WSEG_SIZE);
	ws->operation = rte_cpu_to_be_32(MLX5_WAIT_COND_CYCLIC_SMALLER);
	ws->lkey = RTE_BE32(0);
	ws->va_high = RTE_BE32(0);
	ws->va_low = RTE_BE32(0);
	if (qp_txq->rt_timestamp) {
		ts = ts % (uint64_t)NS_PER_S
		   | (ts / (uint64_t)NS_PER_S) << 32;
	}
	ws->value = rte_cpu_to_be_64(ts);
	ws->mask = qp_txq->rt_timemask;
}


/**
 * Convert timestamp from mbuf format to linear counter
 * of Clock Queue completions (24 bits).
 *
 * @param sh
 *   Pointer to the device shared context to fetch Tx
 *   packet pacing timestamp and parameters.
 * @param ts
 *   Timestamp from mbuf to convert.
 * @return
 *   positive or zero value - completion ID to wait.
 *   negative value - conversion error.
 */
static __rte_always_inline int32_t
mlx5_txpp_convert_qp_tx_ts(struct mlx5_dev_ctx_shared *sh, uint64_t mts)
{
	uint64_t ts, ci;
	uint32_t tick;

	do {
		/*
		 * Read atomically two uint64_t fields and compare lsb bits.
		 * It there is no match - the timestamp was updated in
		 * the service thread, data should be re-read.
		 */
		rte_compiler_barrier();
		ci = rte_atomic_load_explicit(&sh->txpp.ts.ci_ts, rte_memory_order_relaxed);
		ts = rte_atomic_load_explicit(&sh->txpp.ts.ts, rte_memory_order_relaxed);
		rte_compiler_barrier();
		if (!((ts ^ ci) << (64 - MLX5_CQ_INDEX_WIDTH)))
			break;
	} while (true);
	/* Perform the skew correction, positive value to send earlier. */
	mts -= sh->txpp.skew;
	mts -= ts;
	if (unlikely(mts >= UINT64_MAX / 2)) {
		/* We have negative integer, mts is in the past. */
		rte_atomic_fetch_add_explicit(&sh->txpp.err_ts_past,
				   1, rte_memory_order_relaxed);
		return -1;
	}
	tick = sh->txpp.tick;
	MLX5_ASSERT(tick);
	/* Convert delta to completions, round up. */
	mts = (mts + tick - 1) / tick;
	if (unlikely(mts >= (1 << MLX5_CQ_INDEX_WIDTH) / 2 - 1)) {
		/* We have mts is too distant future. */
		rte_atomic_fetch_add_explicit(&sh->txpp.err_ts_future,
				   1, rte_memory_order_relaxed);
		return -1;
	}
	mts <<= 64 - MLX5_CQ_INDEX_WIDTH;
	ci += mts;
	ci >>= 64 - MLX5_CQ_INDEX_WIDTH;
	return ci;
}


/**
 * Build the Synchronize Queue Segment with specified completion index.
 *
 * @param txq
 *   Pointer to TX queue structure.
 * @param loc
 *   Pointer to burst routine local context.
 * @param wqe
 *   Pointer to WQE to fill with built Control Segment.
 * @param wci
 *   Completion index in Clock Queue to wait.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 */
static __rte_always_inline void
mlx5_qp_tx_qseg_init(struct mlx5_qp_data *restrict qp_txq,
		  struct mlx5_qp_txq_local *restrict loc __rte_unused,
		  struct mlx5_wqe *restrict wqe,
		  unsigned int wci,
		  unsigned int olx __rte_unused)
{
	struct mlx5_wqe_qseg *qs;

	qs = RTE_PTR_ADD(wqe, MLX5_WSEG_SIZE);
	qs->max_index = rte_cpu_to_be_32(wci);
	qs->qpn_cqn = rte_cpu_to_be_32(qp_txq->sh->txpp.clock_queue.cq_obj.cq->id);
	qs->reserved0 = RTE_BE32(0);
	qs->reserved1 = RTE_BE32(0);
}


/**
 * Convert the Checksum offloads to Verbs.
 *
 * @param buf
 *   Pointer to the mbuf.
 *
 * @return
 *   Converted checksum flags.
 */
static __rte_always_inline uint8_t
qp_txq_ol_cksum_to_cs(struct rte_mbuf *buf)
{
	uint32_t idx;
	uint8_t is_tunnel = !!(buf->ol_flags & RTE_MBUF_F_TX_TUNNEL_MASK);
	const uint64_t ol_flags_mask = RTE_MBUF_F_TX_TCP_SEG | RTE_MBUF_F_TX_L4_MASK |
				       RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_OUTER_IP_CKSUM;

	/*
	 * The index should have:
	 * bit[0] = RTE_MBUF_F_TX_TCP_SEG
	 * bit[2:3] = RTE_MBUF_F_TX_UDP_CKSUM, RTE_MBUF_F_TX_TCP_CKSUM
	 * bit[4] = RTE_MBUF_F_TX_IP_CKSUM
	 * bit[8] = RTE_MBUF_F_TX_OUTER_IP_CKSUM
	 * bit[9] = tunnel
	 */
	idx = ((buf->ol_flags & ol_flags_mask) >> 50) | (!!is_tunnel << 9);
	return mlx5_cksum_table[idx];
}


/**
 * Set Software Parser flags and offsets in Ethernet Segment of WQE.
 * Flags must be preliminary initialized to zero.
 *
 * @param loc
 *   Pointer to burst routine local context.
 * @param swp_flags
 *   Pointer to store Software Parser flags.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 *
 * @return
 *   Software Parser offsets packed in dword.
 *   Software Parser flags are set by pointer.
 */
static __rte_always_inline uint32_t
qp_txq_mbuf_to_swp(struct mlx5_qp_txq_local *__rte_restrict loc,
		uint8_t *swp_flags,
		unsigned int olx)
{
	uint64_t ol, tunnel;
	unsigned int idx, off;
	uint32_t set;

	if (!MLX5_TXOFF_CONFIG(SWP))
		return 0;
	ol = loc->mbuf->ol_flags;
	tunnel = ol & RTE_MBUF_F_TX_TUNNEL_MASK;
	/*
	 * Check whether Software Parser is required.
	 * Only customized tunnels may ask for.
	 */
	if (likely(tunnel != RTE_MBUF_F_TX_TUNNEL_UDP && tunnel != RTE_MBUF_F_TX_TUNNEL_IP))
		return 0;
	/*
	 * The index should have:
	 * bit[0:1] = RTE_MBUF_F_TX_L4_MASK
	 * bit[4] = RTE_MBUF_F_TX_IPV6
	 * bit[8] = RTE_MBUF_F_TX_OUTER_IPV6
	 * bit[9] = RTE_MBUF_F_TX_OUTER_UDP
	 */
	idx = (ol & (RTE_MBUF_F_TX_L4_MASK | RTE_MBUF_F_TX_IPV6 | RTE_MBUF_F_TX_OUTER_IPV6)) >> 52;
	idx |= (tunnel == RTE_MBUF_F_TX_TUNNEL_UDP) ? (1 << 9) : 0;
	*swp_flags = mlx5_swp_types_table[idx];
	/*
	 * Set offsets for SW parser. Since ConnectX-5, SW parser just
	 * complements HW parser. SW parser starts to engage only if HW parser
	 * can't reach a header. For the older devices, HW parser will not kick
	 * in if any of SWP offsets is set. Therefore, all of the L3 offsets
	 * should be set regardless of HW offload.
	 */
	off = loc->mbuf->outer_l2_len;
	if (MLX5_TXOFF_CONFIG(VLAN) && ol & RTE_MBUF_F_TX_VLAN)
		off += sizeof(struct rte_vlan_hdr);
	set = (off >> 1) << 8; /* Outer L3 offset. */
	off += loc->mbuf->outer_l3_len;
	if (tunnel == RTE_MBUF_F_TX_TUNNEL_UDP)
		set |= off >> 1; /* Outer L4 offset. */
	if (ol & (RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_IPV6)) { /* Inner IP. */
		const uint64_t csum = ol & RTE_MBUF_F_TX_L4_MASK;
			off += loc->mbuf->l2_len;
		set |= (off >> 1) << 24; /* Inner L3 offset. */
		if (csum == RTE_MBUF_F_TX_TCP_CKSUM ||
		    csum == RTE_MBUF_F_TX_UDP_CKSUM ||
		    (MLX5_TXOFF_CONFIG(TSO) && ol & RTE_MBUF_F_TX_TCP_SEG)) {
			off += loc->mbuf->l3_len;
			set |= (off >> 1) << 16; /* Inner L4 offset. */
		}
	}
	set = rte_cpu_to_le_32(set);
	return set;
}


/**
 * Copy data from chain of mbuf to the specified linear buffer.
 * Checksums and VLAN insertion Tx offload features. If data
 * from some mbuf copied completely this mbuf is freed. Local
 * structure is used to keep the byte stream state.
 *
 * @param pdst
 *   Pointer to the destination linear buffer.
 * @param loc
 *   Pointer to burst routine local context.
 * @param len
 *   Length of data to be copied.
 * @param must
 *   Length of data to be copied ignoring no inline hint.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 *
 * @return
 *   Number of actual copied data bytes. This is always greater than or
 *   equal to must parameter and might be lesser than len in no inline
 *   hint flag is encountered.
 */
static __rte_always_inline unsigned int
mlx5_qp_tx_mseg_memcpy(uint8_t *pdst,
		    struct mlx5_qp_txq_local *__rte_restrict loc,
		    unsigned int len,
		    unsigned int must,
		    unsigned int olx __rte_unused)
{
	struct rte_mbuf *mbuf;
	unsigned int part, dlen, copy = 0;
	uint8_t *psrc;

	MLX5_ASSERT(len);
	do {
		/* Allow zero length packets, must check first. */
		dlen = rte_pktmbuf_data_len(loc->mbuf);
		if (dlen <= loc->mbuf_off) {
			/* Exhausted packet, just free. */
			mbuf = loc->mbuf;
			loc->mbuf = mbuf->next;
			rte_pktmbuf_free_seg(mbuf);
			loc->mbuf_off = 0;
			MLX5_ASSERT(loc->mbuf_nseg > 1);
			MLX5_ASSERT(loc->mbuf);
			--loc->mbuf_nseg;
			if (loc->mbuf->ol_flags & RTE_MBUF_F_TX_DYNF_NOINLINE) {
				unsigned int diff;

				if (copy >= must) {
					/*
					 * We already copied the minimal
					 * requested amount of data.
					 */
					return copy;
				}
				diff = must - copy;
				if (diff <= rte_pktmbuf_data_len(loc->mbuf)) {
					/*
					 * Copy only the minimal required
					 * part of the data buffer. Limit amount
					 * of data to be copied to the length of
					 * available space.
					 */
					len = RTE_MIN(len, diff);
				}
			}
			continue;
		}
		dlen -= loc->mbuf_off;
		psrc = rte_pktmbuf_mtod_offset(loc->mbuf, uint8_t *,
					       loc->mbuf_off);
		part = RTE_MIN(len, dlen);
		rte_memcpy(pdst, psrc, part);
		copy += part;
		loc->mbuf_off += part;
		len -= part;
		if (!len) {
			if (loc->mbuf_off >= rte_pktmbuf_data_len(loc->mbuf)) {
				loc->mbuf_off = 0;
				/* Exhausted packet, just free. */
				mbuf = loc->mbuf;
				loc->mbuf = mbuf->next;
				rte_pktmbuf_free_seg(mbuf);
				loc->mbuf_off = 0;
				MLX5_ASSERT(loc->mbuf_nseg >= 1);
				--loc->mbuf_nseg;
			}
			return copy;
		}
		pdst += part;
	} while (true);
}

/**
 * Build the Ethernet Segment with inlined data from multi-segment packet.
 * Checks the boundary of WQEBB and ring buffer wrapping, supports Software
 * Parser, Checksums and VLAN insertion Tx offload features.
 *
 * @param txq
 *   Pointer to TX queue structure.
 * @param loc
 *   Pointer to burst routine local context.
 * @param wqe
 *   Pointer to WQE to fill with built Ethernet Segment.
 * @param vlan
 *   Length of VLAN tag insertion if any.
 * @param inlen
 *   Length of data to inline (VLAN included, if any).
 * @param tso
 *   TSO flag, set mss field from the packet.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 *
 * @return
 *   Pointer to the next Data Segment (aligned and possible NOT wrapped
 *   around - caller should do wrapping check on its own).
 */
static __rte_always_inline struct mlx5_wqe_dseg *
mlx5_qp_tx_eseg_mdat(struct mlx5_qp_data *__rte_restrict qp_txq,
		  struct mlx5_qp_txq_local *__rte_restrict loc,
		  struct mlx5_wqe *__rte_restrict wqe,
		  unsigned int vlan,
		  unsigned int inlen,
		  unsigned int tso,
		  unsigned int olx)
{
	struct mlx5_wqe_eseg *__rte_restrict es = &wqe->eseg;
	uint32_t csum;
	uint8_t *pdst;
	unsigned int part, tlen = 0;

	/*
	 * Calculate and set check sum flags first, uint32_t field
	 * in segment may be shared with Software Parser flags.
	 */
	csum = MLX5_TXOFF_CONFIG(CSUM) ? qp_txq_ol_cksum_to_cs(loc->mbuf) : 0;
	if (tso) {
		csum <<= 24;
		csum |= loc->mbuf->tso_segsz;
		es->flags = rte_cpu_to_be_32(csum);
	} else {
		es->flags = rte_cpu_to_le_32(csum);
	}
	/*
	 * Calculate and set Software Parser offsets and flags.
	 * These flags a set for custom UDP and IP tunnel packets.
	 */
	es->swp_offs = qp_txq_mbuf_to_swp(loc, &es->swp_flags, olx);
	/* Fill metadata field if needed. */
	es->metadata = MLX5_TXOFF_CONFIG(METADATA) ?
		       loc->mbuf->ol_flags & RTE_MBUF_DYNFLAG_TX_METADATA ?
		       rte_cpu_to_be_32(*RTE_FLOW_DYNF_METADATA(loc->mbuf)) :
		       0 : 0;
	MLX5_ASSERT(inlen >= MLX5_ESEG_MIN_INLINE_SIZE);
	pdst = (uint8_t *)&es->inline_data;
	if (MLX5_TXOFF_CONFIG(VLAN) && vlan) {
		/* Implement VLAN tag insertion as part inline data. */
		mlx5_qp_tx_mseg_memcpy(pdst, loc,
				    2 * RTE_ETHER_ADDR_LEN,
				    2 * RTE_ETHER_ADDR_LEN, olx);
		pdst += 2 * RTE_ETHER_ADDR_LEN;
		*(unaligned_uint32_t *)pdst = rte_cpu_to_be_32
						((RTE_ETHER_TYPE_VLAN << 16) |
						 loc->mbuf->vlan_tci);
		pdst += sizeof(struct rte_vlan_hdr);
		tlen += 2 * RTE_ETHER_ADDR_LEN + sizeof(struct rte_vlan_hdr);
	}
	MLX5_ASSERT(pdst < (uint8_t *)qp_txq->sq_wqes_end);
	/*
	 * The WQEBB space availability is checked by caller.
	 * Here we should be aware of WQE ring buffer wraparound only.
	 */
	part = (uint8_t *)qp_txq->sq_wqes_end - pdst;
	part = RTE_MIN(part, inlen - tlen);
	MLX5_ASSERT(part);
	do {
		unsigned int copy;

		/*
		 * Copying may be interrupted inside the routine
		 * if run into no inline hint flag.
		 */
		copy = tso ? inlen : qp_txq->inlen_mode;
		copy = tlen >= copy ? 0 : (copy - tlen);
		copy = mlx5_qp_tx_mseg_memcpy(pdst, loc, part, copy, olx);
		tlen += copy;
		if (likely(inlen <= tlen) || copy < part) {
			es->inline_hdr_sz = rte_cpu_to_be_16(tlen);
			pdst += copy;
			pdst = RTE_PTR_ALIGN(pdst, MLX5_WSEG_SIZE);
			return (struct mlx5_wqe_dseg *)pdst;
		}
		pdst = (uint8_t *)qp_txq->sq_wqes;
		part = inlen - tlen;
	} while (true);
}

/**
 * The routine checks timestamp flag in the current packet,
 * and push WAIT WQE into the queue if scheduling is required.
 *
 * @param txq
 *   Pointer to TX queue structure.
 * @param loc
 *   Pointer to burst routine local context.
 * @param elts
 *   Number of free elements in elts buffer to be checked, for zero
 *   value the check is optimized out by compiler.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 *
 * @return
 *   MLX5_TXCMP_CODE_EXIT - sending is done or impossible.
 *   MLX5_TXCMP_CODE_SINGLE - continue processing with the packet.
 *   MLX5_TXCMP_CODE_MULTI - the WAIT inserted, continue processing.
 * Local context variables partially updated.
 */
static __rte_always_inline enum mlx5_txcmp_code
mlx5_qp_tx_schedule_send(struct mlx5_qp_data *restrict qp_txq,
		      struct mlx5_qp_txq_local *restrict loc,
		      uint16_t elts,
		      unsigned int olx)
{
	if (MLX5_TXOFF_CONFIG(TXPP) &&
	    loc->mbuf->ol_flags & qp_txq->ts_mask) {
		struct mlx5_dev_ctx_shared *sh;
		struct mlx5_wqe *wqe;
		uint64_t ts;

		/*
		 * Estimate the required space quickly and roughly.
		 * We would like to ensure the packet can be pushed
		 * to the queue and we won't get the orphan WAIT WQE.
		 */
		if (loc->wqe_free <= MLX5_WQE_SIZE_MAX / MLX5_WQE_SIZE ||
		    loc->elts_free < elts)
			return MLX5_TXCMP_CODE_EXIT;
		/* Convert the timestamp into completion to wait. */
		ts = *RTE_MBUF_DYNFIELD(loc->mbuf, qp_txq->ts_offset, uint64_t *);
		if (qp_txq->ts_last && ts < qp_txq->ts_last)
			rte_atomic_fetch_add_explicit(&qp_txq->sh->txpp.err_ts_order,
					   1, rte_memory_order_relaxed);
		qp_txq->ts_last = ts;
		wqe = qp_txq->sq_wqes + (qp_txq->sq_wqe_ci & qp_txq->sq_wqe_m);
		sh = qp_txq->sh;
		if (qp_txq->wait_on_time) {
			/* The wait on time capability should be used. */
			ts -= sh->txpp.skew;
			rte_pmd_mlx5_trace_tx_wait(ts);
			mlx5_qp_tx_cseg_init(qp_txq, loc, wqe,
					  1 + sizeof(struct mlx5_wqe_wseg) /
					      MLX5_WSEG_SIZE,
					  MLX5_OPCODE_WAIT |
					  MLX5_OPC_MOD_WAIT_TIME << 24, olx);
			mlx5_qp_tx_wseg_init(qp_txq, loc, wqe, ts, olx);
		} else {
			/* Legacy cross-channel operation should be used. */
			int32_t wci;

			wci = mlx5_txpp_convert_qp_tx_ts(sh, ts);
			if (unlikely(wci < 0))
				return MLX5_TXCMP_CODE_SINGLE;
			/* Build the WAIT WQE with specified completion. */
			rte_pmd_mlx5_trace_tx_wait(ts - sh->txpp.skew);
			mlx5_qp_tx_cseg_init(qp_txq, loc, wqe,
					  1 + sizeof(struct mlx5_wqe_qseg) /
					      MLX5_WSEG_SIZE,
					  MLX5_OPCODE_WAIT |
					  MLX5_OPC_MOD_WAIT_CQ_PI << 24, olx);
			mlx5_qp_tx_qseg_init(qp_txq, loc, wqe, wci, olx);
		}
		++qp_txq->sq_wqe_ci;
		--loc->wqe_free;
		return MLX5_TXCMP_CODE_MULTI;
	}
	return MLX5_TXCMP_CODE_SINGLE;
}


/**
 * Build the Data Segment of pointer type or inline if data length is less than
 * buffer in minimal Data Segment size.
 *
 * @param txq
 *   Pointer to TX queue structure.
 * @param loc
 *   Pointer to burst routine local context.
 * @param dseg
 *   Pointer to WQE to fill with built Data Segment.
 * @param buf
 *   Data buffer to point.
 * @param len
 *   Data buffer length.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 */
static __rte_always_inline void
mlx5_qp_tx_dseg_iptr(struct mlx5_qp_data *__rte_restrict qp_txq,
		  struct mlx5_qp_txq_local *__rte_restrict loc,
		  struct mlx5_wqe_dseg *__rte_restrict dseg,
		  uint8_t *buf,
		  unsigned int len,
		  unsigned int olx __rte_unused)

{
	uintptr_t dst, src;

	MLX5_ASSERT(len);
	if (len > MLX5_DSEG_MIN_INLINE_SIZE) {
		dseg->bcount = rte_cpu_to_be_32(len);
		dseg->lkey = mlx5_mr_mb2mr(&qp_txq->mr_ctrl, loc->mbuf);
		dseg->pbuf = rte_cpu_to_be_64((uintptr_t)buf);

		return;
	}
	dseg->bcount = rte_cpu_to_be_32(len | MLX5_ETH_WQE_DATA_INLINE);
	/* Unrolled implementation of generic rte_memcpy. */
	dst = (uintptr_t)&dseg->inline_data[0];
	src = (uintptr_t)buf;
	if (len & 0x08) {
#ifdef RTE_ARCH_STRICT_ALIGN
		MLX5_ASSERT(dst == RTE_PTR_ALIGN(dst, sizeof(uint32_t)));
		*(uint32_t *)dst = *(unaligned_uint32_t *)src;
		dst += sizeof(uint32_t);
		src += sizeof(uint32_t);
		*(uint32_t *)dst = *(unaligned_uint32_t *)src;
		dst += sizeof(uint32_t);
		src += sizeof(uint32_t);
#else
		*(uint64_t *)dst = *(unaligned_uint64_t *)src;
		dst += sizeof(uint64_t);
		src += sizeof(uint64_t);
#endif
	}
	if (len & 0x04) {
		*(uint32_t *)dst = *(unaligned_uint32_t *)src;
		dst += sizeof(uint32_t);
		src += sizeof(uint32_t);
	}
	if (len & 0x02) {
		*(uint16_t *)dst = *(unaligned_uint16_t *)src;
		dst += sizeof(uint16_t);
		src += sizeof(uint16_t);
	}
	if (len & 0x01)
		*(uint8_t *)dst = *(uint8_t *)src;
}

/**
 * Build the Ethernet Segment with optionally inlined data with
 * VLAN insertion and following Data Segments (if any) from
 * multi-segment packet. Used by ordinary send and TSO.
 *
 * @param txq
 *   Pointer to TX queue structure.
 * @param loc
 *   Pointer to burst routine local context.
 * @param wqe
 *   Pointer to WQE to fill with built Ethernet/Data Segments.
 * @param vlan
 *   Length of VLAN header to insert, 0 means no VLAN insertion.
 * @param inlen
 *   Data length to inline. For TSO this parameter specifies exact value,
 *   for ordinary send routine can be aligned by caller to provide better WQE
 *   space saving and data buffer start address alignment.
 *   This length includes VLAN header being inserted.
 * @param tso
 *   Zero means ordinary send, inlined data can be extended,
 *   otherwise this is TSO, inlined data length is fixed.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 *
 * @return
 *   Actual size of built WQE in segments.
 */
static __rte_always_inline unsigned int
mlx5_qp_tx_mseg_build(struct mlx5_qp_data *__rte_restrict qp_txq,
		   struct mlx5_qp_txq_local *__rte_restrict loc,
		   struct mlx5_wqe *__rte_restrict wqe,
		   unsigned int vlan,
		   unsigned int inlen,
		   unsigned int tso,
		   unsigned int olx __rte_unused)
{
	struct mlx5_wqe_dseg *__rte_restrict dseg;
	unsigned int ds;

	MLX5_ASSERT((rte_pktmbuf_pkt_len(loc->mbuf) + vlan) >= inlen);
	loc->mbuf_nseg = NB_SEGS(loc->mbuf);
	loc->mbuf_off = 0;

	dseg = mlx5_qp_tx_eseg_mdat(qp_txq, loc, wqe, vlan, inlen, tso, olx);
	if (!loc->mbuf_nseg)
		goto dseg_done;
	/*
	 * There are still some mbuf remaining, not inlined.
	 * The first mbuf may be partially inlined and we
	 * must process the possible non-zero data offset.
	 */
	if (loc->mbuf_off) {
		unsigned int dlen;
		uint8_t *dptr;

		/*
		 * Exhausted packets must be dropped before.
		 * Non-zero offset means there are some data
		 * remained in the packet.
		 */
		MLX5_ASSERT(loc->mbuf_off < rte_pktmbuf_data_len(loc->mbuf));
		MLX5_ASSERT(rte_pktmbuf_data_len(loc->mbuf));
		dptr = rte_pktmbuf_mtod_offset(loc->mbuf, uint8_t *,
					       loc->mbuf_off);
		dlen = rte_pktmbuf_data_len(loc->mbuf) - loc->mbuf_off;
		/*
		 * Build the pointer/minimal Data Segment.
		 * Do ring buffer wrapping check in advance.
		 */
		if ((uintptr_t)dseg >= (uintptr_t)qp_txq->sq_wqes_end)
			dseg = (struct mlx5_wqe_dseg *)qp_txq->sq_wqes;
		mlx5_qp_tx_dseg_iptr(qp_txq, loc, dseg, dptr, dlen, olx);
		/* Store the mbuf to be freed on completion. */
		MLX5_ASSERT(loc->elts_free);
		qp_txq->sq_elts[qp_txq->sq_elts_head++ & qp_txq->sq_elts_m] = loc->mbuf;
		--loc->elts_free;
		++dseg;
		if (--loc->mbuf_nseg == 0)
			goto dseg_done;
		loc->mbuf = loc->mbuf->next;
		loc->mbuf_off = 0;
	}
	do {
		if (unlikely(!rte_pktmbuf_data_len(loc->mbuf))) {
			struct rte_mbuf *mbuf;

			/* Zero length segment found, just skip. */
			mbuf = loc->mbuf;
			loc->mbuf = loc->mbuf->next;
			rte_pktmbuf_free_seg(mbuf);
			if (--loc->mbuf_nseg == 0)
				break;
		} else {
			if ((uintptr_t)dseg >= (uintptr_t)qp_txq->sq_wqes_end)
				dseg = (struct mlx5_wqe_dseg *)qp_txq->sq_wqes;
			mlx5_qp_tx_dseg_iptr
				(qp_txq, loc, dseg,
				 rte_pktmbuf_mtod(loc->mbuf, uint8_t *),
				 rte_pktmbuf_data_len(loc->mbuf), olx);
			MLX5_ASSERT(loc->elts_free);
			qp_txq->sq_elts[qp_txq->sq_elts_head++ & qp_txq->sq_elts_m] = loc->mbuf;
			--loc->elts_free;
			++dseg;
			if (--loc->mbuf_nseg == 0)
				break;
			loc->mbuf = loc->mbuf->next;
		}
	} while (true);

dseg_done:
	/* Calculate actual segments used from the dseg pointer. */
	if ((uintptr_t)wqe < (uintptr_t)dseg)
		ds = ((uintptr_t)dseg - (uintptr_t)wqe) / MLX5_WSEG_SIZE;
	else
		ds = (((uintptr_t)dseg - (uintptr_t)wqe) +
		      qp_txq->sq_wqe_s * MLX5_WQE_SIZE) / MLX5_WSEG_SIZE;
	return ds;
}

/**
 * Tx one packet function for multi-segment TSO. Supports all
 * types of Tx offloads, uses MLX5_OPCODE_TSO to build WQEs,
 * sends one packet per WQE.
 *
 * This routine is responsible for storing processed mbuf
 * into elts ring buffer and update elts_head.
 *
 * @param txq
 *   Pointer to TX queue structure.
 * @param loc
 *   Pointer to burst routine local context.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 *
 * @return
 *   MLX5_TXCMP_CODE_EXIT - sending is done or impossible.
 *   MLX5_TXCMP_CODE_ERROR - some unrecoverable error occurred.
 * Local context variables partially updated.
 */
static __rte_always_inline enum mlx5_txcmp_code
mlx5_qp_tx_packet_multi_tso(struct mlx5_qp_data *__rte_restrict qp_txq,
			struct mlx5_qp_txq_local *__rte_restrict loc,
			unsigned int olx)
{
	struct mlx5_wqe *__rte_restrict wqe;
	unsigned int ds, dlen, inlen, ntcp, vlan = 0;

	MLX5_ASSERT(loc->elts_free >= NB_SEGS(loc->mbuf));
	if (MLX5_TXOFF_CONFIG(TXPP)) {
		enum mlx5_txcmp_code wret;

		/* Generate WAIT for scheduling if requested. */
		wret = mlx5_qp_tx_schedule_send(qp_txq, loc, 0, olx);
		if (wret == MLX5_TXCMP_CODE_EXIT)
			return MLX5_TXCMP_CODE_EXIT;
		if (wret == MLX5_TXCMP_CODE_ERROR)
			return MLX5_TXCMP_CODE_ERROR;
	}
	/*
	 * Calculate data length to be inlined to estimate
	 * the required space in WQE ring buffer.
	 */
	dlen = rte_pktmbuf_pkt_len(loc->mbuf);
	if (MLX5_TXOFF_CONFIG(VLAN) && loc->mbuf->ol_flags & RTE_MBUF_F_TX_VLAN)
		vlan = sizeof(struct rte_vlan_hdr);
	inlen = loc->mbuf->l2_len + vlan +
		loc->mbuf->l3_len + loc->mbuf->l4_len;
	if (unlikely((!inlen || !loc->mbuf->tso_segsz)))
		return MLX5_TXCMP_CODE_ERROR;
	if (loc->mbuf->ol_flags & RTE_MBUF_F_TX_TUNNEL_MASK)
		inlen += loc->mbuf->outer_l2_len + loc->mbuf->outer_l3_len;
	/* Packet must contain all TSO headers. */
	if (unlikely(inlen > MLX5_MAX_TSO_HEADER ||
		     inlen <= MLX5_ESEG_MIN_INLINE_SIZE ||
		     inlen > (dlen + vlan)))
		return MLX5_TXCMP_CODE_ERROR;
	/*
	 * Check whether there are enough free WQEBBs:
	 * - Control Segment
	 * - Ethernet Segment
	 * - First Segment of inlined Ethernet data
	 * - ... data continued ...
	 * - Data Segments of pointer/min inline type
	 */
	ds = NB_SEGS(loc->mbuf) + 2 + (inlen -
				       MLX5_ESEG_MIN_INLINE_SIZE +
				       MLX5_WSEG_SIZE +
				       MLX5_WSEG_SIZE - 1) / MLX5_WSEG_SIZE;
	if (unlikely(loc->wqe_free < ((ds + 3) / 4)))
		return MLX5_TXCMP_CODE_EXIT;
	/* Check for maximal WQE size. */
	if (unlikely((MLX5_WQE_SIZE_MAX / MLX5_WSEG_SIZE) < ds))
		return MLX5_TXCMP_CODE_ERROR;
#ifdef MLX5_PMD_SOFT_COUNTERS
	/* Update sent data bytes/packets counters. */
	ntcp = (dlen - (inlen - vlan) + loc->mbuf->tso_segsz - 1) /
		loc->mbuf->tso_segsz;
	/*
	 * One will be added for mbuf itself at the end of the mlx5_tx_burst
	 * from loc->pkts_sent field.
	 */
	--ntcp;
	qp_txq->stats.opackets += ntcp;
	qp_txq->stats.obytes += dlen + vlan + ntcp * inlen;
#endif
	wqe = qp_txq->sq_wqes + (qp_txq->sq_wqe_ci & qp_txq->sq_wqe_m);
	loc->wqe_last = wqe;
	mlx5_qp_tx_cseg_init(qp_txq, loc, wqe, 0, MLX5_OPCODE_TSO, olx);
	rte_pmd_mlx5_trace_tx_push(loc->mbuf, qp_txq->sq_wqe_ci);
	ds = mlx5_qp_tx_mseg_build(qp_txq, loc, wqe, vlan, inlen, 1, olx);
	wqe->cseg.sq_ds = rte_cpu_to_be_32(qp_txq->qp_num_8s | ds);
	qp_txq->sq_wqe_ci += (ds + 3) / 4;
	loc->wqe_free -= (ds + 3) / 4;
	return MLX5_TXCMP_CODE_MULTI;
}


/**
 * Build the Ethernet Segment without inlined data.
 * Supports Software Parser, Checksums and VLAN insertion Tx offload features.
 *
 * @param txq
 *   Pointer to TX queue structure.
 * @param loc
 *   Pointer to burst routine local context.
 * @param wqe
 *   Pointer to WQE to fill with built Ethernet Segment.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 */
static __rte_always_inline void
mlx5_qp_tx_eseg_none(struct mlx5_qp_data *__rte_restrict qp_txq __rte_unused,
		  struct mlx5_qp_txq_local *__rte_restrict loc,
		  struct mlx5_wqe *__rte_restrict wqe,
		  unsigned int olx)
{
	struct mlx5_wqe_eseg *__rte_restrict es = &wqe->eseg;
	uint32_t csum;

	/*
	 * Calculate and set check sum flags first, dword field
	 * in segment may be shared with Software Parser flags.
	 */
	csum = MLX5_TXOFF_CONFIG(CSUM) ? qp_txq_ol_cksum_to_cs(loc->mbuf) : 0;
	es->flags = rte_cpu_to_le_32(csum);
	/*
	 * Calculate and set Software Parser offsets and flags.
	 * These flags a set for custom UDP and IP tunnel packets.
	 */
	es->swp_offs = qp_txq_mbuf_to_swp(loc, &es->swp_flags, olx);
	/* Fill metadata field if needed. */
	es->metadata = MLX5_TXOFF_CONFIG(METADATA) ?
		       loc->mbuf->ol_flags & RTE_MBUF_DYNFLAG_TX_METADATA ?
		       rte_cpu_to_be_32(*RTE_FLOW_DYNF_METADATA(loc->mbuf)) :
		       0 : 0;
	/* Engage VLAN tag insertion feature if requested. */
	if (MLX5_TXOFF_CONFIG(VLAN) &&
	    loc->mbuf->ol_flags & RTE_MBUF_F_TX_VLAN) {
		/*
		 * We should get here only if device support
		 * this feature correctly.
		 */
		MLX5_ASSERT(qp_txq->vlan_en);
		es->inline_hdr = rte_cpu_to_be_32(MLX5_ETH_WQE_VLAN_INSERT |
						  loc->mbuf->vlan_tci);
	} else {
		es->inline_hdr = RTE_BE32(0);
	}
}

/**
 * Tx one packet function for multi-segment SEND. Supports all types of Tx
 * offloads, uses MLX5_OPCODE_SEND to build WQEs, sends one packet per WQE,
 * without any data inlining in Ethernet Segment.
 *
 * This routine is responsible for storing processed mbuf
 * into elts ring buffer and update elts_head.
 *
 * @param txq
 *   Pointer to TX queue structure.
 * @param loc
 *   Pointer to burst routine local context.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 *
 * @return
 *   MLX5_TXCMP_CODE_EXIT - sending is done or impossible.
 *   MLX5_TXCMP_CODE_ERROR - some unrecoverable error occurred.
 * Local context variables partially updated.
 */
static __rte_always_inline enum mlx5_txcmp_code
mlx5_qp_tx_packet_multi_send(struct mlx5_qp_data *__rte_restrict qp_txq,
			  struct mlx5_qp_txq_local *__rte_restrict loc,
			  unsigned int olx)
{
	struct mlx5_wqe_dseg *__rte_restrict dseg;
	struct mlx5_wqe *__rte_restrict wqe;
	unsigned int ds, nseg;

	MLX5_ASSERT(NB_SEGS(loc->mbuf) > 1);
	MLX5_ASSERT(loc->elts_free >= NB_SEGS(loc->mbuf));
	if (MLX5_TXOFF_CONFIG(TXPP)) {
		enum mlx5_txcmp_code wret;

		/* Generate WAIT for scheduling if requested. */
		wret = mlx5_qp_tx_schedule_send(qp_txq, loc, 0, olx);
		if (wret == MLX5_TXCMP_CODE_EXIT)
			return MLX5_TXCMP_CODE_EXIT;
		if (wret == MLX5_TXCMP_CODE_ERROR)
			return MLX5_TXCMP_CODE_ERROR;
	}
	/*
	 * No inline at all, it means the CPU cycles saving is prioritized at
	 * configuration, we should not copy any packet data to WQE.
	 */
	nseg = NB_SEGS(loc->mbuf);
	ds = 2 + nseg;
	if (unlikely(loc->wqe_free < ((ds + 3) / 4)))
		return MLX5_TXCMP_CODE_EXIT;
	/* Check for maximal WQE size. */
	if (unlikely((MLX5_WQE_SIZE_MAX / MLX5_WSEG_SIZE) < ds))
		return MLX5_TXCMP_CODE_ERROR;
	/*
	 * Some Tx offloads may cause an error if packet is not long enough,
	 * check against assumed minimal length.
	 */
	if (rte_pktmbuf_pkt_len(loc->mbuf) <= MLX5_ESEG_MIN_INLINE_SIZE)
		return MLX5_TXCMP_CODE_ERROR;
#ifdef MLX5_PMD_SOFT_COUNTERS
	/* Update sent data bytes counter. */
	qp_txq->stats.obytes += rte_pktmbuf_pkt_len(loc->mbuf);
	if (MLX5_TXOFF_CONFIG(VLAN) &&
	    loc->mbuf->ol_flags & RTE_MBUF_F_TX_VLAN)
		qp_txq->stats.obytes += sizeof(struct rte_vlan_hdr);
#endif
	/*
	 * SEND WQE, one WQEBB:
	 * - Control Segment, SEND opcode
	 * - Ethernet Segment, optional VLAN, no inline
	 * - Data Segments, pointer only type
	 */
	wqe = qp_txq->sq_wqes + (qp_txq->sq_wqe_ci & qp_txq->sq_wqe_m);
	loc->wqe_last = wqe;
	mlx5_qp_tx_cseg_init(qp_txq, loc, wqe, ds, MLX5_OPCODE_SEND, olx);
	rte_pmd_mlx5_trace_tx_push(loc->mbuf, qp_txq->sq_wqe_ci);
	mlx5_qp_tx_eseg_none(qp_txq, loc, wqe, olx);
	dseg = &wqe->dseg[0];
	do {
		if (unlikely(!rte_pktmbuf_data_len(loc->mbuf))) {
			struct rte_mbuf *mbuf;

			/*
			 * Zero length segment found, have to correct total
			 * size of WQE in segments.
			 * It is supposed to be rare occasion, so in normal
			 * case (no zero length segments) we avoid extra
			 * writing to the Control Segment.
			 */
			--ds;
			wqe->cseg.sq_ds -= RTE_BE32(1);
			mbuf = loc->mbuf;
			loc->mbuf = mbuf->next;
			rte_pktmbuf_free_seg(mbuf);
			if (--nseg == 0)
				break;
		} else {
			mlx5_qp_tx_dseg_ptr
				(qp_txq, loc, dseg,
				 rte_pktmbuf_mtod(loc->mbuf, uint8_t *),
				 rte_pktmbuf_data_len(loc->mbuf), olx);
			qp_txq->sq_elts[qp_txq->sq_elts_head++ & qp_txq->sq_elts_m] = loc->mbuf;
			--loc->elts_free;
			if (--nseg == 0)
				break;
			++dseg;
			if ((uintptr_t)dseg >= (uintptr_t)qp_txq->sq_wqes_end)
				dseg = (struct mlx5_wqe_dseg *)qp_txq->sq_wqes;
			loc->mbuf = loc->mbuf->next;
		}
	} while (true);
	qp_txq->sq_wqe_ci += (ds + 3) / 4;
	loc->wqe_free -= (ds + 3) / 4;
	return MLX5_TXCMP_CODE_MULTI;
}

/**
 * Tx one packet function for multi-segment SEND. Supports all
 * types of Tx offloads, uses MLX5_OPCODE_SEND to build WQEs,
 * sends one packet per WQE, with data inlining in
 * Ethernet Segment and minimal Data Segments.
 *
 * This routine is responsible for storing processed mbuf
 * into elts ring buffer and update elts_head.
 *
 * @param txq
 *   Pointer to TX queue structure.
 * @param loc
 *   Pointer to burst routine local context.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 *
 * @return
 *   MLX5_TXCMP_CODE_EXIT - sending is done or impossible.
 *   MLX5_TXCMP_CODE_ERROR - some unrecoverable error occurred.
 * Local context variables partially updated.
 */
static __rte_always_inline enum mlx5_txcmp_code
mlx5_qp_tx_packet_multi_inline(struct mlx5_qp_data *__rte_restrict qp_txq,
			    struct mlx5_qp_txq_local *__rte_restrict loc,
			    unsigned int olx)
{
	struct mlx5_wqe *__rte_restrict wqe;
	unsigned int ds, inlen, dlen, vlan = 0;

	MLX5_ASSERT(MLX5_TXOFF_CONFIG(INLINE));
	MLX5_ASSERT(NB_SEGS(loc->mbuf) > 1);
	MLX5_ASSERT(loc->elts_free >= NB_SEGS(loc->mbuf));
	/*
	 * First calculate data length to be inlined
	 * to estimate the required space for WQE.
	 */
	dlen = rte_pktmbuf_pkt_len(loc->mbuf);
	if (MLX5_TXOFF_CONFIG(VLAN) && loc->mbuf->ol_flags & RTE_MBUF_F_TX_VLAN)
		vlan = sizeof(struct rte_vlan_hdr);
	inlen = dlen + vlan;
	/* Check against minimal length. */
	if (inlen <= MLX5_ESEG_MIN_INLINE_SIZE)
		return MLX5_TXCMP_CODE_ERROR;
	MLX5_ASSERT(qp_txq->inlen_send >= MLX5_ESEG_MIN_INLINE_SIZE);
	if (inlen > qp_txq->inlen_send ||
	    loc->mbuf->ol_flags & RTE_MBUF_F_TX_DYNF_NOINLINE) {
		struct rte_mbuf *mbuf;
		unsigned int nxlen;
		uintptr_t start;

		mbuf = loc->mbuf;
		nxlen = rte_pktmbuf_data_len(mbuf) + vlan;
		/*
		 * Packet length exceeds the allowed inline data length,
		 * check whether the minimal inlining is required.
		 */
		if (qp_txq->inlen_mode) {
			MLX5_ASSERT(qp_txq->inlen_mode >=
				    MLX5_ESEG_MIN_INLINE_SIZE);
			MLX5_ASSERT(qp_txq->inlen_mode <= qp_txq->inlen_send);
			inlen = RTE_MIN(qp_txq->inlen_mode, inlen);
		} else if (vlan && !qp_txq->vlan_en) {
			/*
			 * VLAN insertion is requested and hardware does not
			 * support the offload, will do with software inline.
			 */
			inlen = MLX5_ESEG_MIN_INLINE_SIZE;
		} else if (mbuf->ol_flags & RTE_MBUF_F_TX_DYNF_NOINLINE ||
			   nxlen > qp_txq->inlen_send) {
			return mlx5_qp_tx_packet_multi_send(qp_txq, loc, olx);
		} else if (nxlen <= MLX5_ESEG_MIN_INLINE_SIZE) {
			inlen = MLX5_ESEG_MIN_INLINE_SIZE;
		} else {
			goto do_first;
		}
		if (mbuf->ol_flags & RTE_MBUF_F_TX_DYNF_NOINLINE)
			goto do_build;
		/*
		 * Now we know the minimal amount of data is requested
		 * to inline. Check whether we should inline the buffers
		 * from the chain beginning to eliminate some mbufs.
		 */
		if (unlikely(nxlen <= qp_txq->inlen_send)) {
			/* We can inline first mbuf at least. */
			if (nxlen < inlen) {
				unsigned int smlen;

				/* Scan mbufs till inlen filled. */
				do {
					smlen = nxlen;
					mbuf = NEXT(mbuf);
					MLX5_ASSERT(mbuf);
					nxlen = rte_pktmbuf_data_len(mbuf);
					nxlen += smlen;
				} while (unlikely(nxlen < inlen));
				if (unlikely(nxlen > qp_txq->inlen_send)) {
					/* We cannot inline entire mbuf. */
					smlen = inlen - smlen;
					start = rte_pktmbuf_mtod_offset
						    (mbuf, uintptr_t, smlen);
					goto do_align;
				}
			}
do_first:
			do {
				inlen = nxlen;
				mbuf = NEXT(mbuf);
				/* There should be not end of packet. */
				MLX5_ASSERT(mbuf);
				if (mbuf->ol_flags & RTE_MBUF_F_TX_DYNF_NOINLINE)
					break;
				nxlen = inlen + rte_pktmbuf_data_len(mbuf);
			} while (unlikely(nxlen < qp_txq->inlen_send));
		}
		start = rte_pktmbuf_mtod(mbuf, uintptr_t);
		/*
		 * Check whether we can do inline to align start
		 * address of data buffer to cacheline.
		 */
do_align:
		start = (~start + 1) & (RTE_CACHE_LINE_SIZE - 1);
		if (unlikely(start)) {
			start += inlen;
			if (start <= qp_txq->inlen_send)
				inlen = start;
		}
	}
	/*
	 * Check whether there are enough free WQEBBs:
	 * - Control Segment
	 * - Ethernet Segment
	 * - First Segment of inlined Ethernet data
	 * - ... data continued ...
	 * - Data Segments of pointer/min inline type
	 *
	 * Estimate the number of Data Segments conservatively,
	 * supposing no any mbufs is being freed during inlining.
	 */
do_build:
	if (MLX5_TXOFF_CONFIG(TXPP)) {
		enum mlx5_txcmp_code wret;

		/* Generate WAIT for scheduling if requested. */
		wret = mlx5_qp_tx_schedule_send(qp_txq, loc, 0, olx);
		if (wret == MLX5_TXCMP_CODE_EXIT)
			return MLX5_TXCMP_CODE_EXIT;
		if (wret == MLX5_TXCMP_CODE_ERROR)
			return MLX5_TXCMP_CODE_ERROR;
	}
	MLX5_ASSERT(inlen <= qp_txq->inlen_send);
	ds = NB_SEGS(loc->mbuf) + 2 + (inlen -
				       MLX5_ESEG_MIN_INLINE_SIZE +
				       MLX5_WSEG_SIZE +
				       MLX5_WSEG_SIZE - 1) / MLX5_WSEG_SIZE;
	if (unlikely(loc->wqe_free < ((ds + 3) / 4)))
		return MLX5_TXCMP_CODE_EXIT;
	/* Check for maximal WQE size. */
	if (unlikely((MLX5_WQE_SIZE_MAX / MLX5_WSEG_SIZE) < ds)) {
		/*  Check if we can adjust the inline length. */
		if (unlikely(qp_txq->inlen_mode)) {
			ds = NB_SEGS(loc->mbuf) + 2 +
				(qp_txq->inlen_mode -
				MLX5_ESEG_MIN_INLINE_SIZE +
				MLX5_WSEG_SIZE +
				MLX5_WSEG_SIZE - 1) / MLX5_WSEG_SIZE;
			if (unlikely((MLX5_WQE_SIZE_MAX / MLX5_WSEG_SIZE) < ds))
				return MLX5_TXCMP_CODE_ERROR;
		}
		/* We have lucky opportunity to adjust. */
		inlen = RTE_MIN(inlen, MLX5_WQE_SIZE_MAX -
				       MLX5_WSEG_SIZE * 2 -
				       MLX5_WSEG_SIZE * NB_SEGS(loc->mbuf) -
				       MLX5_WSEG_SIZE +
				       MLX5_ESEG_MIN_INLINE_SIZE);
	}
#ifdef MLX5_PMD_SOFT_COUNTERS
	/* Update sent data bytes/packets counters. */
	qp_txq->stats.obytes += dlen + vlan;
#endif
	wqe = qp_txq->sq_wqes + (qp_txq->sq_wqe_ci & qp_txq->sq_wqe_m);
	loc->wqe_last = wqe;
	mlx5_qp_tx_cseg_init(qp_txq, loc, wqe, 0, MLX5_OPCODE_SEND, olx);
	rte_pmd_mlx5_trace_tx_push(loc->mbuf, qp_txq->sq_wqe_ci);
	ds = mlx5_qp_tx_mseg_build(qp_txq, loc, wqe, vlan, inlen, 0, olx);
	wqe->cseg.sq_ds = rte_cpu_to_be_32(qp_txq->qp_num_8s | ds);
	qp_txq->sq_wqe_ci += (ds + 3) / 4;
	loc->wqe_free -= (ds + 3) / 4;
	return MLX5_TXCMP_CODE_MULTI;
}

/**
 * Tx burst function for multi-segment packets. Supports all
 * types of Tx offloads, uses MLX5_OPCODE_SEND/TSO to build WQEs,
 * sends one packet per WQE. Function stops sending if it
 * encounters the single-segment packet.
 *
 * This routine is responsible for storing processed mbuf
 * into elts ring buffer and update elts_head.
 *
 * @param txq
 *   Pointer to TX queue structure.
 * @param[in] pkts
 *   Packets to transmit.
 * @param pkts_n
 *   Number of packets in array.
 * @param loc
 *   Pointer to burst routine local context.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 *
 * @return
 *   MLX5_TXCMP_CODE_EXIT - sending is done or impossible.
 *   MLX5_TXCMP_CODE_ERROR - some unrecoverable error occurred.
 *   MLX5_TXCMP_CODE_SINGLE - single-segment packet encountered.
 *   MLX5_TXCMP_CODE_TSO - TSO single-segment packet encountered.
 * Local context variables updated.
 */
static __rte_always_inline enum mlx5_txcmp_code
mlx5_qp_tx_burst_mseg(struct mlx5_qp_data *__rte_restrict qp_txq,
		   struct rte_mbuf **__rte_restrict pkts,
		   unsigned int pkts_n,
		   struct mlx5_qp_txq_local *__rte_restrict loc,
		   unsigned int olx)
{
	MLX5_ASSERT(loc->elts_free && loc->wqe_free);
	MLX5_ASSERT(pkts_n > loc->pkts_sent);
	pkts += loc->pkts_sent + 1;
	pkts_n -= loc->pkts_sent;
	for (;;) {
		enum mlx5_txcmp_code ret;

		MLX5_ASSERT(NB_SEGS(loc->mbuf) > 1);
		/*
		 * Estimate the number of free elts quickly but conservatively.
		 * Some segment may be fully inlined and freed,
		 * ignore this here - precise estimation is costly.
		 */
		if (loc->elts_free < NB_SEGS(loc->mbuf))
			return MLX5_TXCMP_CODE_EXIT;
		if (MLX5_TXOFF_CONFIG(TSO) &&
		    unlikely(loc->mbuf->ol_flags & RTE_MBUF_F_TX_TCP_SEG)) {
			/* Proceed with multi-segment TSO. */
			ret = mlx5_qp_tx_packet_multi_tso(qp_txq, loc, olx);
		} else if (MLX5_TXOFF_CONFIG(INLINE)) {
			/* Proceed with multi-segment SEND with inlining. */
			ret = mlx5_qp_tx_packet_multi_inline(qp_txq, loc, olx);
		} else {
			/* Proceed with multi-segment SEND w/o inlining. */
			ret = mlx5_qp_tx_packet_multi_send(qp_txq, loc, olx);
		}
		if (ret == MLX5_TXCMP_CODE_EXIT)
			return MLX5_TXCMP_CODE_EXIT;
		if (ret == MLX5_TXCMP_CODE_ERROR)
			return MLX5_TXCMP_CODE_ERROR;
		/* WQE is built, go to the next packet. */
		++loc->pkts_sent;
		--pkts_n;
		if (unlikely(!pkts_n || !loc->elts_free || !loc->wqe_free))
			return MLX5_TXCMP_CODE_EXIT;
		loc->mbuf = *pkts++;
		if (pkts_n > 1)
			rte_prefetch0(*pkts);
		if (likely(NB_SEGS(loc->mbuf) > 1))
			continue;
		/* Here ends the series of multi-segment packets. */
		if (MLX5_TXOFF_CONFIG(TSO) &&
		    unlikely(loc->mbuf->ol_flags & RTE_MBUF_F_TX_TCP_SEG))
			return MLX5_TXCMP_CODE_TSO;
		return MLX5_TXCMP_CODE_SINGLE;
	}
	MLX5_ASSERT(false);
}


/**
 * Tx burst function for single-segment packets with TSO.
 * Supports all types of Tx offloads, except multi-packets.
 * Uses MLX5_OPCODE_TSO to build WQEs, sends one packet per WQE.
 * Function stops sending if it encounters the multi-segment
 * packet or packet without TSO requested.
 *
 * The routine is responsible for storing processed mbuf into elts ring buffer
 * and update elts_head if inline offloads is requested due to possible early
 * freeing of the inlined mbufs (can not store pkts array in elts as a batch).
 *
 * @param txq
 *   Pointer to TX queue structure.
 * @param[in] pkts
 *   Packets to transmit.
 * @param pkts_n
 *   Number of packets in array.
 * @param loc
 *   Pointer to burst routine local context.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 *
 * @return
 *   MLX5_TXCMP_CODE_EXIT - sending is done or impossible.
 *   MLX5_TXCMP_CODE_ERROR - some unrecoverable error occurred.
 *   MLX5_TXCMP_CODE_SINGLE - single-segment packet encountered.
 *   MLX5_TXCMP_CODE_MULTI - multi-segment packet encountered.
 * Local context variables updated.
 */
static __rte_always_inline enum mlx5_txcmp_code
mlx5_qp_tx_burst_tso(struct mlx5_qp_data *__rte_restrict qp_txq,
		  struct rte_mbuf **__rte_restrict pkts,
		  unsigned int pkts_n,
		  struct mlx5_qp_txq_local *__rte_restrict loc,
		  unsigned int olx)
{
	MLX5_ASSERT(loc->elts_free && loc->wqe_free);
	MLX5_ASSERT(pkts_n > loc->pkts_sent);
	pkts += loc->pkts_sent + 1;
	pkts_n -= loc->pkts_sent;
	for (;;) {
		struct mlx5_wqe_dseg *__rte_restrict dseg;
		struct mlx5_wqe *__rte_restrict wqe;
		unsigned int ds, dlen, hlen, ntcp, vlan = 0;
		uint8_t *dptr;

		MLX5_ASSERT(NB_SEGS(loc->mbuf) == 1);
		if (MLX5_TXOFF_CONFIG(TXPP)) {
			enum mlx5_txcmp_code wret;

			/* Generate WAIT for scheduling if requested. */
			wret = mlx5_qp_tx_schedule_send(qp_txq, loc, 1, olx);
			if (wret == MLX5_TXCMP_CODE_EXIT)
				return MLX5_TXCMP_CODE_EXIT;
			if (wret == MLX5_TXCMP_CODE_ERROR)
				return MLX5_TXCMP_CODE_ERROR;
		}
		dlen = rte_pktmbuf_data_len(loc->mbuf);
		if (MLX5_TXOFF_CONFIG(VLAN) &&
		    loc->mbuf->ol_flags & RTE_MBUF_F_TX_VLAN) {
			vlan = sizeof(struct rte_vlan_hdr);
		}
		/*
		 * First calculate the WQE size to check
		 * whether we have enough space in ring buffer.
		 */
		hlen = loc->mbuf->l2_len + vlan +
		       loc->mbuf->l3_len + loc->mbuf->l4_len;
		if (unlikely((!hlen || !loc->mbuf->tso_segsz)))
			return MLX5_TXCMP_CODE_ERROR;
		if (loc->mbuf->ol_flags & RTE_MBUF_F_TX_TUNNEL_MASK)
			hlen += loc->mbuf->outer_l2_len +
				loc->mbuf->outer_l3_len;
		/* Segment must contain all TSO headers. */
		if (unlikely(hlen > MLX5_MAX_TSO_HEADER ||
			     hlen <= MLX5_ESEG_MIN_INLINE_SIZE ||
			     hlen > (dlen + vlan)))
			return MLX5_TXCMP_CODE_ERROR;
		/*
		 * Check whether there are enough free WQEBBs:
		 * - Control Segment
		 * - Ethernet Segment
		 * - First Segment of inlined Ethernet data
		 * - ... data continued ...
		 * - Finishing Data Segment of pointer type
		 */
		ds = 4 + (hlen - MLX5_ESEG_MIN_INLINE_SIZE +
			  MLX5_WSEG_SIZE - 1) / MLX5_WSEG_SIZE;
		if (loc->wqe_free < ((ds + 3) / 4))
			return MLX5_TXCMP_CODE_EXIT;
#ifdef MLX5_PMD_SOFT_COUNTERS
		/* Update sent data bytes/packets counters. */
		ntcp = (dlen + vlan - hlen +
			loc->mbuf->tso_segsz - 1) /
			loc->mbuf->tso_segsz;
		/*
		 * One will be added for mbuf itself at the end
		 * of the mlx5_tx_burst from loc->pkts_sent field.
		 */
		--ntcp;
		qp_txq->stats.opackets += ntcp;
		qp_txq->stats.obytes += dlen + vlan + ntcp * hlen;
#endif
		/*
		 * Build the TSO WQE:
		 * - Control Segment
		 * - Ethernet Segment with hlen bytes inlined
		 * - Data Segment of pointer type
		 */
		wqe = qp_txq->sq_wqes + (qp_txq->sq_wqe_ci & qp_txq->sq_wqe_m);
		loc->wqe_last = wqe;
		mlx5_qp_tx_cseg_init(qp_txq, loc, wqe, ds, MLX5_OPCODE_TSO, olx);
		rte_pmd_mlx5_trace_tx_push(loc->mbuf, qp_txq->sq_wqe_ci);
		dseg = mlx5_qp_tx_eseg_data(qp_txq, loc, wqe, vlan, hlen, 1, olx);
		dptr = rte_pktmbuf_mtod(loc->mbuf, uint8_t *) + hlen - vlan;
		dlen -= hlen - vlan;
		mlx5_qp_tx_dseg_ptr(qp_txq, loc, dseg, dptr, dlen, olx);
		/*
		 * WQE is built, update the loop parameters
		 * and go to the next packet.
		 */
		qp_txq->sq_wqe_ci += (ds + 3) / 4;
		loc->wqe_free -= (ds + 3) / 4;
		if (MLX5_TXOFF_CONFIG(INLINE))
			qp_txq->sq_elts[qp_txq->sq_elts_head++ & qp_txq->sq_elts_m] = loc->mbuf;
		--loc->elts_free;
		++loc->pkts_sent;
		--pkts_n;
		if (unlikely(!pkts_n || !loc->elts_free || !loc->wqe_free))
			return MLX5_TXCMP_CODE_EXIT;
		loc->mbuf = *pkts++;
		if (pkts_n > 1)
			rte_prefetch0(*pkts);
		if (MLX5_TXOFF_CONFIG(MULTI) &&
		    unlikely(NB_SEGS(loc->mbuf) > 1))
			return MLX5_TXCMP_CODE_MULTI;
		if (likely(!(loc->mbuf->ol_flags & RTE_MBUF_F_TX_TCP_SEG)))
			return MLX5_TXCMP_CODE_SINGLE;
		/* Continue with the next TSO packet. */
	}
	MLX5_ASSERT(false);
}

/**
 * DPDK Tx callback template. This is configured template used to generate
 * routines optimized for specified offload setup.
 * One of this generated functions is chosen at SQ configuration time.
 *
 * @param txq
 *   Generic pointer to TX queue structure.
 * @param[in] pkts
 *   Packets to transmit.
 * @param pkts_n
 *   Number of packets in array.
 * @param olx
 *   Configured offloads mask, presents the bits of MLX5_TXOFF_CONFIG_xxx
 *   values. Should be static to take compile time static configuration
 *   advantages.
 *
 * @return
 *   Number of packets successfully transmitted (<= pkts_n).
 */
static __rte_always_inline uint16_t
mlx5_qp_tx_burst_tmpl(struct mlx5_qp_data *__rte_restrict qp_txq,
		   struct rte_mbuf **__rte_restrict pkts,
		   uint16_t pkts_n,
		   unsigned int olx)
{
	struct mlx5_qp_txq_local loc;
	enum mlx5_txcmp_code ret;
	unsigned int part;

	MLX5_ASSERT(qp_txq->sq_elts_s >= (uint16_t)(qp_txq->sq_elts_head - qp_txq->sq_elts_tail));
	MLX5_ASSERT(qp_txq->sq_wqe_s >= (uint16_t)(qp_txq->sq_wqe_ci - qp_txq->sq_wqe_pi));
	if (unlikely(!pkts_n))
		return 0;
	if (MLX5_TXOFF_CONFIG(INLINE))
		loc.mbuf_free = 0;
	loc.pkts_sent = 0;
	loc.pkts_copy = 0;
	loc.wqe_last = NULL;

send_loop:
	loc.pkts_loop = loc.pkts_sent;
	/*
	 * Check if there are some CQEs, if any:
	 * - process an encountered errors
	 * - process the completed WQEs
	 * - free related mbufs
	 * - doorbell the NIC about processed CQEs
	 */
	rte_prefetch0(*(pkts + loc.pkts_sent));
	mlx5_qp_tx_handle_completion(qp_txq, olx);
	/*
	 * Calculate the number of available resources - elts and WQEs.
	 * There are two possible different scenarios:
	 * - no data inlining into WQEs, one WQEBB may contains up to
	 *   four packets, in this case elts become scarce resource
	 * - data inlining into WQEs, one packet may require multiple
	 *   WQEBBs, the WQEs become the limiting factor.
	 */
	MLX5_ASSERT(qp_txq->sq_elts_s >= (uint16_t)(qp_txq->sq_elts_head - qp_txq->sq_elts_tail));
	loc.elts_free = qp_txq->sq_elts_s -
				(uint16_t)(qp_txq->sq_elts_head - qp_txq->sq_elts_tail);
	MLX5_ASSERT(qp_txq->sq_wqe_s >= (uint16_t)(qp_txq->sq_wqe_ci - qp_txq->sq_wqe_pi));
	loc.wqe_free = qp_txq->sq_wqe_s -
				(uint16_t)(qp_txq->sq_wqe_ci - qp_txq->sq_wqe_pi);
	if (unlikely(!loc.elts_free || !loc.wqe_free))
		goto burst_exit;
	for (;;) {
		/*
		 * Fetch the packet from array. Usually this is the first
		 * packet in series of multi/single segment packets.
		 */
		loc.mbuf = *(pkts + loc.pkts_sent);
		/* Dedicated branch for multi-segment packets. */
		if (MLX5_TXOFF_CONFIG(MULTI) &&
		    unlikely(NB_SEGS(loc.mbuf) > 1)) {
			/*
			 * Multi-segment packet encountered.
			 * Hardware is able to process it only
			 * with SEND/TSO opcodes, one packet
			 * per WQE, do it in dedicated routine.
			 */
enter_send_multi:
			MLX5_ASSERT(loc.pkts_sent >= loc.pkts_copy);
			part = loc.pkts_sent - loc.pkts_copy;
			if (!MLX5_TXOFF_CONFIG(INLINE) && part) {
				/*
				 * There are some single-segment mbufs not
				 * stored in elts. The mbufs must be in the
				 * same order as WQEs, so we must copy the
				 * mbufs to elts here, before the coming
				 * multi-segment packet mbufs is appended.
				 */
				mlx5_qp_tx_copy_elts(qp_txq, pkts + loc.pkts_copy,
						  part, olx);
				loc.pkts_copy = loc.pkts_sent;
			}
			MLX5_ASSERT(pkts_n > loc.pkts_sent);
			ret = mlx5_qp_tx_burst_mseg(qp_txq, pkts, pkts_n, &loc, olx);
			if (!MLX5_TXOFF_CONFIG(INLINE))
				loc.pkts_copy = loc.pkts_sent;
			/*
			 * These returned code checks are supposed
			 * to be optimized out due to routine inlining.
			 */
			if (ret == MLX5_TXCMP_CODE_EXIT) {
				/*
				 * The routine returns this code when
				 * all packets are sent or there is no
				 * enough resources to complete request.
				 */
				break;
			}
			if (ret == MLX5_TXCMP_CODE_ERROR) {
				/*
				 * The routine returns this code when some error
				 * in the incoming packets format occurred.
				 */
				qp_txq->stats.oerrors++;
				break;
			}
			if (ret == MLX5_TXCMP_CODE_SINGLE) {
				/*
				 * The single-segment packet was encountered
				 * in the array, try to send it with the
				 * best optimized way, possible engaging eMPW.
				 */
				goto enter_send_single;
			}
			if (MLX5_TXOFF_CONFIG(TSO) &&
			    ret == MLX5_TXCMP_CODE_TSO) {
				/*
				 * The single-segment TSO packet was
				 * encountered in the array.
				 */
				goto enter_send_tso;
			}
			/* We must not get here. Something is going wrong. */
			MLX5_ASSERT(false);
			qp_txq->stats.oerrors++;
			break;
		}
		/* Dedicated branch for single-segment TSO packets. */
		if (MLX5_TXOFF_CONFIG(TSO) &&
		    unlikely(loc.mbuf->ol_flags & RTE_MBUF_F_TX_TCP_SEG)) {
			/*
			 * TSO might require special way for inlining
			 * (dedicated parameters) and is sent with
			 * MLX5_OPCODE_TSO opcode only, provide this
			 * in dedicated branch.
			 */
enter_send_tso:
			MLX5_ASSERT(NB_SEGS(loc.mbuf) == 1);
			MLX5_ASSERT(pkts_n > loc.pkts_sent);
			ret = mlx5_qp_tx_burst_tso(qp_txq, pkts, pkts_n, &loc, olx);
			/*
			 * These returned code checks are supposed
			 * to be optimized out due to routine inlining.
			 */
			if (ret == MLX5_TXCMP_CODE_EXIT)
				break;
			if (ret == MLX5_TXCMP_CODE_ERROR) {
				qp_txq->stats.oerrors++;
				break;
			}
			if (ret == MLX5_TXCMP_CODE_SINGLE)
				goto enter_send_single;
			if (MLX5_TXOFF_CONFIG(MULTI) &&
			    ret == MLX5_TXCMP_CODE_MULTI) {
				/*
				 * The multi-segment packet was
				 * encountered in the array.
				 */
				goto enter_send_multi;
			}
			/* We must not get here. Something is going wrong. */
			MLX5_ASSERT(false);
			qp_txq->stats.oerrors++;
			break;
		}
		/*
		 * The dedicated branch for the single-segment packets
		 * without TSO. Often these ones can be sent using
		 * MLX5_OPCODE_EMPW with multiple packets in one WQE.
		 * The routine builds the WQEs till it encounters
		 * the TSO or multi-segment packet (in case if these
		 * offloads are requested at SQ configuration time).
		 */
enter_send_single:
		MLX5_ASSERT(pkts_n > loc.pkts_sent);
		ret = mlx5_tx_burst_single(txq, pkts, pkts_n, &loc, olx);
		/*
		 * These returned code checks are supposed
		 * to be optimized out due to routine inlining.
		 */
		if (ret == MLX5_TXCMP_CODE_EXIT)
			break;
		if (ret == MLX5_TXCMP_CODE_ERROR) {
			qp_txq->stats.oerrors++;
			break;
		}
		if (MLX5_TXOFF_CONFIG(MULTI) &&
		    ret == MLX5_TXCMP_CODE_MULTI) {
			/*
			 * The multi-segment packet was
			 * encountered in the array.
			 */
			goto enter_send_multi;
		}
		if (MLX5_TXOFF_CONFIG(TSO) &&
		    ret == MLX5_TXCMP_CODE_TSO) {
			/*
			 * The single-segment TSO packet was
			 * encountered in the array.
			 */
			goto enter_send_tso;
		}
		/* We must not get here. Something is going wrong. */
		MLX5_ASSERT(false);
		qp_txq->stats.oerrors++;
		break;
	}
	/*
	 * Main Tx loop is completed, do the rest:
	 * - set completion request if thresholds are reached
	 * - doorbell the hardware
	 * - copy the rest of mbufs to elts (if any)
	 */
	MLX5_ASSERT(MLX5_TXOFF_CONFIG(INLINE) ||
		    loc.pkts_sent >= loc.pkts_copy);
	/* Take a shortcut if nothing is sent. */
	if (unlikely(loc.pkts_sent == loc.pkts_loop))
		goto burst_exit;
	/* Request CQE generation if limits are reached. */
	if (MLX5_TXOFF_CONFIG(TXPP) && __rte_trace_point_fp_is_enabled())
		mlx5_tx_request_completion_trace(txq, &loc, olx);
	else
		mlx5_tx_request_completion(txq, &loc, olx);
	/*
	 * Ring QP doorbell immediately after WQE building completion
	 * to improve latencies. The pure software related data treatment
	 * can be completed after doorbell. Tx CQEs for this SQ are
	 * processed in this thread only by the polling.
	 *
	 * The rdma core library can map doorbell register in two ways,
	 * depending on the environment variable "MLX5_SHUT_UP_BF":
	 *
	 * - as regular cached memory, the variable is either missing or
	 *   set to zero. This type of mapping may cause the significant
	 *   doorbell register writing latency and requires explicit memory
	 *   write barrier to mitigate this issue and prevent write combining.
	 *
	 * - as non-cached memory, the variable is present and set to not "0"
	 *   value. This type of mapping may cause performance impact under
	 *   heavy loading conditions but the explicit write memory barrier is
	 *   not required and it may improve core performance.
	 *
	 * - the legacy behaviour (prior 19.08 release) was to use some
	 *   heuristics to decide whether write memory barrier should
	 *   be performed. This behavior is supported with specifying
	 *   tx_db_nc=2, write barrier is skipped if application provides
	 *   the full recommended burst of packets, it supposes the next
	 *   packets are coming and the write barrier will be issued on
	 *   the next burst (after descriptor writing, at least).
	 */
	mlx5_doorbell_ring(mlx5_tx_bfreg(qp_txq),
			   *(volatile uint64_t *)loc.wqe_last, qp_txq->sq_wqe_ci,
			   qp_txq->sq_db, !qp_txq->db_nc &&
			   (!qp_txq->db_heu || pkts_n % MLX5_TX_DEFAULT_BURST));
	/* Not all of the mbufs may be stored into elts yet. */
	part = MLX5_TXOFF_CONFIG(INLINE) ? 0 : loc.pkts_sent - loc.pkts_copy;
	if (!MLX5_TXOFF_CONFIG(INLINE) && part) {
		/*
		 * There are some single-segment mbufs not stored in elts.
		 * It can be only if the last packet was single-segment.
		 * The copying is gathered into one place due to it is
		 * a good opportunity to optimize that with SIMD.
		 * Unfortunately if inlining is enabled the gaps in pointer
		 * array may happen due to early freeing of the inlined mbufs.
		 */
		mlx5_tx_copy_elts(qp_txq, pkts + loc.pkts_copy, part, olx);
		loc.pkts_copy = loc.pkts_sent;
	}
	MLX5_ASSERT(qp_txq->sq_elts_s >= (uint16_t)(qp_txq->sq_elts_head - qp_txq->sq_elts_tail));
	MLX5_ASSERT(qp_txq->sq_wqe_s >= (uint16_t)(qp_txq->sq_wqe_ci - qp_txq->sq_wqe_pi));
	if (pkts_n > loc.pkts_sent) {
		/*
		 * If burst size is large there might be no enough CQE
		 * fetched from completion queue and no enough resources
		 * freed to send all the packets.
		 */
		goto send_loop;
	}
burst_exit:
#ifdef MLX5_PMD_SOFT_COUNTERS
	/* Increment sent packets counter. */
	qp_txq->stats.opackets += loc.pkts_sent;
#endif
	if (MLX5_TXOFF_CONFIG(INLINE) && loc.mbuf_free)
		__mlx5_tx_free_mbuf(qp_txq, pkts, loc.mbuf_free, olx);
	/* Trace productive bursts only. */
	if (__rte_trace_point_fp_is_enabled() && loc.pkts_sent)
		rte_pmd_mlx5_trace_tx_exit(mlx5_read_pcibar_clock_from_txq(qp_txq),
					   loc.pkts_sent, pkts_n);
	return loc.pkts_sent;
}


struct mlx5_qp_ctrl * mlx5_qp_get(struct rte_eth_dev *dev, uint16_t idx);
void qp_alloc_elts(struct mlx5_qp_ctrl *qp_ctrl);
int mlx5_qp_releasable(struct rte_eth_dev *dev, uint16_t idx);
int mlx5_qp_release(struct rte_eth_dev *dev, uint16_t idx);
struct mlx5_qp_ctrl * mlx5_qp_new(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc, unsigned int socket, const struct rte_eth_txconf *conf);
int mlx5_qp_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc, unsigned int socket, const struct rte_eth_txconf *conf);
void mlx5_qp_queue_release(struct rte_eth_dev *dev, uint16_t qid);

#endif /* RTE_PMD_MLX5_QP_H_ */
