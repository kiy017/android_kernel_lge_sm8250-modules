// SPDX-License-Identifier: GPL-2.0-only
/*
 * timelm: module entry for LG's 4.19 kona techpack/camera built as one
 * out-of-tree camera.ko.
 *
 * Stock built the techpack into the kernel, so every sub-driver had its own
 * initcall and the order came from the link order (drivers/Makefile) plus
 * the initcall levels: camera_class (subsys), all module_init()s, then the
 * two late_initcall()s.  This file runs the same functions in that order.
 *
 * cam_req_mgr_late_init() creates the v4l-subdev nodes exactly once and
 * cam_register_subdev() rejects later subdevs ("dynamic node is not
 * allowed"), so deferred probes are flushed before it runs.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>

struct camera_initcall {
	const char *name;
	int (*init)(void);
	void (*exit)(void);
};

#define CAM_INITCALL(i, e) { #i, i, e }

extern int lge_regulator_mode_change_init(void);
extern void lge_regulator_mode_change_exit(void);
extern int camera_class_init(void);
extern void camera_class_exit(void);
extern int cam_req_mgr_init(void);
extern void cam_req_mgr_exit(void);
extern int cam_sync_init(void);
extern void cam_sync_exit(void);
extern int cam_smmu_init_module(void);
extern void cam_smmu_exit_module(void);
extern int cam_cpas_dev_init_module(void);
extern void cam_cpas_dev_exit_module(void);
extern int cam_cdm_intf_init_module(void);
extern void cam_cdm_intf_exit_module(void);
extern int cam_hw_cdm_init_module(void);
extern void cam_hw_cdm_exit_module(void);
extern int cam_ife_csid17x_init_module(void);
extern void cam_ife_csid17x_exit_module(void);
extern int cam_ife_csid_lite_init_module(void);
extern void cam_ife_csid_lite_exit_module(void);
extern int cam_vfe_init_module(void);
extern void cam_vfe_exit_module(void);
extern int cam_isp_dev_init_module(void);
extern void cam_isp_dev_exit_module(void);
extern int cam_res_mgr_init(void);
extern void cam_res_mgr_exit(void);
extern int cam_cci_init_module(void);
extern void cam_cci_exit_module(void);
extern int cam_csiphy_init_module(void);
extern void cam_csiphy_exit_module(void);
extern int cam_actuator_driver_init(void);
extern void cam_actuator_driver_exit(void);
extern int cam_sensor_driver_init(void);
extern void cam_sensor_driver_exit(void);
extern int cam_eeprom_driver_init(void);
extern void cam_eeprom_driver_exit(void);
extern int cam_ois_driver_init(void);
extern void cam_ois_driver_exit(void);
extern int cam_flash_init_module(void);
extern void cam_flash_exit_module(void);
extern int cam_a5_init_module(void);
extern void cam_a5_exit_module(void);
extern int cam_ipe_init_module(void);
extern void cam_ipe_exit_module(void);
extern int cam_bps_init_module(void);
extern void cam_bps_exit_module(void);
extern int cam_icp_init_module(void);
extern void cam_icp_exit_module(void);
extern int cam_jpeg_enc_init_module(void);
extern void cam_jpeg_enc_exit_module(void);
extern int cam_jpeg_dma_init_module(void);
extern void cam_jpeg_dma_exit_module(void);
extern int cam_jpeg_dev_init_module(void);
extern void cam_jpeg_dev_exit_module(void);
extern int cam_fd_hw_init_module(void);
extern void cam_fd_hw_exit_module(void);
extern int cam_fd_dev_init_module(void);
extern void cam_fd_dev_exit_module(void);
extern int cam_lrme_hw_init_module(void);
extern void cam_lrme_hw_exit_module(void);
extern int cam_lrme_dev_init_module(void);
extern void cam_lrme_dev_exit_module(void);
extern int cam_custom_hw_sub_module_init(void);
extern void cam_custom_hw_sub_module_exit(void);
extern int cam_custom_csid_driver_init(void);
extern void cam_custom_csid_driver_exit(void);
extern int cam_custom_dev_init_module(void);
extern void cam_custom_dev_exit_module(void);
extern int cam_req_mgr_late_init(void);
extern int cam_cci_late_init(void);

/* stock link order: drivers/Makefile, then each directory's Makefile */
static const struct camera_initcall camera_initcalls[] = {
	/* drivers/soc/qcom/lge in the 4.19 kernel */
	CAM_INITCALL(lge_regulator_mode_change_init,
		lge_regulator_mode_change_exit),
	CAM_INITCALL(camera_class_init, camera_class_exit),
	CAM_INITCALL(cam_req_mgr_init, cam_req_mgr_exit),
	CAM_INITCALL(cam_sync_init, cam_sync_exit),
	CAM_INITCALL(cam_smmu_init_module, cam_smmu_exit_module),
	CAM_INITCALL(cam_cpas_dev_init_module, cam_cpas_dev_exit_module),
	CAM_INITCALL(cam_cdm_intf_init_module, cam_cdm_intf_exit_module),
	CAM_INITCALL(cam_hw_cdm_init_module, cam_hw_cdm_exit_module),
	CAM_INITCALL(cam_ife_csid17x_init_module, cam_ife_csid17x_exit_module),
	CAM_INITCALL(cam_ife_csid_lite_init_module,
		cam_ife_csid_lite_exit_module),
	CAM_INITCALL(cam_vfe_init_module, cam_vfe_exit_module),
	CAM_INITCALL(cam_isp_dev_init_module, cam_isp_dev_exit_module),
	CAM_INITCALL(cam_res_mgr_init, cam_res_mgr_exit),
	CAM_INITCALL(cam_cci_init_module, cam_cci_exit_module),
	CAM_INITCALL(cam_csiphy_init_module, cam_csiphy_exit_module),
	CAM_INITCALL(cam_actuator_driver_init, cam_actuator_driver_exit),
	CAM_INITCALL(cam_sensor_driver_init, cam_sensor_driver_exit),
	CAM_INITCALL(cam_eeprom_driver_init, cam_eeprom_driver_exit),
	CAM_INITCALL(cam_ois_driver_init, cam_ois_driver_exit),
	CAM_INITCALL(cam_flash_init_module, cam_flash_exit_module),
	CAM_INITCALL(cam_a5_init_module, cam_a5_exit_module),
	CAM_INITCALL(cam_ipe_init_module, cam_ipe_exit_module),
	CAM_INITCALL(cam_bps_init_module, cam_bps_exit_module),
	CAM_INITCALL(cam_icp_init_module, cam_icp_exit_module),
	CAM_INITCALL(cam_jpeg_enc_init_module, cam_jpeg_enc_exit_module),
	CAM_INITCALL(cam_jpeg_dma_init_module, cam_jpeg_dma_exit_module),
	CAM_INITCALL(cam_jpeg_dev_init_module, cam_jpeg_dev_exit_module),
	CAM_INITCALL(cam_fd_hw_init_module, cam_fd_hw_exit_module),
	CAM_INITCALL(cam_fd_dev_init_module, cam_fd_dev_exit_module),
	CAM_INITCALL(cam_lrme_hw_init_module, cam_lrme_hw_exit_module),
	CAM_INITCALL(cam_lrme_dev_init_module, cam_lrme_dev_exit_module),
	CAM_INITCALL(cam_custom_hw_sub_module_init,
		cam_custom_hw_sub_module_exit),
	CAM_INITCALL(cam_custom_csid_driver_init, cam_custom_csid_driver_exit),
	CAM_INITCALL(cam_custom_dev_init_module, cam_custom_dev_exit_module),
};

static void camera_unwind(int n)
{
	while (--n >= 0)
		camera_initcalls[n].exit();
}

static int __init camera_init(void)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(camera_initcalls); i++) {
		rc = camera_initcalls[i].init();
		if (rc) {
			pr_err("camera: %s failed: %d\n",
				camera_initcalls[i].name, rc);
			camera_unwind(i);
			return rc;
		}
	}

	/* late_initcall level: let deferred sub-driver probes finish first */
	wait_for_device_probe();

	/*
	 * A failing late_initcall was only logged when built in.  On kona
	 * cam_cci_late_init() always returns -EINVAL (the CCI subdev has no
	 * devnode), so neither result unwinds the module.
	 */
	rc = cam_req_mgr_late_init();
	if (rc)
		pr_err("camera: cam_req_mgr_late_init failed: %d\n", rc);

	rc = cam_cci_late_init();
	if (rc)
		pr_info("camera: cam_cci_late_init: %d (no CCI devnode)\n", rc);

	return 0;
}

static void __exit camera_exit(void)
{
	camera_unwind(ARRAY_SIZE(camera_initcalls));
}

module_init(camera_init);
module_exit(camera_exit);
MODULE_DESCRIPTION("LG kona Spectra camera (4.19 techpack, timelm port)");
MODULE_LICENSE("GPL v2");
