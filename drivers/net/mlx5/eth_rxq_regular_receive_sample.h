/*
 * Copyright (c) 2025 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ETH_RXQ_REGULAR_RECEIVE_SAMPLE_H_
#define ETH_RXQ_REGULAR_RECEIVE_SAMPLE_H_

#include <stdbool.h>
#include <stdint.h>

#include <doca_error.h>

#include <rte_mbuf.h>
#include <rte_mempool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct eth_rxq_sample_objects;

doca_error_t eth_rxq_open(struct eth_rxq_sample_objects **handle,
			  const char *ib_dev_name,
			  bool timestamp_enable,
			  uint16_t queue_idx,
			  struct rte_mempool *mp);

uint16_t eth_rxq_poll(struct eth_rxq_sample_objects *handle,
		      struct rte_mbuf **mbufs,
		      uint16_t nb_pkts);

void eth_rxq_close(struct eth_rxq_sample_objects *handle);

/*
 * Install a root pipe steering ESP packets into one of two RXQs based on the
 * LSB of the ESP sequence number. handles[0] receives SN-LSB==0, handles[1]
 * receives SN-LSB==1. Both handles must already be opened.
 */
doca_error_t eth_rxq_install_lsb_demux_flow(struct eth_rxq_sample_objects **handles);

void eth_rxq_uninstall_demux_flow(void);

#ifdef __cplusplus
}
#endif

#endif /* ETH_RXQ_REGULAR_RECEIVE_SAMPLE_H_ */

