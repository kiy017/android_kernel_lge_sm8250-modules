/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * timelm: the subset of the 4.19 QC <linux/msm-bus.h> handle API used by
 * msm_cvp (cvp_hfi.c), implemented on the 5.15 interconnect framework in
 * msm/cvp/timelm_msm_bus_icc.c.  Masters/slaves are the 4.19 msm-bus IDs
 * from the stock qcom,bus-master/slave DT properties; ab/ib are in Bps.
 */

#ifndef _ARCH_ARM_MACH_MSM_BUS_H
#define _ARCH_ARM_MACH_MSM_BUS_H

#include <linux/types.h>

struct device;
struct icc_path;

struct msm_bus_client_handle {
	char *name;
	int mas;
	int slv;
	struct icc_path *path;
};

struct msm_bus_client_handle *
msm_bus_scale_register(uint32_t mas, uint32_t slv, char *name,
		bool active_only);
/* timelm: device the ICC paths are requested for (msm_cvp pdev) */
void timelm_msm_bus_set_dev(struct device *dev);
void msm_bus_scale_unregister(struct msm_bus_client_handle *cl);
int msm_bus_scale_update_bw(struct msm_bus_client_handle *cl, u64 ab, u64 ib);

#endif /*_ARCH_ARM_MACH_MSM_BUS_H*/
