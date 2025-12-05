#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>

#include <ethdev_driver.h>
#include "mlx5.h"
#include "rte_common.h"
#include "rte_stdatomic.h"
#include "mlx5_qp.h"

static inline struct mlx5_qp_ctrl *
mlx5_qp_get(struct rte_eth_dev *dev, uint16_t idx)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	struct mlx5_qp_data *qp_data = (*priv->qps)[idx];
	struct mlx5_qp_ctrl *ctrl = NULL;


	if (qp_data) {
		ctrl = container_of(qp_data, struct mlx5_qp_ctrl, qp);
		rte_atomic_fetch_add_explicit(&ctrl->refcnt,1,rte_memory_order_relaxed);
	}

	return ctrl;
}

/**
 * Allocate QP elements.
 *
 * @param qp_ctrl
 *   Pointer to QP structure.
 */
void
qp_alloc_elts(struct mlx5_qp_ctrl *qp_ctrl)
{
	const unsigned int elts_n = 1 << qp_ctrl->qp.sq_elts_n;
	unsigned int i;

	for (i = 0; (i != elts_n); ++i)
		qp_ctrl->qp.sq_elts[i] = NULL;
	DRV_LOG(DEBUG, "port %u QP %u allocated and configured %u WRs",
		PORT_ID(qp_ctrl->priv), qp_ctrl->qp.qp_idx, elts_n);
	qp_ctrl->qp.sq_elts_head = 0;
	qp_ctrl->qp.sq_elts_tail = 0;
	qp_ctrl->qp.sq_elts_comp = 0;
}

/**
 * Release QP.
 *
 * @param dev
 *   Pointer to Ethernet device.
 * @param idx
 *   QP index.
 *
 * @return
 *   1 while a reference on it exists, 0 when freed.
 */
int
mlx5_qp_release(struct rte_eth_dev *dev, uint16_t idx)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	struct mlx5_qp_ctrl *qp_ctrl;

	if (priv->qps == NULL || (*priv->qps)[idx] == NULL)
		return 0;
	qp_ctrl = container_of((*priv->qps)[idx], struct mlx5_qp_ctrl, qp);
	if (rte_atomic_fetch_sub_explicit(&qp_ctrl->refcnt, 1, rte_memory_order_relaxed) - 1 > 1)
		return 1;
	if (qp_ctrl->obj) {
		priv->obj_ops.qp_obj_release(qp_ctrl->obj);
		LIST_REMOVE(qp_ctrl->obj, next);
		mlx5_free(qp_ctrl->obj);
		qp_ctrl->obj = NULL;
	}
	if (!rte_atomic_load_explicit(&qp_ctrl->refcnt, rte_memory_order_relaxed)) {
		LIST_REMOVE(qp_ctrl, next);
		mlx5_free(qp_ctrl);
		(*priv->qps)[idx] = NULL;
	}
	return 0;
}

/**
 * Verify if the QP can be released.
 *
 * @param dev
 *   Pointer to Ethernet device.
 * @param idx
 *   QP index.
 *
 * @return
 *   1 if the queue can be released.
 */
int
mlx5_qp_releasable(struct rte_eth_dev *dev, uint16_t idx)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	struct mlx5_qp_ctrl *qp;

	if (!(*priv->qps)[idx])
		return -1;
	qp = container_of((*priv->qps)[idx], struct mlx5_qp_ctrl, qp);
	return (rte_atomic_load_explicit(&qp->refcnt, rte_memory_order_relaxed) == 1);
}

/**
 * QP  presetup checks.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param idx
 *   QP index.
 * @param desc
 *   Number of descriptors to configure in queue.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
static int
mlx5_qp_pre_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t *desc)
{
	struct mlx5_priv *priv = dev->data->dev_private;

	if (*desc > mlx5_dev_get_max_wq_size(priv->sh)) {
		DRV_LOG(ERR,
			"port %u number of descriptors requested for QP"
			" %u is more than supported",
			dev->data->port_id, idx);
		rte_errno = EINVAL;
		return -EINVAL;
	}
	if (*desc <= MLX5_TX_COMP_THRESH) {
		DRV_LOG(WARNING,
			"port %u number of descriptors requested for QP"
			" %u must be higher than MLX5_TX_COMP_THRESH, using %u"
			" instead of %u", dev->data->port_id, idx,
			MLX5_TX_COMP_THRESH + 1, *desc);
		*desc = MLX5_TX_COMP_THRESH + 1;
	}
	if (!rte_is_power_of_2(*desc)) {
		*desc = 1 << log2above(*desc);
		DRV_LOG(WARNING,
			"port %u increased number of descriptors in QP"
			" %u to the next power of two (%d)",
			dev->data->port_id, idx, *desc);
	}
	DRV_LOG(DEBUG, "port %u configuring queue %u for %u descriptors",
		dev->data->port_id, idx, *desc);
	if (idx >= priv->qps_n) {
		DRV_LOG(ERR, "port %u QP index out of range (%u >= %u)",
			dev->data->port_id, idx, priv->qps_n);
		rte_errno = EOVERFLOW;
		return -rte_errno;
	}
	if (!mlx5_qp_releasable(dev, idx)) {
		rte_errno = EBUSY;
		DRV_LOG(ERR, "port %u unable to release QP index %u",
			dev->data->port_id, idx);
		return -rte_errno;
	}
	mlx5_qp_release(dev, idx);
	return 0;
}
/**
 * DPDK callback to configure a QP.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param idx
 *   QP index.
 * @param desc
 *   Number of descriptors to configure in queue.
 * @param socket
 *   NUMA socket on which memory must be allocated.
 * @param[in] conf
 *   Thresholds parameters.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
mlx5_qp_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc,
		    unsigned int socket, const struct rte_eth_txconf *conf)
{
	struct mlx5_priv *priv = dev->data->dev_private;
	struct mlx5_qp_data *qp = (*priv->qps)[idx];
	struct mlx5_qp_ctrl *qp_ctrl =
		container_of(qp, struct mlx5_qp_ctrl, qp);
	int res;

	res = mlx5_qp_pre_setup(dev, idx, &desc);
	if (res)
		return res;
	//txq_ctrl = mlx5_txq_new(dev, idx, desc, socket, conf);
	qp_ctrl = mlx5_qp_new(dev, idx, desc, socket, conf);
	if (!qp_ctrl) {
		DRV_LOG(ERR, "port %u unable to allocate QP index %u",
			dev->data->port_id, idx);
		return -rte_errno;
	}
	DRV_LOG(DEBUG, "port %u adding QP %u to list",
		dev->data->port_id, idx);
	(*priv->qps)[idx] = &qp_ctrl->qp;
	return 0;
}


/**
 * Set QP parameters from device configuration.
 *
 * @param qp_ctrl
 *   Pointer to QP control structure.
 */
static void
qp_set_params(struct mlx5_qp_ctrl *qp_ctrl)
{
	struct mlx5_priv *priv = qp_ctrl->priv;
	struct mlx5_port_config *config = &priv->config;
	struct mlx5_dev_cap *dev_cap = &priv->sh->dev_cap;
	unsigned int inlen_send; /* Inline data for ordinary SEND.*/
	unsigned int inlen_empw; /* Inline data for enhanced MPW. */
	unsigned int inlen_mode; /* Minimal required Inline data. */
	unsigned int txqs_inline; /* Min Tx queues to enable inline. */
	uint64_t dev_txoff = priv->dev_data->dev_conf.txmode.offloads;
	bool tso = qp_ctrl->qp.sq_offloads & (RTE_ETH_TX_OFFLOAD_TCP_TSO |
					    RTE_ETH_TX_OFFLOAD_VXLAN_TNL_TSO |
					    RTE_ETH_TX_OFFLOAD_GRE_TNL_TSO |
					    RTE_ETH_TX_OFFLOAD_IP_TNL_TSO |
					    RTE_ETH_TX_OFFLOAD_UDP_TNL_TSO);
	bool vlan_inline;
	unsigned int temp;

	qp_ctrl->qp.fast_free =
		!!((qp_ctrl->qp.sq_offloads & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE) &&
		   !(qp_ctrl->qp.sq_offloads & RTE_ETH_TX_OFFLOAD_MULTI_SEGS) &&
		   !config->mprq.enabled);
	if (config->txqs_inline == MLX5_ARG_UNSET)
		txqs_inline =
#if defined(RTE_ARCH_ARM64)
		(priv->pci_dev && priv->pci_dev->id.device_id ==
			PCI_DEVICE_ID_MELLANOX_BLUEFIELD) ?
			MLX5_INLINE_MAX_TXQS_BLUEFIELD :
#endif
			MLX5_INLINE_MAX_TXQS;
	else
		txqs_inline = (unsigned int)config->txqs_inline;
	inlen_send = (config->txq_inline_max == MLX5_ARG_UNSET) ?
		     MLX5_SEND_DEF_INLINE_LEN :
		     (unsigned int)config->txq_inline_max;
	inlen_empw = (config->txq_inline_mpw == MLX5_ARG_UNSET) ?
		     MLX5_EMPW_DEF_INLINE_LEN :
		     (unsigned int)config->txq_inline_mpw;
	inlen_mode = (config->txq_inline_min == MLX5_ARG_UNSET) ?
		     0 : (unsigned int)config->txq_inline_min;
	if (config->mps != MLX5_MPW_ENHANCED && config->mps != MLX5_MPW)
		inlen_empw = 0;
	/*
	 * If there is requested minimal amount of data to inline
	 * we MUST enable inlining. This is a case for ConnectX-4
	 * which usually requires L2 inlined for correct operating
	 * and ConnectX-4 Lx which requires L2-L4 inlined to
	 * support E-Switch Flows.
	 */
	if (inlen_mode) {
		if (inlen_mode <= MLX5_ESEG_MIN_INLINE_SIZE) {
			/*
			 * Optimize minimal inlining for single
			 * segment packets to fill one WQEBB
			 * without gaps.
			 */
			temp = MLX5_ESEG_MIN_INLINE_SIZE;
		} else {
			temp = inlen_mode - MLX5_ESEG_MIN_INLINE_SIZE;
			temp = RTE_ALIGN(temp, MLX5_WSEG_SIZE) +
			       MLX5_ESEG_MIN_INLINE_SIZE;
			temp = RTE_MIN(temp, MLX5_SEND_MAX_INLINE_LEN);
		}
		if (temp != inlen_mode) {
			DRV_LOG(INFO,
				"port %u minimal required inline setting"
				" aligned from %u to %u",
				PORT_ID(priv), inlen_mode, temp);
			inlen_mode = temp;
		}
	}
	/*
	 * If port is configured to support VLAN insertion and device
	 * does not support this feature by HW (for NICs before ConnectX-5
	 * or in case of wqe_vlan_insert flag is not set) we must enable
	 * data inline on all queues because it is supported by single
	 * tx_burst routine.
	 */
	qp_ctrl->qp.vlan_en = config->hw_vlan_insert;
	vlan_inline = (dev_txoff & RTE_ETH_TX_OFFLOAD_VLAN_INSERT) &&
		      !config->hw_vlan_insert;
	/*
	 * If there are few Tx queues it is prioritized
	 * to save CPU cycles and disable data inlining at all.
	 */
	if (inlen_send && priv->qps_n >= txqs_inline) {
		/*
		 * The data sent with ordinal MLX5_OPCODE_SEND
		 * may be inlined in Ethernet Segment, align the
		 * length accordingly to fit entire WQEBBs.
		 */
		temp = RTE_MAX(inlen_send,
			       MLX5_ESEG_MIN_INLINE_SIZE + MLX5_WQE_DSEG_SIZE);
		temp -= MLX5_ESEG_MIN_INLINE_SIZE + MLX5_WQE_DSEG_SIZE;
		temp = RTE_ALIGN(temp, MLX5_WQE_SIZE);
		temp += MLX5_ESEG_MIN_INLINE_SIZE + MLX5_WQE_DSEG_SIZE;
		temp = RTE_MIN(temp, MLX5_WQE_SIZE_MAX +
				     MLX5_ESEG_MIN_INLINE_SIZE -
				     MLX5_WQE_CSEG_SIZE -
				     MLX5_WQE_ESEG_SIZE -
				     MLX5_WQE_DSEG_SIZE * 2);
		temp = RTE_MIN(temp, MLX5_SEND_MAX_INLINE_LEN);
		temp = RTE_MAX(temp, inlen_mode);
		if (temp != inlen_send) {
			DRV_LOG(INFO,
				"port %u ordinary send inline setting"
				" aligned from %u to %u",
				PORT_ID(priv), inlen_send, temp);
			inlen_send = temp;
		}
		/*
		 * Not aligned to cache lines, but to WQEs.
		 * First bytes of data (initial alignment)
		 * is going to be copied explicitly at the
		 * beginning of inlining buffer in Ethernet
		 * Segment.
		 */
		MLX5_ASSERT(inlen_send >= MLX5_ESEG_MIN_INLINE_SIZE);
		MLX5_ASSERT(inlen_send <= MLX5_WQE_SIZE_MAX +
					  MLX5_ESEG_MIN_INLINE_SIZE -
					  MLX5_WQE_CSEG_SIZE -
					  MLX5_WQE_ESEG_SIZE -
					  MLX5_WQE_DSEG_SIZE * 2);
	} else if (inlen_mode) {
		/*
		 * If minimal inlining is requested we must
		 * enable inlining in general, despite the
		 * number of configured queues. Ignore the
		 * txq_inline_max devarg, this is not
		 * full-featured inline.
		 */
		inlen_send = inlen_mode;
		inlen_empw = 0;
	} else if (vlan_inline) {
		/*
		 * Hardware does not report offload for
		 * VLAN insertion, we must enable data inline
		 * to implement feature by software.
		 */
		inlen_send = MLX5_ESEG_MIN_INLINE_SIZE;
		inlen_empw = 0;
	} else {
		inlen_send = 0;
		inlen_empw = 0;
	}
	qp_ctrl->qp.inlen_send = inlen_send;
	qp_ctrl->qp.inlen_mode = inlen_mode;
	qp_ctrl->qp.inlen_empw = 0;
	if (inlen_send && inlen_empw && priv->txqs_n >= txqs_inline) {
		/*
		 * The data sent with MLX5_OPCODE_ENHANCED_MPSW
		 * may be inlined in Data Segment, align the
		 * length accordingly to fit entire WQEBBs.
		 */
		temp = RTE_MAX(inlen_empw,
			       MLX5_WQE_SIZE + MLX5_DSEG_MIN_INLINE_SIZE);
		temp -= MLX5_DSEG_MIN_INLINE_SIZE;
		temp = RTE_ALIGN(temp, MLX5_WQE_SIZE);
		temp += MLX5_DSEG_MIN_INLINE_SIZE;
		temp = RTE_MIN(temp, MLX5_WQE_SIZE_MAX +
				     MLX5_DSEG_MIN_INLINE_SIZE -
				     MLX5_WQE_CSEG_SIZE -
				     MLX5_WQE_ESEG_SIZE -
				     MLX5_WQE_DSEG_SIZE);
		temp = RTE_MIN(temp, MLX5_EMPW_MAX_INLINE_LEN);
		if (temp != inlen_empw) {
			DRV_LOG(INFO,
				"port %u enhanced empw inline setting"
				" aligned from %u to %u",
				PORT_ID(priv), inlen_empw, temp);
			inlen_empw = temp;
		}
		MLX5_ASSERT(inlen_empw >= MLX5_ESEG_MIN_INLINE_SIZE);
		MLX5_ASSERT(inlen_empw <= MLX5_WQE_SIZE_MAX +
					  MLX5_DSEG_MIN_INLINE_SIZE -
					  MLX5_WQE_CSEG_SIZE -
					  MLX5_WQE_ESEG_SIZE -
					  MLX5_WQE_DSEG_SIZE);
		qp_ctrl->qp.inlen_empw = inlen_empw;
	}
	qp_ctrl->max_inline_data = RTE_MAX(inlen_send, inlen_empw);
	if (tso) {
		qp_ctrl->max_tso_header = MLX5_MAX_TSO_HEADER;
		qp_ctrl->max_inline_data = RTE_MAX(qp_ctrl->max_inline_data,
						    MLX5_MAX_TSO_HEADER);
		qp_ctrl->qp.tso_en = 1;
	}
	if (((RTE_ETH_TX_OFFLOAD_VXLAN_TNL_TSO & qp_ctrl->qp.sq_offloads) &&
	    (dev_cap->tunnel_en & MLX5_TUNNELED_OFFLOADS_VXLAN_CAP)) |
	   ((RTE_ETH_TX_OFFLOAD_GRE_TNL_TSO & qp_ctrl->qp.sq_offloads) &&
	    (dev_cap->tunnel_en & MLX5_TUNNELED_OFFLOADS_GRE_CAP)) |
	   ((RTE_ETH_TX_OFFLOAD_GENEVE_TNL_TSO & qp_ctrl->qp.sq_offloads) &&
	    (dev_cap->tunnel_en & MLX5_TUNNELED_OFFLOADS_GENEVE_CAP)) |
	   (dev_cap->swp  & MLX5_SW_PARSING_TSO_CAP))
		qp_ctrl->qp.tunnel_en = 1;
	qp_ctrl->qp.swp_en = (((RTE_ETH_TX_OFFLOAD_IP_TNL_TSO |
				  RTE_ETH_TX_OFFLOAD_UDP_TNL_TSO) &
				  qp_ctrl->qp.sq_offloads) && (dev_cap->swp &
				  MLX5_SW_PARSING_TSO_CAP)) |
				((RTE_ETH_TX_OFFLOAD_OUTER_IPV4_CKSUM &
				 qp_ctrl->qp.sq_offloads) && (dev_cap->swp &
				 MLX5_SW_PARSING_CSUM_CAP));
}

/**
 * Calculate the maximal inline data size for QP TX queue.
 *
 * @param qp_ctrl
 *   Pointer to QP control structure.
 *
 * @return
 *   The maximal inline data size.
 */
static unsigned int
qp_calc_inline_max(struct mlx5_qp_ctrl *qp_ctrl)
{
	const unsigned int desc = 1 << qp_ctrl->qp.sq_elts_n;
	struct mlx5_priv *priv = qp_ctrl->priv;
	unsigned int wqe_size;

	wqe_size = mlx5_dev_get_max_wq_size(priv->sh) / desc;
	if (!wqe_size)
		return 0;
	/*
	 * This calculation is derived from the source of
	 * mlx5_calc_send_wqe() in rdma_core library.
	 */
	wqe_size = wqe_size * MLX5_WQE_SIZE -
		   MLX5_WQE_CSEG_SIZE -
		   MLX5_WQE_ESEG_SIZE -
		   MLX5_WSEG_SIZE -
		   MLX5_WSEG_SIZE +
		   MLX5_ESEG_MIN_INLINE_SIZE;
	return wqe_size;
}

/**
 * Adjust QP data inline parameters for large queue sizes.
 * The data inline feature requires multiple WQEs to fit the packets,
 * and if the large amount of Tx descriptors is requested by application
 * the total WQE amount may exceed the hardware capabilities. If the
 * default inline setting are used we can try to adjust these ones and
 * meet the hardware requirements and not exceed the queue size.
 *
 * @param qp_ctrl
 *   Pointer to QP control structure.
 */
static void
qp_adjust_params(struct mlx5_qp_ctrl *qp_ctrl)
{
	struct mlx5_priv *priv = qp_ctrl->priv;
	struct mlx5_port_config *config = &priv->config;
	unsigned int max_inline;

	max_inline = qp_calc_inline_max(qp_ctrl);
	if (!qp_ctrl->qp.inlen_send) {
		/*
		 * Inline data feature is not engaged at all.
		 * There is nothing to adjust.
		 */
		return;
	}
	if (qp_ctrl->max_inline_data <= max_inline) {
		/*
		 * The requested inline data length does not
		 * exceed queue capabilities.
		 */
		return;
	}
	if (qp_ctrl->qp.inlen_mode > max_inline) {
		DRV_LOG(WARNING,
			"minimal data inline requirements (%u) are not satisfied (%u) on port %u",
			qp_ctrl->qp.inlen_mode, max_inline, priv->dev_data->port_id);
	}
	if (qp_ctrl->qp.inlen_send > max_inline &&
	    config->txq_inline_max != MLX5_ARG_UNSET &&
	    config->txq_inline_max > (int)max_inline) {
		DRV_LOG(WARNING,
			"txq_inline_max requirements (%u) are not satisfied (%u) on port %u",
			qp_ctrl->qp.inlen_send, max_inline, priv->dev_data->port_id);
	}
	if (qp_ctrl->qp.inlen_empw > max_inline &&
	    config->txq_inline_mpw != MLX5_ARG_UNSET &&
	    config->txq_inline_mpw > (int)max_inline) {
		DRV_LOG(WARNING,
			"txq_inline_mpw requirements (%u) are not satisfied (%u) on port %u",
			qp_ctrl->qp.inlen_empw, max_inline, priv->dev_data->port_id);
	}
	MLX5_ASSERT(max_inline >= (MLX5_ESEG_MIN_INLINE_SIZE - MLX5_DSEG_MIN_INLINE_SIZE));
	max_inline -= MLX5_ESEG_MIN_INLINE_SIZE - MLX5_DSEG_MIN_INLINE_SIZE;
	if (qp_ctrl->qp.tso_en && max_inline < MLX5_MAX_TSO_HEADER) {
		DRV_LOG(WARNING,
			"tso header inline requirements (%u) are not satisfied (%u) on port %u",
			MLX5_MAX_TSO_HEADER, max_inline, priv->dev_data->port_id);
	}
	if (qp_ctrl->qp.inlen_send > max_inline) {
		DRV_LOG(WARNING,
			"adjust txq_inline_max (%u->%u) due to large Tx queue on port %u",
			qp_ctrl->qp.inlen_send, max_inline, priv->dev_data->port_id);
		qp_ctrl->qp.inlen_send = max_inline;
	}
	if (qp_ctrl->qp.inlen_empw > max_inline) {
		DRV_LOG(WARNING,
			"adjust txq_inline_mpw (%u->%u) due to large Tx queue on port %u",
			qp_ctrl->qp.inlen_empw, max_inline, priv->dev_data->port_id);
		qp_ctrl->qp.inlen_empw = max_inline;
	}
	qp_ctrl->max_inline_data = RTE_MAX(qp_ctrl->qp.inlen_send,
					    qp_ctrl->qp.inlen_empw);
	MLX5_ASSERT(qp_ctrl->qp.inlen_mode <= qp_ctrl->qp.inlen_send);
	MLX5_ASSERT(qp_ctrl->qp.inlen_mode <= qp_ctrl->qp.inlen_empw ||
		    !qp_ctrl->qp.inlen_empw);
}

/**
 * Calculate the total number of WQEBB for QP Tx queue.
 *
 * Simplified version of calc_sq_size() in rdma-core.
 *
 * @param qp_ctrl
 *   Pointer to QP control structure.
 * @param devx
 *   If the calculation is used for Devx queue.
 *
 * @return
 *   The number of WQEBB.
 */
static int
qp_calc_wqebb_cnt(struct mlx5_qp_ctrl *qp_ctrl, bool devx)
{
	unsigned int wqe_size;
	const unsigned int desc = 1 << qp_ctrl->qp.sq_elts_n;

	if (devx) {
		wqe_size = qp_ctrl->qp.tso_en ?
			   RTE_ALIGN(qp_ctrl->max_tso_header, MLX5_WSEG_SIZE) : 0;
		wqe_size += MLX5_WQE_CSEG_SIZE +
			    MLX5_WQE_ESEG_SIZE +
			    MLX5_WQE_DSEG_SIZE;
		if (qp_ctrl->qp.inlen_send)
			wqe_size = RTE_MAX(wqe_size, sizeof(struct mlx5_wqe_cseg) +
						     sizeof(struct mlx5_wqe_eseg) +
						     RTE_ALIGN(qp_ctrl->qp.inlen_send +
							       sizeof(uint32_t),
							       MLX5_WSEG_SIZE));
		wqe_size = RTE_ALIGN(wqe_size, MLX5_WQE_SIZE);
	} else {
		wqe_size = MLX5_WQE_CSEG_SIZE +
			   MLX5_WQE_ESEG_SIZE +
			   MLX5_WSEG_SIZE -
			   MLX5_ESEG_MIN_INLINE_SIZE +
			   qp_ctrl->max_inline_data;
		wqe_size = RTE_MAX(wqe_size, MLX5_WQE_SIZE);
	}
	return rte_align32pow2(wqe_size * desc) / MLX5_WQE_SIZE;
}

/*
 * Calculate WQ memory length for a QP Tx queue.
 *
 * @param log_wqe_cnt
 *   Logarithm value of WQE numbers.
 *
 * @return
 *   memory length of this WQ.
 */
static uint32_t mlx5_qp_wq_mem_length(uint32_t log_wqe_cnt)
{
	uint32_t num_of_wqbbs = 1U << log_wqe_cnt;
	uint32_t umem_size;

	umem_size = MLX5_WQE_SIZE * num_of_wqbbs;
	return umem_size;
}

/*
 * Calculate CQ memory length for a QP Tx queue.
 *
 * @param dev
 *   Pointer to Ethernet device.
 * @param qp_ctrl
 *   Pointer to the TxQ control structure of the CQ.
 *
 * @return
 *   memory length of this CQ.
 */
static uint32_t
mlx5_qp_cq_mem_length(struct rte_eth_dev *dev, struct mlx5_qp_ctrl *qp_ctrl)
{
	uint32_t cqe_n, log_desc_n;

	if (__rte_trace_point_fp_is_enabled() &&
	    qp_ctrl->qp.sq_offloads & RTE_ETH_TX_OFFLOAD_SEND_ON_TIMESTAMP)
		cqe_n = UINT16_MAX / 2 - 1;
	else
		cqe_n = (1UL << qp_ctrl->qp.sq_elts_n) / MLX5_TX_COMP_THRESH +
			1 + MLX5_TX_COMP_THRESH_INLINE_DIV;
	log_desc_n = log2above(cqe_n);
	cqe_n = 1UL << log_desc_n;
	if (cqe_n > UINT16_MAX) {
		DRV_LOG(ERR, "Port %u Tx queue %u requests to many CQEs %u.",
			dev->data->port_id, qp_ctrl->qp.qp_idx, cqe_n);
		rte_errno = EINVAL;
		return 0;
	}
	return sizeof(struct mlx5_cqe) * cqe_n;
}

/**
 * Create a DPDK QP.
 *
 * @param dev
 *   Pointer to Ethernet device.
 * @param idx
 *   QP index.
 * @param desc
 *   Number of descriptors to configure in queue.
 * @param socket
 *   NUMA socket on which memory must be allocated.
 * @param[in] conf
 *  Thresholds parameters.
 *
 * @return
 *   A DPDK queue object on success, NULL otherwise and rte_errno is set.
 */
struct mlx5_qp_ctrl *
mlx5_qp_new(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc,
	     unsigned int socket, const struct rte_eth_txconf *conf)
{
	int ret;
	struct mlx5_priv *priv = dev->data->dev_private;
	struct mlx5_qp_ctrl *tmpl;
	uint16_t max_wqe;
	uint32_t wqebb_cnt, log_desc_n;

	if (socket != (unsigned int)SOCKET_ID_ANY) {
		tmpl = mlx5_malloc(MLX5_MEM_RTE | MLX5_MEM_ZERO, sizeof(*tmpl) +
			   desc * sizeof(struct rte_mbuf *), 0, socket);
	} else {
		tmpl = mlx5_malloc_numa_tolerant(MLX5_MEM_RTE | MLX5_MEM_ZERO, sizeof(*tmpl) +
					 desc * sizeof(struct rte_mbuf *), 0,
					 dev->device->numa_node);
	}
	if (!tmpl) {
		rte_errno = ENOMEM;
		return NULL;
	}
	if (socket != (unsigned int)SOCKET_ID_ANY) {
		if (mlx5_mr_ctrl_init(&tmpl->qp.mr_ctrl,
					&priv->sh->cdev->mr_scache.dev_gen, socket))
			/* rte_errno is already set. */
			goto error;
	} else {
		ret = mlx5_mr_ctrl_init(&tmpl->qp.mr_ctrl,
					&priv->sh->cdev->mr_scache.dev_gen, dev->device->numa_node);
		if (ret == -ENOMEM) {
			ret = mlx5_mr_ctrl_init(&tmpl->qp.mr_ctrl,
						&priv->sh->cdev->mr_scache.dev_gen, SOCKET_ID_ANY);
		}
		if (ret)
			/* rte_errno is already set. */
			goto error;
	}
	MLX5_ASSERT(desc > MLX5_TX_COMP_THRESH);
	tmpl->qp.sq_offloads = conf->offloads |
			     dev->data->dev_conf.txmode.offloads;
	tmpl->priv = priv;
	tmpl->socket = (socket == (unsigned int)SOCKET_ID_ANY ?
			(unsigned int)dev->device->numa_node : socket);
	tmpl->qp.sq_elts_n = log2above(desc);
	tmpl->qp.sq_elts_s = desc;
	tmpl->qp.sq_elts_m = desc - 1;
	tmpl->qp.port_id = dev->data->port_id;
	tmpl->qp.qp_idx = idx;
	qp_set_params(tmpl);
	qp_adjust_params(tmpl);
	wqebb_cnt = qp_calc_wqebb_cnt(tmpl, !!mlx5_devx_obj_ops_en(priv->sh));
	max_wqe = mlx5_dev_get_max_wq_size(priv->sh);
	if (wqebb_cnt > max_wqe) {
		DRV_LOG(ERR,
			"port %u Tx WQEBB count (%d) exceeds the limit (%d),"
			" try smaller queue size",
			dev->data->port_id, wqebb_cnt, max_wqe);
		rte_errno = ENOMEM;
		goto error;
	}
	if (priv->sh->config.txq_mem_algn != 0) {
		log_desc_n = log2above(wqebb_cnt);
		tmpl->qp.sq_mem_len = mlx5_qp_wq_mem_length(log_desc_n);
		tmpl->qp.sq_cq_mem_len = mlx5_qp_cq_mem_length(dev, tmpl);
		DRV_LOG(DEBUG, "Port %u TxQ %u WQ length %u, CQ length %u before align.",
			dev->data->port_id, idx, tmpl->qp.sq_mem_len, tmpl->qp.sq_cq_mem_len);
		priv->consec_tx_mem.sq_total_size += tmpl->qp.sq_mem_len;
		priv->consec_tx_mem.cq_total_size += tmpl->qp.sq_cq_mem_len;
	}
	rte_atomic_fetch_add_explicit(&tmpl->refcnt, 1, rte_memory_order_relaxed);
	LIST_INSERT_HEAD(&priv->qpsctrl, tmpl, next);
	return tmpl;
error:
	mlx5_mr_btree_free(&tmpl->qp.mr_ctrl.cache_bh);
	mlx5_free(tmpl);
	return NULL;
}
