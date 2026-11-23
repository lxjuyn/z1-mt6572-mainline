// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek MT6572 + MT6323 sound card.
 *
 * Machine driver binding the MT6572 AFE platform (DL1 playback, AWB capture)
 * to the MT6323 PMIC analog codec, with headphone-jack detection that
 * auto-routes between the speaker and the headphones, and an optional external
 * speaker amplifier. The AWB capture link carries either the FM receiver's
 * I2S input or the MT6323 mic uplink (hearing-aid loopback), selected by the
 * AFE's "AWB Source" mux plus the codec's "MIC Route" switch.
 * Modelled on the mt8183-mt6358 PMIC-codec card.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include "../../codecs/mt6323-accdet.h"
#include "../common/mtk-soundcard-driver.h"
#include "../common/mtk-soc-card.h"

#include <sound/jack.h>
#include <sound/soc.h>

/* The factory Z1 switches the external speaker path through GPIO140.
 * Keep it off until the codec route is powered, and mute it before teardown.
 */
static int mt6572_speaker_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_card *card = snd_soc_dapm_to_card(w->dapm);
	struct mtk_soc_card_data *data = snd_soc_card_get_drvdata(card);
	struct gpio_desc *amp = data->mach_priv;

	if (!amp)
		return 0;

	if (event == SND_SOC_DAPM_POST_PMU) {
		gpiod_set_value_cansleep(amp, 1);
		msleep(10);
	} else if (event == SND_SOC_DAPM_PRE_PMD) {
		gpiod_set_value_cansleep(amp, 0);
	}

	return 0;
}

static const struct snd_soc_dapm_widget mt6572_mt6323_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", mt6572_speaker_event),
	SND_SOC_DAPM_SPK("Earpiece", NULL),
};

/* The codec owns the analog driver widgets while this card owns the board
 * endpoints.  Without these cross-component edges DAPM can power the DAC but
 * never reaches the physical speaker/earpiece sink. */
static const struct snd_soc_dapm_route mt6572_mt6323_routes[] = {
	{ "Speaker", NULL, "Speaker Driver" },
	{ "Earpiece", NULL, "Earpiece Driver" },
};

/* The Z1 external amplifier takes the HP signal via the speaker route
 * switch. Its endpoint and the headphone jack remain independently gated.
 */
static const struct snd_soc_dapm_route z1_routes[] = {
	{ "Speaker", NULL, "Speaker Route" },
	{ "Earpiece", NULL, "Earpiece Driver" },
};

/*
 * Lets userspace keep the speaker on with the headphones in; each jack event
 * re-applies the automatic route. The codec provides the Headphone switch.
 */
static const struct snd_kcontrol_new mt6572_mt6323_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
};

/*
 * The codec is bound by of_node (see soc_card_probe): an MFD child's ASoC
 * component name is fmt_single_name(dev)-based ("mt6323-codec.N"), never its
 * DT node name, so a COMP_CODEC("mt6397codec", ...) string can never match.
 * The of_node is parsed from the card's "mediatek,audio-codec" phandle and
 * installed on the DL1 link at probe time.  The DAI name below is exact
 * (mt6323.c:418).
 */
SND_SOC_DAILINK_DEFS(playback,
	DAILINK_COMP_ARRAY(COMP_CPU("mt6572-afe-dl1")),
	DAILINK_COMP_ARRAY(COMP_CODEC("mt6397codec", "mt6323-snd-codec-aif1")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(awb,
	DAILINK_COMP_ARRAY(COMP_CPU("mt6572-afe-awb")),
	DAILINK_COMP_ARRAY(COMP_CODEC("mt6397codec", "mt6323-snd-codec-aif1")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link mt6572_mt6323_dai_links[] = {
	{
		.name = "DL1",
		.stream_name = "DL1 Playback",
		SND_SOC_DAILINK_REG(playback),
	},
	{
		.name = "AWB",
		.stream_name = "AWB Capture",
		.capture_only = 1,
		SND_SOC_DAILINK_REG(awb),
	},
};

static struct snd_soc_jack mt6572_mt6323_hp_jack;

static struct snd_soc_jack_pin mt6572_mt6323_jack_pins[] = {
	{ .pin = "Headphone", .mask = SND_JACK_HEADPHONE },
	{ .pin = "Speaker", .mask = SND_JACK_HEADPHONE, .invert = 1 },
};

static int mt6572_mt6323_accdet_init(struct snd_soc_pcm_runtime *rtd)
{
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(rtd->card);
	int ret;

	if (!soc_card_data->accdet)
		return 0;

	ret = snd_soc_card_jack_new_pins(rtd->card, "Headphone Jack",
				   SND_JACK_HEADSET,
				   &mt6572_mt6323_hp_jack, mt6572_mt6323_jack_pins,
				   ARRAY_SIZE(mt6572_mt6323_jack_pins));
	if (ret) {
		dev_err(rtd->dev, "Headphone Jack create failed: %d\n", ret);
		return ret;
	}

	ret = mt6323_accdet_enable_jack_detect(soc_card_data->accdet, &mt6572_mt6323_hp_jack);
	if (ret) {
		dev_err(rtd->dev, "Headphone Jack enable failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int mt6572_mt6323_soc_card_probe(struct mtk_soc_card_data *soc_card_data,
					bool legacy)
{
	struct snd_soc_card *card = soc_card_data->card_data->card;
	struct device_node *codec_np;
	struct gpio_desc *amp;
	struct snd_soc_dai_link *dai_link;
	int i;

	(void)legacy;

	amp = devm_gpiod_get_optional(card->dev, "speaker-enable", GPIOD_OUT_LOW);
	if (IS_ERR(amp))
		return dev_err_probe(card->dev, PTR_ERR(amp), "cannot get speaker enable GPIO\n");
	soc_card_data->mach_priv = amp;
	if (of_machine_is_compatible("jbo,z1")) {
		if (!amp)
			return dev_err_probe(card->dev, -EINVAL, "missing Z1 speaker enable GPIO\n");
		card->dapm_routes = z1_routes;
		card->num_dapm_routes = ARRAY_SIZE(z1_routes);
	}

	/*
	 * Bind the codec by of_node, not by component name.  An MFD child's
	 * ASoC component name comes from fmt_single_name(dev) (the platform
	 * device name, "mt6323-codec.N") -- never from its DT node name -- so
	 * COMP_CODEC("mt6397codec", ...) fails snd_soc_is_matching_component()'s
	 * strict strcmp and the card defers forever ("Cannot register card").
	 * The z1 dts carries the phandle directly (mediatek,audio-codec =
	 * <&codec> -> mt6397codec), which equals the MFD child's of_node.
	 * The reference is kept for the card's lifetime; the card device is
	 * this module's only consumer, so an explicit of_node_put on removal
	 * is unnecessary here.
	 */
	codec_np = of_parse_phandle(card->dev->of_node, "mediatek,audio-codec", 0);
	if (!codec_np) {
		dev_err(card->dev, "mediatek,audio-codec phandle missing\n");
		return -EINVAL;
	}

	for_each_card_prelinks(card, i, dai_link) {
		if (strcmp(dai_link->name, "DL1") == 0) {
			dai_link->codecs->of_node = codec_np;
			dai_link->codecs->name = NULL;
			dai_link->init = mt6572_mt6323_accdet_init;
		} else if (strcmp(dai_link->name, "AWB") == 0) {
			/* the AWB capture link needs the codec DAI behind it so
			 * DAPM can power the MIC uplink path when recording */
			dai_link->codecs->of_node = codec_np;
			dai_link->codecs->name = NULL;
		}
	}

	return 0;
}

static struct snd_soc_card mt6572_mt6323_card = {
	.name = "mt6572-mt6323",
	.owner = THIS_MODULE,
	.dai_link = mt6572_mt6323_dai_links,
	.num_links = ARRAY_SIZE(mt6572_mt6323_dai_links),
	.controls = mt6572_mt6323_controls,
	.num_controls = ARRAY_SIZE(mt6572_mt6323_controls),
	.dapm_widgets = mt6572_mt6323_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt6572_mt6323_widgets),
	.dapm_routes = mt6572_mt6323_routes,
	.num_dapm_routes = ARRAY_SIZE(mt6572_mt6323_routes),
};

static const struct mtk_soundcard_pdata mt6572_card = {
	.card_name = "mt6572_mt6323",
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt6572_mt6323_card,
		.num_jacks = 1,
	},
	.soc_probe = mt6572_mt6323_soc_card_probe,
};

static const struct of_device_id mt6572_mt6323_dt_match[] = {
	{ .compatible = "mediatek,mt6572-mt6323-sound", .data = &mt6572_card },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mt6572_mt6323_dt_match);

static struct platform_driver mt6572_mt6323_driver = {
	.driver = {
		.name = "mt6572-mt6323",
		.of_match_table = mt6572_mt6323_dt_match,
	},
	.probe = mtk_soundcard_common_probe,
};
module_platform_driver(mt6572_mt6323_driver);

MODULE_DESCRIPTION("MediaTek MT6572 MT6323 sound card");
MODULE_LICENSE("GPL");
