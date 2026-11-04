// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018, LG Eletronics. All rights reserved.
 *
 * timelm: trimmed port of LG's drivers/soc/qcom/lge/power/lrmc
 * (CONFIG_LGE_PM, 4.19) carried in camera.ko, its only user: cam_flash_core
 * raises the BOB rail with bob_mode_enable() while the flash fires high.
 * Same DT node/properties ("lge,lrmc", lrmc_bob_vreg-supply, bob_* and
 * def_* voltages).  Dropped: the bob_mode sysfs debug node and the pr_err
 * logging.  bob_mode_enable/disable are no-ops until the node has probed
 * (the 4.19 code dereferenced lrmcdata unconditionally).
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#define LRMC_DEV_NAME "lge,lrmc"

#define PM_BOB_DEF_MODE 0
#define PM_BOB_UP_MODE  1

/* CONFIG_MACH_KONA_TIMELM defaults */
#define BOB_UP_MIN_VOLT 3920000
#define BOB_UP_MAX_VOLT 3960000
#define BOB_DEF_MIN_VOLT 3312000
#define BOB_DEF_MAX_VOLT 3960000

struct lrmc_dev {
	struct device *dev;
	struct regulator *bob_vreg;
	int bob_current_mode;
	uint32_t bob_min_voltage;
	uint32_t bob_max_voltage;
	uint32_t def_min_voltage;
	uint32_t def_max_voltage;
};

static struct lrmc_dev *lrmcdata;

static void lrmc_read_u32(struct device_node *node, const char *name,
	uint32_t *val, uint32_t def)
{
	if (of_property_read_u32(node, name, val))
		*val = def;
}

void bob_mode_enable(void)
{
	int ret;

	if (!lrmcdata)
		return;

	ret = regulator_set_voltage(lrmcdata->bob_vreg,
			lrmcdata->bob_min_voltage, lrmcdata->bob_max_voltage);
	if (ret < 0)
		dev_err(lrmcdata->dev, "bob up: set_voltage %d\n", ret);

	lrmcdata->bob_current_mode = PM_BOB_UP_MODE;
}

void bob_mode_disable(void)
{
	int ret;

	if (!lrmcdata)
		return;

	ret = regulator_set_voltage(lrmcdata->bob_vreg,
			lrmcdata->def_min_voltage, lrmcdata->def_max_voltage);
	if (ret < 0)
		dev_err(lrmcdata->dev, "bob default: set_voltage %d\n", ret);

	lrmcdata->bob_current_mode = PM_BOB_DEF_MODE;
}

static int lge_regulator_mode_change_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct lrmc_dev *data;

	if (!node)
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->bob_vreg = devm_regulator_get(dev, "lrmc_bob_vreg");
	if (IS_ERR(data->bob_vreg))
		return dev_err_probe(dev, PTR_ERR(data->bob_vreg),
			"bob_vreg\n");

	lrmc_read_u32(node, "bob_min_voltage", &data->bob_min_voltage,
		BOB_UP_MIN_VOLT);
	lrmc_read_u32(node, "bob_max_voltage", &data->bob_max_voltage,
		BOB_UP_MAX_VOLT);
	lrmc_read_u32(node, "def_min_voltage", &data->def_min_voltage,
		BOB_DEF_MIN_VOLT);
	lrmc_read_u32(node, "def_max_voltage", &data->def_max_voltage,
		BOB_DEF_MAX_VOLT);

	data->dev = dev;
	lrmcdata = data;
	dev_info(dev, "bob up %u-%u uV, default %u-%u uV\n",
		data->bob_min_voltage, data->bob_max_voltage,
		data->def_min_voltage, data->def_max_voltage);

	return 0;
}

static int lge_regulator_mode_change_remove(struct platform_device *pdev)
{
	lrmcdata = NULL;
	return 0;
}

static const struct of_device_id lge_regulator_mode_change_match[] = {
	{.compatible = LRMC_DEV_NAME},
	{},
};

static struct platform_driver lge_regulator_mode_change_driver = {
	.probe          = lge_regulator_mode_change_probe,
	.remove         = lge_regulator_mode_change_remove,
	.driver         = {
		.name   = LRMC_DEV_NAME,
		.of_match_table = lge_regulator_mode_change_match,
	},
};

int lge_regulator_mode_change_init(void)
{
	return platform_driver_register(&lge_regulator_mode_change_driver);
}

void lge_regulator_mode_change_exit(void)
{
	platform_driver_unregister(&lge_regulator_mode_change_driver);
}
