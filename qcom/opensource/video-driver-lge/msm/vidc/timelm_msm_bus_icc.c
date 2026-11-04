// SPDX-License-Identifier: GPL-2.0-only
/*
 * timelm: 4.19 msm-bus handle API (msm_bus_scale_register/update_bw/
 * unregister) on the 5.15 interconnect framework, for msm_vidc.  The stock
 * kona-vidc.dtsi bus nodes use 4.19 msm-bus IDs; msm_bus_icc_id_map[] maps
 * them to dt-bindings/interconnect/qcom,kona.h.  active_only is not used by
 * msm_vidc (always false = dual context = untagged ICC vote).
 */

#include <linux/err.h>
#include <linux/interconnect.h>
#include <linux/msm-bus.h>
#include <linux/slab.h>
#include <dt-bindings/interconnect/qcom,kona.h>

#include "msm_vidc_debug.h"

/* 4.19 dt-bindings/msm/msm-bus-ids.h -> 5.15 qcom,kona.h */
static const struct {
	int msm_bus_id;
	int icc_id;
} msm_bus_icc_id_map[] = {
	{ 1,   MASTER_APPSS_PROC },	/* MSM_BUS_MASTER_AMPSS_M0 */
	{ 63,  MASTER_VIDEO_P0 },	/* MSM_BUS_MASTER_VIDEO_P0 */
	{ 129, MASTER_LLCC },		/* MSM_BUS_MASTER_LLCC */
	{ 512, SLAVE_EBI1 },		/* MSM_BUS_SLAVE_EBI_CH0 */
	{ 596, SLAVE_VENUS_CFG },	/* MSM_BUS_SLAVE_VENUS_CFG */
	{ 770, SLAVE_LLCC },		/* MSM_BUS_SLAVE_LLCC */
};

static struct device *msm_bus_icc_dev;

void timelm_msm_bus_set_dev(struct device *dev)
{
	msm_bus_icc_dev = dev;
}

static int msm_bus_icc_id(int msm_bus_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(msm_bus_icc_id_map); i++)
		if (msm_bus_icc_id_map[i].msm_bus_id == msm_bus_id)
			return msm_bus_icc_id_map[i].icc_id;

	return -EINVAL;
}

struct msm_bus_client_handle *
msm_bus_scale_register(uint32_t mas, uint32_t slv, char *name,
		bool active_only)
{
	struct msm_bus_client_handle *cl;
	int src = msm_bus_icc_id(mas), dst = msm_bus_icc_id(slv);

	if (src < 0 || dst < 0) {
		d_vpr_e("%s: no ICC id for msm-bus %u -> %u\n", name, mas, slv);
		return ERR_PTR(-EINVAL);
	}

	cl = kzalloc(sizeof(*cl), GFP_KERNEL);
	if (!cl)
		return ERR_PTR(-ENOMEM);

	cl->path = icc_get(msm_bus_icc_dev, src, dst);
	if (IS_ERR_OR_NULL(cl->path)) {
		long rc = cl->path ? PTR_ERR(cl->path) : -ENODEV;

		d_vpr_e("%s: icc_get(%d, %d) failed %ld\n", name, src, dst, rc);
		kfree(cl);
		return ERR_PTR(rc);
	}

	cl->name = name;
	cl->mas = mas;
	cl->slv = slv;

	return cl;
}

int msm_bus_scale_update_bw(struct msm_bus_client_handle *cl, u64 ab, u64 ib)
{
	if (IS_ERR_OR_NULL(cl))
		return -EINVAL;

	return icc_set_bw(cl->path, Bps_to_icc(ab), Bps_to_icc(ib));
}

void msm_bus_scale_unregister(struct msm_bus_client_handle *cl)
{
	if (IS_ERR_OR_NULL(cl))
		return;

	icc_put(cl->path);
	kfree(cl);
}
