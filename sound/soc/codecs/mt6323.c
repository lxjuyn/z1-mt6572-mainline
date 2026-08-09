// SPDX-License-Identifier: GPL-2.0
/*
 * MT6323 ALSA SoC audio codec driver
 *
 * Minimal mainline ASoC skeleton for the MT6323 PMIC audio codec as used
 * on the MT6572 based Z1 (hearing-aid) device.
 *
 * NOTE ON THE Z1 PWRAP WRITE GATE
 * -------------------------------
 * The Z1 pwrap has a write-denial gate (CONFIG_MTK_PMIC_WRAP_Z1_WRITE_DENY)
 * which currently rejects *all* MT6323 register writes. Every regmap write
 * performed by this driver will therefore be refused on the Z1 until that
 * gate is reworked on the pwrap side. This driver intentionally does NOT try
 * to bypass the gate: the Z1 power-on audio sequence is a separate step and
 * is left out here. As a result only a structure/compile-level skeleton is
 * provided; nothing is expected to produce sound on the Z1 in its current
 * state.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/mfd/mt6323/registers.h>
#include <sound/soc.h>

/* Same advanced-format set used by the other MediaTek PMIC codecs */
#define MT6323_SND_SOC_ADV_MT_FMTS \
	(SNDRV_PCM_FMTBIT_S16_LE | \
	 SNDRV_PCM_FMTBIT_S16_BE | \
	 SNDRV_PCM_FMTBIT_U16_LE | \
	 SNDRV_PCM_FMTBIT_U16_BE | \
	 SNDRV_PCM_FMTBIT_S24_LE | \
	 SNDRV_PCM_FMTBIT_S24_BE | \
	 SNDRV_PCM_FMTBIT_U24_LE | \
	 SNDRV_PCM_FMTBIT_U24_BE | \
	 SNDRV_PCM_FMTBIT_S32_LE | \
	 SNDRV_PCM_FMTBIT_S32_BE | \
	 SNDRV_PCM_FMTBIT_U32_LE | \
	 SNDRV_PCM_FMTBIT_U32_BE)

#define MT6323_SOC_HIGH_USE_RATE \
	(SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_192000)

struct mt6323_priv {
	struct device *dev;
	struct regmap *regmap;
};

/*
 * DAPM widgets.
 *
 * The MT6323 audio register blocks are:
 *   - SPK_CON0..12   (0x52..0x6a): speaker amplifier path
 *   - AUDTOP_CON0..9 (0x700..0x712): audio top control
 *   - TOP_CKPDN0..2  (0x102/0x108/0x10e): audio clock power-downs
 *
 * include/linux/mfd/mt6323/registers.h only provides the register *addresses*,
 * not the bit fields, so every widget below is registered at SND_SOC_NOPM.
 * TODO: once the MT6323 audio register bit semantics are mapped, replace the
 * NOPM widgets with the real reg/shift pairs (e.g. DAC/ADC power-up bits in
 * AUDTOP_CONx, SPK amp enable in SPK_CONx) and add the matching regmap
 * power-up/down event handlers.
 */
static const struct snd_soc_dapm_widget mt6323_dapm_widgets[] = {
	/* Power/clock supplies (bit semantics TODO) */
	SND_SOC_DAPM_SUPPLY("DL_DIG", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("UL_DIG", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SPK_ANA", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* AIF endpoints */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "MT6323 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "MT6323 Capture", 0, SND_SOC_NOPM, 0, 0),

	/* DAC / ADC */
	SND_SOC_DAPM_DAC("DACL", "MT6323 Playback", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DACR", "MT6323 Playback", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADCL", "MT6323 Capture", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADCR", "MT6323 Capture", SND_SOC_NOPM, 0, 0),

	/* Audio inputs / outputs */
	SND_SOC_DAPM_INPUT("Microphone"),
	SND_SOC_DAPM_OUTPUT("Headphone"),
	SND_SOC_DAPM_OUTPUT("Speaker"),
};

static const struct snd_soc_dapm_route mt6323_dapm_routes[] = {
	/* Playback: AIF -> DAC -> HP/SPK */
	{ "DACL", NULL, "AIF1RX" },
	{ "DACR", NULL, "AIF1RX" },
	{ "Headphone", NULL, "DACL" },
	{ "Headphone", NULL, "DACR" },
	{ "Speaker", NULL, "DACL" },
	{ "Speaker", NULL, "DACR" },

	/* Capture: MIC -> ADC -> AIF */
	{ "ADCL", NULL, "Microphone" },
	{ "ADCR", NULL, "Microphone" },
	{ "AIF1TX", NULL, "ADCL" },
	{ "AIF1TX", NULL, "ADCR" },

	/* Supply dependencies */
	{ "DACL", NULL, "DL_DIG" },
	{ "DACR", NULL, "DL_DIG" },
	{ "ADCL", NULL, "UL_DIG" },
	{ "ADCR", NULL, "UL_DIG" },
	{ "Headphone", NULL, "SPK_ANA" },
	{ "Speaker", NULL, "SPK_ANA" },
};

static struct snd_soc_dai_driver mt6323_snd_codec_dai[] = {
	{
		.name = "mt6323-snd-codec-aif1",
		.playback = {
			.stream_name = "MT6323 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MT6323_SOC_HIGH_USE_RATE,
			.formats = MT6323_SND_SOC_ADV_MT_FMTS,
		},
		.capture = {
			.stream_name = "MT6323 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MT6323_SOC_HIGH_USE_RATE,
			.formats = MT6323_SND_SOC_ADV_MT_FMTS,
		},
	},
};

static int mt6323_codec_probe(struct snd_soc_component *component)
{
	struct mt6323_priv *priv = snd_soc_component_get_drvdata(component);

	snd_soc_component_init_regmap(component, priv->regmap);

	/*
	 * NOTE: on the Z1 every MT6323 write is rejected by the pwrap write
	 * gate (CONFIG_MTK_PMIC_WRAP_Z1_WRITE_DENY), so no audio power-on
	 * sequence is attempted here. TODO: add the MT6323 audio enable /
	 * regulator / clock sequence once the pwrap write gate is lifted.
	 */

	return 0;
}

static const struct snd_soc_component_driver mt6323_component_driver = {
	.probe = mt6323_codec_probe,
	.read = snd_soc_component_read,
	.write = snd_soc_component_write,
	.dapm_widgets = mt6323_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt6323_dapm_widgets),
	.dapm_routes = mt6323_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(mt6323_dapm_routes),
};

static int mt6323_platform_driver_probe(struct platform_device *pdev)
{
	struct mt6323_priv *priv;
	struct regmap *regmap;

	/*
	 * The regmap is owned by the pwrap device and shared by all the
	 * MT6323 MFD children. dev_get_regmap() is the canonical way to
	 * fetch it from the MFD parent; fall back to the MFD drvdata
	 * (struct mt6397_chip) pattern used by mt6357/mt6358 upstream.
	 */
	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap) {
		struct mt6397_chip *mt6397 = dev_get_drvdata(pdev->dev.parent);

		if (!mt6397)
			return -ENODEV;
		regmap = mt6397->regmap;
	}
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	priv->regmap = regmap;
	dev_set_drvdata(&pdev->dev, priv);

	return devm_snd_soc_register_component(&pdev->dev,
					       &mt6323_component_driver,
					       mt6323_snd_codec_dai,
					       ARRAY_SIZE(mt6323_snd_codec_dai));
}

static const struct platform_device_id mt6323_platform_ids[] = {
	{ "mt6323-sound", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, mt6323_platform_ids);

static const struct of_device_id mt6323_of_match[] = {
	{ .compatible = "mediatek,mt6323-codec" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mt6323_of_match);

static struct platform_driver mt6323_codec_driver = {
	.probe = mt6323_platform_driver_probe,
	.id_table = mt6323_platform_ids,
	.driver = {
		.name = "mt6323-sound",
		.of_match_table = mt6323_of_match,
	},
};

module_platform_driver(mt6323_codec_driver);

MODULE_DESCRIPTION("MT6323 ALSA SoC codec driver");
MODULE_AUTHOR("Z1 mainline porting");
MODULE_LICENSE("GPL");
