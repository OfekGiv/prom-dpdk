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
