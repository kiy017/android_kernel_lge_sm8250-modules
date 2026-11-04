// SPDX-License-Identifier: GPL-2.0-only
/*
 * timelm: 4.19 msm-bus client API (as used by cam_cpas_hw.c) on top of the
 * 5.15 interconnect framework.
 *
 * msm_bus_pdata_from_node() parses the stock properties exactly like 4.19
 * drivers/soc/qcom/msm_bus/msm_bus_of.c (qcom,msm-bus,name/num-cases/
 * num-paths/vectors-KBps, KBTOB = x1000).  Each path is an icc_get() on the
 * 5.15 kona node IDs; the 4.19 msm-bus IDs in the stock DT are translated
 * with msm_bus_icc_id_map[].  No active-only flag in the camera DT, i.e.
 * dual context, which is the untagged (QCOM_ICC_TAG_ALWAYS) ICC vote.
 */

#include <linux/interconnect.h>
#include <linux/msm-bus.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <dt-bindings/interconnect/qcom,kona.h>

#include "cam_debug_util.h"

#define KBTOB(a) ((a) * 1000ULL)
#define MSM_BUS_ICC_MAX_CLIENTS 16

/* 4.19 dt-bindings/msm/msm-bus-ids.h -> 5.15 dt-bindings/interconnect/qcom,kona.h */
static const struct {
	int msm_bus_id;
	int icc_id;
} msm_bus_icc_id_map[] = {
	{ 1,   MASTER_APPSS_PROC },	/* MSM_BUS_MASTER_AMPSS_M0 */
	{ 137, MASTER_CAMNOC_SF },	/* MSM_BUS_MASTER_CAMNOC_SF */
	{ 170, MASTER_CAMNOC_HF },	/* MSM_BUS_MASTER_CAMNOC_HF */
	{ 171, MASTER_CAMNOC_ICP },	/* MSM_BUS_MASTER_CAMNOC_ICP */
	{ 512, SLAVE_EBI1 },		/* MSM_BUS_SLAVE_EBI_CH0 */
	{ 589, SLAVE_CAMERA_CFG },	/* MSM_BUS_SLAVE_CAMERA_CFG */
};

struct msm_bus_icc_client {
	struct msm_bus_scale_pdata *pdata;
	struct icc_path **paths;	/* one per vector of a usecase */
};

static struct msm_bus_icc_client msm_bus_icc_clients[MSM_BUS_ICC_MAX_CLIENTS];
static DEFINE_MUTEX(msm_bus_icc_lock);

static int msm_bus_icc_id(int msm_bus_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(msm_bus_icc_id_map); i++)
		if (msm_bus_icc_id_map[i].msm_bus_id == msm_bus_id)
			return msm_bus_icc_id_map[i].icc_id;

	return -EINVAL;
}

struct msm_bus_scale_pdata *msm_bus_pdata_from_node(
		struct platform_device *pdev, struct device_node *of_node)
{
	struct device *dev;
	struct msm_bus_scale_pdata *pdata;
	struct msm_bus_paths *usecase;
	const __be32 *vec_arr;
	u32 num_usecases, num_paths;
	int i, j, len;

	if (!pdev || !of_node)
		return NULL;
	dev = &pdev->dev;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	if (of_property_read_string(of_node, "qcom,msm-bus,name",
			&pdata->name) ||
		of_property_read_u32(of_node, "qcom,msm-bus,num-cases",
			&num_usecases) ||
		of_property_read_u32(of_node, "qcom,msm-bus,num-paths",
			&num_paths)) {
		CAM_ERR(CAM_CPAS, "%pOF: msm-bus name/num-cases/num-paths",
			of_node);
		return NULL;
	}

	pdata->num_usecases = num_usecases;
	pdata->active_only = of_property_read_bool(of_node,
		"qcom,msm-bus,active-only");
	pdata->dev = dev;

	vec_arr = of_get_property(of_node, "qcom,msm-bus,vectors-KBps", &len);
	if (!vec_arr || len != num_usecases * num_paths * sizeof(u32) * 4) {
		CAM_ERR(CAM_CPAS, "%s: bad vectors-KBps", pdata->name);
		return NULL;
	}

	usecase = devm_kcalloc(dev, num_usecases, sizeof(*usecase),
		GFP_KERNEL);
	if (!usecase)
		return NULL;

	for (i = 0; i < num_usecases; i++) {
		usecase[i].num_paths = num_paths;
		usecase[i].vectors = devm_kcalloc(dev, num_paths,
			sizeof(struct msm_bus_vectors), GFP_KERNEL);
		if (!usecase[i].vectors)
			return NULL;

		for (j = 0; j < num_paths; j++) {
			int index = ((i * num_paths) + j) * 4;

			usecase[i].vectors[j].src = be32_to_cpu(vec_arr[index]);
			usecase[i].vectors[j].dst =
				be32_to_cpu(vec_arr[index + 1]);
			usecase[i].vectors[j].ab =
				KBTOB(be32_to_cpu(vec_arr[index + 2]));
			usecase[i].vectors[j].ib =
				KBTOB(be32_to_cpu(vec_arr[index + 3]));
		}
	}

	pdata->usecase = usecase;
	return pdata;
}

static void msm_bus_icc_put_paths(struct icc_path **paths, int n)
{
	while (--n >= 0)
		icc_put(paths[n]);
	kfree(paths);
}

/* returns a client handle (index + 1), 0 on failure, as in 4.19 */
uint32_t msm_bus_scale_register_client(struct msm_bus_scale_pdata *pdata)
{
	struct msm_bus_icc_client *cl = NULL;
	struct icc_path **paths;
	int i, j, num_paths, src, dst;

	if (!pdata || !pdata->dev || !pdata->num_usecases)
		return 0;

	num_paths = pdata->usecase[0].num_paths;
	paths = kcalloc(num_paths, sizeof(*paths), GFP_KERNEL);
	if (!paths)
		return 0;

	for (j = 0; j < num_paths; j++) {
		src = msm_bus_icc_id(pdata->usecase[0].vectors[j].src);
		dst = msm_bus_icc_id(pdata->usecase[0].vectors[j].dst);
		if (src < 0 || dst < 0) {
			CAM_ERR(CAM_CPAS, "%s: no ICC id for msm-bus %d -> %d",
				pdata->name, pdata->usecase[0].vectors[j].src,
				pdata->usecase[0].vectors[j].dst);
			goto put;
		}

		paths[j] = icc_get(pdata->dev, src, dst);
		if (IS_ERR_OR_NULL(paths[j])) {
			CAM_ERR(CAM_CPAS, "%s: icc_get(%d, %d) failed %ld",
				pdata->name, src, dst, PTR_ERR(paths[j]));
			goto put;
		}
	}

	mutex_lock(&msm_bus_icc_lock);
	for (i = 0; i < MSM_BUS_ICC_MAX_CLIENTS; i++) {
		if (!msm_bus_icc_clients[i].pdata) {
			cl = &msm_bus_icc_clients[i];
			cl->pdata = pdata;
			cl->paths = paths;
			break;
		}
	}
	mutex_unlock(&msm_bus_icc_lock);

	if (!cl) {
		CAM_ERR(CAM_CPAS, "%s: out of msm-bus clients", pdata->name);
		goto put;
	}

	return i + 1;

put:
	msm_bus_icc_put_paths(paths, j);
	return 0;
}

static struct msm_bus_icc_client *msm_bus_icc_get_client(uint32_t cl)
{
	if (!cl || cl > MSM_BUS_ICC_MAX_CLIENTS ||
		!msm_bus_icc_clients[cl - 1].pdata)
		return NULL;

	return &msm_bus_icc_clients[cl - 1];
}

int msm_bus_scale_client_update_request(uint32_t cl, unsigned int index)
{
	struct msm_bus_icc_client *client = msm_bus_icc_get_client(cl);
	struct msm_bus_paths *usecase;
	int j, rc;

	if (!client || index >= client->pdata->num_usecases)
		return -EINVAL;

	usecase = &client->pdata->usecase[index];
	for (j = 0; j < usecase->num_paths; j++) {
		rc = icc_set_bw(client->paths[j],
			Bps_to_icc(usecase->vectors[j].ab),
			Bps_to_icc(usecase->vectors[j].ib));
		if (rc) {
			CAM_ERR(CAM_CPAS, "%s: icc_set_bw idx %u failed %d",
				client->pdata->name, index, rc);
			return rc;
		}
	}

	return 0;
}

void msm_bus_scale_unregister_client(uint32_t cl)
{
	struct msm_bus_icc_client *client = msm_bus_icc_get_client(cl);

	if (!client)
		return;

	msm_bus_icc_put_paths(client->paths,
		client->pdata->usecase[0].num_paths);

	mutex_lock(&msm_bus_icc_lock);
	client->pdata = NULL;
	client->paths = NULL;
	mutex_unlock(&msm_bus_icc_lock);
}
