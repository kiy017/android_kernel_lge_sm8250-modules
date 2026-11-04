/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * timelm: the subset of the 4.19 QC <linux/msm-bus.h> used by cam_cpas,
 * implemented on top of the 5.15 interconnect framework in
 * drivers/cam_cpas/cam_msm_bus_icc.c.  Struct layouts match 4.19, so
 * cam_cpas_hw.c and the stock "qcom,msm-bus,*" DT properties stay as they
 * are.
 */

#ifndef _ARCH_ARM_MACH_MSM_BUS_H
#define _ARCH_ARM_MACH_MSM_BUS_H

#include <linux/types.h>
#include <linux/platform_device.h>

struct msm_bus_vectors {
	int src; /* Master */
	int dst; /* Slave */
	uint64_t ab; /* Arbitrated bandwidth */
	uint64_t ib; /* Instantaneous bandwidth */
};

struct msm_bus_paths {
	int num_paths;
	struct msm_bus_vectors *vectors;
};

struct msm_bus_scale_pdata {
	struct msm_bus_paths *usecase;
	int num_usecases;
	const char *name;
	unsigned int active_only;
	/* timelm: device that owns the ICC paths (4.19 had none here) */
	struct device *dev;
};

struct msm_bus_scale_pdata *msm_bus_pdata_from_node(
		struct platform_device *pdev, struct device_node *of_node);
uint32_t msm_bus_scale_register_client(struct msm_bus_scale_pdata *pdata);
int msm_bus_scale_client_update_request(uint32_t cl, unsigned int index);
void msm_bus_scale_unregister_client(uint32_t cl);

#endif /*_ARCH_ARM_MACH_MSM_BUS_H*/
