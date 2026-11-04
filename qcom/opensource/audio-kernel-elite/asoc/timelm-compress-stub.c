// SPDX-License-Identifier: GPL-2.0-only
/*
 * timelm: placeholder for the "msm-compress-dsp" platform while the Elite
 * compress-offload driver (msm-compress-q6-v2.c) is not ported.
 *
 * kona.c has 8 compress FE dai links on this platform.  They must stay: an
 * FE's PCM/compress device number is its position in the card's link list,
 * and the vendor HAL (audio_platform_info.xml / mixer_paths.xml) addresses
 * FEs by those numbers.  Without a registered "msm-compress-dsp" component
 * the whole card would stay in probe-defer.
 *
 * Offload opens fail with -ENODEV, so the audio policy falls back to PCM
 * instead of playing silence into a DSP session that doesn't exist.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/compress_driver.h>
#include <sound/soc.h>

#define DRV_NAME "msm-compress-dsp"

static int timelm_compr_open(struct snd_soc_component *component,
			     struct snd_compr_stream *cstream)
{
	dev_info_ratelimited(component->dev,
		"compress offload not ported yet, open rejected\n");
	return -ENODEV;
}

static const struct snd_compress_ops timelm_compr_ops = {
	.open = timelm_compr_open,
};

static const struct snd_soc_component_driver timelm_compr_component = {
	.name = DRV_NAME,
	.compress_ops = &timelm_compr_ops,
};

static int timelm_compr_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "compress offload stub registered\n");
	return devm_snd_soc_register_component(&pdev->dev,
					       &timelm_compr_component,
					       NULL, 0);
}

static const struct of_device_id timelm_compr_dt_match[] = {
	{ .compatible = "qcom,msm-compress-dsp" },
	{ }
};
MODULE_DEVICE_TABLE(of, timelm_compr_dt_match);

static struct platform_driver timelm_compr_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = timelm_compr_dt_match,
		.suppress_bind_attrs = true,
	},
	.probe = timelm_compr_probe,
};

int __init msm_compress_dsp_init(void)
{
	return platform_driver_register(&timelm_compr_driver);
}

void msm_compress_dsp_exit(void)
{
	platform_driver_unregister(&timelm_compr_driver);
}
