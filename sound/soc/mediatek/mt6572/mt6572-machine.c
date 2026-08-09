// SPDX-License-Identifier: GPL-2.0
/*
 * mt6572-machine.c  --  MT6572 MT6323 ALSA SoC machine driver
 *
 * Minimal ASoC machine skeleton for the MT6572 AFE + MT6323 PMIC codec
 * audio path used on the Z1 hearing-aid device.
 *
 * Modeled on sound/soc/mediatek/mt6797/mt6797-mt6351.c (classic MTK
 * AFE + PMIC-codec machine, DPCM FE/BE).
 *
 * DAI NAME CONTRACT (strings must match the platform/codec drivers):
 *   - CPU (AFE FE playback) : "PCM_DL1"
 *   - CPU (AFE FE capture)  : "PCM_VUL"
 *   - CPU (AFE BE)          : "ADDA"
 *   - CODEC (MT6323 AIF1)   : "mt6323-snd-codec-aif1"
 *
 * NOTE: the AFE platform driver (mt6572-afe-pcm.c) currently registers its
 * FE/BE DAIs as "PCM_DL1"/"PCM_VUL"/"ADDA".  If it is later renamed to a
 * "mt6572-afe-pcm*" / "mt6572-afe-adda" scheme, the COMP_CPU() strings below
 * must be updated to match.
 *
 * TODO (runtime, not compile-blocking):
 *   - fill mt6572_audio_card_init(): real MT6323 DAI setup
 *     (snd_soc_dai_set_sysclk / snd_soc_dai_set_fmt) for the AFE BE <-> codec
 *   - fill mt6572_audio_card_hw_params(): AFE ADDA <-> MT6323 DAI format /
 *     channel map once the Z1 wiring is confirmed
 *   - add card->dapm_widgets / .dapm_routes / .controls when the AFE power
 *     sequence is defined
 */

#include <linux/array_size.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

/* Front End: PCM_DL1 playback */
SND_SOC_DAILINK_DEFS(playback1,
	DAILINK_COMP_ARRAY(COMP_CPU("PCM_DL1")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

/* Front End: PCM_VUL capture */
SND_SOC_DAILINK_DEFS(capture1,
	DAILINK_COMP_ARRAY(COMP_CPU("PCM_VUL")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

/* Back End: AFE ADDA <-> MT6323 codec AIF1 */
SND_SOC_DAILINK_DEFS(primary_codec,
	DAILINK_COMP_ARRAY(COMP_CPU("ADDA")),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "mt6323-snd-codec-aif1")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static int mt6572_audio_card_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	/*
	 * TODO: configure the AFE BE (ADDA) <-> MT6323 codec digital link.
	 * Once the Z1 wiring is confirmed, do e.g.:
	 *   struct snd_soc_pcm_runtime *rtd = substream->private_data;
	 *   struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	 *   struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	 *   snd_soc_dai_set_tdm_slot(codec_dai, ...);
	 *   snd_soc_dai_set_channel_map(codec_dai, ...);
	 */
	return 0;
}

/*
 * Machine stream ops for the AFE BE <-> codec link.  This is the "card ops"
 * hook where per-stream hardware setup would go.
 */
static const struct snd_soc_ops mt6572_audio_card_ops = {
	.hw_params = mt6572_audio_card_hw_params,
};

/*
 * BE DAI link init: called when the backend link is probed.  This is where
 * the AFE BE DAI gets connected to the MT6323 codec DAI (sysclk/format).
 */
static int mt6572_audio_card_init(struct snd_soc_pcm_runtime *rtd)
{
	/*
	 * TODO: real MT6323 DAI setup once the Z1 pwrap write gate is lifted
	 * (see sound/soc/codecs/mt6323.c).  Example:
	 *   struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	 *   snd_soc_dai_set_sysclk(codec_dai, 0, 26000000, 0);
	 *   snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_CBS_CFS |
	 *                       SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF);
	 */
	return 0;
}

static struct snd_soc_dai_link mt6572_mt6323_dai_links[] = {
	/* Front End links */
	{
		.name = "PCM_DL1",
		.stream_name = "DL1",
		.trigger = { SND_SOC_DPCM_TRIGGER_PRE,
			     SND_SOC_DPCM_TRIGGER_PRE },
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback1),
	},
	{
		.name = "PCM_VUL",
		.stream_name = "VUL",
		.trigger = { SND_SOC_DPCM_TRIGGER_PRE,
			     SND_SOC_DPCM_TRIGGER_PRE },
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture1),
	},
	/* Back End link: AFE ADDA <-> MT6323 codec */
	{
		.name = "MTK_Codec",
		.no_pcm = 1,
		.ignore_suspend = 1,
		.init = mt6572_audio_card_init,
		.ops = &mt6572_audio_card_ops,
		SND_SOC_DAILINK_REG(primary_codec),
	},
};

static struct snd_soc_card mt6572_mt6323_card = {
	.name = "mt6572-mt6323",
	.owner = THIS_MODULE,
	.dai_link = mt6572_mt6323_dai_links,
	.num_links = ARRAY_SIZE(mt6572_mt6323_dai_links),
};

static int mt6572_mt6323_dev_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &mt6572_mt6323_card;
	struct device_node *platform_node, *codec_node;
	struct snd_soc_dai_link *dai_link;
	int ret, i;

	card->dev = &pdev->dev;

	/* AFE platform component (mtk-afe-pcm on the "mediatek,mt6572-audio" node) */
	platform_node = of_parse_phandle(pdev->dev.of_node,
					 "mediatek,platform", 0);
	if (!platform_node) {
		dev_err(&pdev->dev, "Property 'platform' missing or invalid\n");
		return -EINVAL;
	}
	for_each_card_prelinks(card, i, dai_link) {
		if (dai_link->platforms->name)
			continue;
		dai_link->platforms->of_node = platform_node;
	}

	/* MT6323 codec node */
	codec_node = of_parse_phandle(pdev->dev.of_node,
				      "mediatek,audio-codec", 0);
	if (!codec_node) {
		dev_err(&pdev->dev,
			"Property 'audio-codec' missing or invalid\n");
		ret = -EINVAL;
		goto put_platform_node;
	}
	for_each_card_prelinks(card, i, dai_link) {
		if (dai_link->codecs->name)
			continue;
		dai_link->codecs->of_node = codec_node;
	}

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "%s snd_soc_register_card fail %d\n",
			__func__, ret);

	of_node_put(codec_node);
put_platform_node:
	of_node_put(platform_node);
	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id mt6572_mt6323_dt_match[] = {
	{ .compatible = "mediatek,mt6572-sound", },
	{}
};
MODULE_DEVICE_TABLE(of, mt6572_mt6323_dt_match);
#endif

static struct platform_driver mt6572_mt6323_driver = {
	.driver = {
		.name = "mt6572-sound",
#ifdef CONFIG_OF
		.of_match_table = mt6572_mt6323_dt_match,
#endif
	},
	.probe = mt6572_mt6323_dev_probe,
};

module_platform_driver(mt6572_mt6323_driver);

MODULE_DESCRIPTION("MT6572 MT6323 ALSA SoC machine driver");
MODULE_AUTHOR("Z1 mainline porting");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("mt6572 mt6323 soc card");
