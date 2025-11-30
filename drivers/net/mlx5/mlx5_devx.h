/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2020 Mellanox Technologies, Ltd
 */

#ifndef RTE_PMD_MLX5_DEVX_H_
#define RTE_PMD_MLX5_DEVX_H_

#include "mlx5.h"
#include "mlx5_qp.h"

void mlx5_qp_release_devx_resources(struct mlx5_qp_ctrl *qp_ctrl);
int mlx5_qp_devx_obj_new(struct rte_eth_dev *dev, uint16_t idx, enum mlx5_qp_dir dir);
int mlx5_txq_devx_obj_new(struct rte_eth_dev *dev, uint16_t idx);
int mlx5_txq_devx_modify(struct mlx5_txq_obj *obj,
			 enum mlx5_txq_modify_type type, uint8_t dev_port);
void mlx5_txq_devx_obj_release(struct mlx5_txq_obj *txq_obj);
int mlx5_devx_modify_rq(struct mlx5_rxq_priv *rxq, uint8_t type);
int mlx5_devx_extq_port_validate(uint16_t port_id);

extern struct mlx5_obj_ops devx_obj_ops;


#endif /* RTE_PMD_MLX5_DEVX_H_ */
