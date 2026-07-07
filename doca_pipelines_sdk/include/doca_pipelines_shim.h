/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef L3FWD_DOCA_PIPELINES_SHIM_H
#define L3FWD_DOCA_PIPELINES_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int l3fwd_doca_pipelines_dpdk_probe(const char *pci_bdf, const char *extra_devargs);
int l3fwd_doca_pipelines_init(uint16_t nb_queues);
void l3fwd_doca_pipelines_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
