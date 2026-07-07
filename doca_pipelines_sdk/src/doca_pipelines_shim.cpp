/* SPDX-License-Identifier: BSD-3-Clause */

#include "doca_pipelines_shim.h"

#include "doca_pipelines.hpp"

extern "C" {

int l3fwd_doca_pipelines_dpdk_probe(const char *pci_bdf, const char *extra_devargs)
{
	return doca_pipelines_dpdk_probe(pci_bdf, extra_devargs);
}

int l3fwd_doca_pipelines_init(uint16_t nb_queues)
{
    return doca_pipelines_init(nb_queues);
}

void l3fwd_doca_pipelines_cleanup(void)
{
    doca_pipelines_cleanup();
}

}
