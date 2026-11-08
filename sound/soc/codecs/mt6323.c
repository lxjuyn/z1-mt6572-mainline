// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek MT6323 PMIC analog audio codec.
 *
 * The analog audio back-end (DAC, headphone driver, path to an external
 * speaker amp, and the MIC uplink: input mux, PGA, ADC, micbias) of the
 * MT6323 PMIC, reached over the parent mt6397 MFD pwrap regmap. The SoC-side
 * AFE and the sound card are separate drivers.
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#define MT6323_CODEC_RATES	SNDRV_PCM_RATE_8000_48000
#define MT6323_CODEC_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
				 SNDRV_PCM_FMTBIT_S24_LE | \
				 SNDRV_PCM_FMTBIT_S32_LE)

/*
 * Audio registers in the PMIC 16-bit space: ABB_AFE<->PMIC bridge @ 0x4000,
 * AUDTOP analog DAC/headphone/voice block @ 0x0700, internal class-D speaker
 * amplifier control @ 0x0052 (SPK_CON).
 */
#define ABB_AFE_CON(n)		(0x4000 + (n) * 2)
#define AUDTOP_CON(n)		(0x0700 + (n) * 2)
#define SPK_CON(n)		(0x0052 + (n) * 2)
/* Speaker clock gate lives in TOP_CKPDN1 (cleared to enable). */
#define PMIC_RG_AUD_SPK_PDN	0x000e
#define ABB_AFE_UP8X_FIFO_CFG0	0x401e
#define ABB_AFE_PMIC_NEWIF_CFG0	0x4024
#define ABB_AFE_PMIC_NEWIF_CFG1	0x4026
#define ABB_AFE_PMIC_NEWIF_CFG2	0x4028
#define ABB_AFE_PMIC_NEWIF_CFG3	0x402a

/* MT6323 audio clocks (TOP_CKPDN / TOP_CKCON). Atomic SET/CLR aliases. */
#define MT6323_TOP_CKPDN0_SET	0x0104
#define MT6323_TOP_CKPDN0_CLR	0x0106
#define PMIC_RG_CLKSQ_EN_AUD	BIT(0)
#define MT6323_TOP_CKPDN1_SET	0x010a
#define MT6323_TOP_CKPDN1_CLR	0x010c
#define PMIC_RG_AUD_26M_PDN	BIT(8)
#define MT6323_TOP_CKCON1_SET	0x0128
#define PMIC_CKCON1_AUD_BITS	GENMASK(13, 12)

/*
 * Output gain lives in the ZCD (zero-cross-detect) block. Each register packs
 * the L channel at [4:0] and R at [11:7]; the 5-bit field encodes +8dB (0) ..
 * 0dB (8) .. -10dB (18) .. -40dB (0x1f), -1dB per step (same layout as mt6358).
 */
#define ZCD_CON1		0x0802		/* lineout L/R gain */
#define ZCD_CON2		0x0804		/* headphone L/R gain */
#define ZCD_GAIN_0DB		8
#define ZCD_GAIN_N10DB		18
#define ZCD_GAIN_CTL_MAX	0x12		/* control range = +8dB .. -10dB */
#define ZCD_GAIN_REG(g)		(((g) << 7) | (g))

/*
 * Uplink (MIC capture) path, from the stock mt6572 audio HAL record flow:
 * the ABB AFE bridge has a UL lane (CON0[1]) whose rate/format lives in
 * CON1[4] and NEWIF_CFG2[11:10]; CON11[8] toggles a config-apply trigger.
 * The AUDTOP block carries the mic analog: PGA + ADC per channel, the input
 * mux in CON0[3:0]/CON1[7:4] (with the CON3 swap bits) and the micbias in
 * CON8[3]. Values mirror AudioPlatformDevice/AudioMachineDevice AnalogOpen/
 * Close(DEVICE_IN_ADC).
 */
#define ABB_AFE_CON0_UL_ON	BIT(1)
#define ABB_AFE_CON1_UL_RATE	BIT(4)		/* set = 48 kHz UL, clear = below */
#define ABB_AFE_CON11_TOP_TRIG	BIT(8)		/* config-apply trigger (toggle) */
#define AUDTOP_CON0_L_VCM_MASK	0x7f80		/* LCH 1.4V/2.4V buffers + UL VCM */
#define AUDTOP_CON0_L_VCM_ON	0x7800
#define AUDTOP_CON0_L_VCM_OFF	0x6000		/* vendor close, downlink off */
#define AUDTOP_CON0_L_MUX_MASK	0x000f		/* MIC1 = 0, MIC2 = 1, open = 3 */
#define AUDTOP_CON0_L_MUX_OPEN	0x0003
#define AUDTOP_CON0_L_PGA_MASK	0x0070
#define AUDTOP_CON0_L_PGA_6DB	0x0020
#define AUDTOP_CON0_L_ADC_MASK	0x0180		/* LCH ADC + PGA enable */
#define AUDTOP_CON1_R_MUX_MASK	0x00f0		/* MIC2 = 0x10, MIC1 = 0x40, open = 0x20 */
#define AUDTOP_CON1_R_MUX_OPEN	0x0020
#define AUDTOP_CON1_R_PGA_MASK	0x0700
#define AUDTOP_CON1_R_PGA_6DB	0x0200
#define AUDTOP_CON2_R_VCM_MASK	0x00ff
#define AUDTOP_CON2_R_VCM_ON	0x00f0
#define AUDTOP_CON2_R_VCM_OFF	0x00c0
#define AUDTOP_CON2_R_ADC_MASK	0x0003		/* RCH ADC + PGA enable */
#define AUDTOP_CON3_R_SWAP	BIT(9)		/* feed MIC1 to the R channel */
#define AUDTOP_CON8_MICBIAS	BIT(3)
#define NEWIF_CFG2_UL_BASE	0x302f		/* UL up8x rxif ADC voice mode */
#define NEWIF_CFG2_UL_RATE	GENMASK(11, 10)	/* 3 @48 kHz, 1 below */

struct mt6323_codec_priv {
	struct device *dev;
	struct regmap *regmap;		/* borrowed from the parent MT6323 MFD */
	unsigned int mic_mux;		/* "MIC Input Mux": 0 = MIC1, 1 = MIC2 */
	unsigned int mic_rate;		/* capture sample rate, from .hw_params */
	bool mic_pwr;			/* MIC UL analog currently powered */
	bool dl_pwr;			/* downlink DAC currently powered */
};

/*
 * Analog idle baseline from the stock power-on; the MT6323 audio fields are
 * undocumented. Per the BSP: NEWIF_CFG0 = sample_rate << 12 | 0x330, and
 * NEWIF_CFG2 = 0x302f selects the up8x receive-interface ADC voice mode.
 */
static const struct reg_sequence mt6323_codec_init[] = {
	{ MT6323_TOP_CKCON1_SET, PMIC_CKCON1_AUD_BITS },	/* audio clock domain */
	/* Analog AFE clock/format bridge to the codec. */
	{ ABB_AFE_CON(1),  0x0009 },
	{ ABB_AFE_CON(3),  0x0221 },
	{ ABB_AFE_CON(4),  0x0255 },
	{ ABB_AFE_CON(5),  0x0028 },
	{ ABB_AFE_CON(6),  0x0218 },
	{ ABB_AFE_CON(7),  0x0204 },
	{ ABB_AFE_CON(10), 0x0001 },
	/* Analog top: DAC, headphone buffer and bias -- idle baseline. */
	{ AUDTOP_CON(0),   0x6010 },
	{ AUDTOP_CON(1),   0x0140 },
	{ AUDTOP_CON(2),   0x00c0 },
	{ AUDTOP_CON(3),   0x0200 },
	{ AUDTOP_CON(5),   0x0014 },
	{ AUDTOP_CON(6),   0x37e2 },
	{ AUDTOP_CON(8),   0x0200 },
	{ AUDTOP_CON(9),   0x0008 },
	/* PMIC NewIF serial link to the SoC AFE (up8x FIFO + DL/UL config). */
	{ ABB_AFE_UP8X_FIFO_CFG0,  0x0001 },
	{ ABB_AFE_PMIC_NEWIF_CFG0, 0x7330 },	/* DL: sample_rate << 12 | 0x330 */
	{ ABB_AFE_PMIC_NEWIF_CFG1, 0x0018 },
	{ ABB_AFE_PMIC_NEWIF_CFG2, 0x302f },	/* UL up8x rxif ADC voice mode */
	{ ABB_AFE_PMIC_NEWIF_CFG3, 0xf872 },
	/* Conservative default analog output gain: headphone 0dB, lineout -10dB. */
	{ ZCD_CON2, ZCD_GAIN_REG(ZCD_GAIN_0DB) },
	{ ZCD_CON1, ZCD_GAIN_REG(ZCD_GAIN_N10DB) },
	{ SPK_CON(9), 0x0400 },		/* class-D speaker PGA gain +6dB; "Speaker Volume" owns it */
};

/* Audio clocks: CLKSQ + 26 MHz, gated with the stream. */
static int mt6323_clk_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct mt6323_codec_priv *priv =
		snd_soc_component_get_drvdata(snd_soc_dapm_to_component(w->dapm));

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_write(priv->regmap, MT6323_TOP_CKPDN0_SET, PMIC_RG_CLKSQ_EN_AUD);
		regmap_write(priv->regmap, MT6323_TOP_CKPDN1_CLR, PMIC_RG_AUD_26M_PDN);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_write(priv->regmap, MT6323_TOP_CKPDN1_SET, PMIC_RG_AUD_26M_PDN);
		regmap_write(priv->regmap, MT6323_TOP_CKPDN0_CLR, PMIC_RG_CLKSQ_EN_AUD);
		break;
	}
	return 0;
}

/* AFE<->PMIC serial bridge (NEWIF) downlink enable. */
static int mt6323_newif_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct mt6323_codec_priv *priv =
		snd_soc_component_get_drvdata(snd_soc_dapm_to_component(w->dapm));

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* enable the ABB AFE bridge (CON0) and its clock/format config (CON11) */
		regmap_update_bits(priv->regmap, ABB_AFE_CON(0),  0x0001, 0x0001);
		regmap_update_bits(priv->regmap, ABB_AFE_CON(11), 0x0303, 0x0303);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(priv->regmap, ABB_AFE_CON(11), 0x0303, 0x0000);
		regmap_update_bits(priv->regmap, ABB_AFE_CON(0),  0x0001, 0x0000);
		break;
	}
	return 0;
}

/* Analog DAC: bias + enable on power-up, back to the idle baseline on down. */
static int mt6323_dac_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct mt6323_codec_priv *priv =
		snd_soc_component_get_drvdata(snd_soc_dapm_to_component(w->dapm));

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		priv->dl_pwr = true;
		regmap_write(priv->regmap, AUDTOP_CON(5), 0x0014);	/* DAC output level: stock plays here; old 0x7714 over-drove the speaker */
		/* scoped to the DAC's own bits: the UL mic path shares CON0 */
		regmap_update_bits(priv->regmap, AUDTOP_CON(0), 0x7010, 0x7010);
		break;
	case SND_SOC_DAPM_POST_PMD:
		priv->dl_pwr = false;
		regmap_update_bits(priv->regmap, AUDTOP_CON(0), 0x7010, 0x6010);
		regmap_write(priv->regmap, AUDTOP_CON(5), 0x0014);	/* baseline */
		break;
	}
	return 0;
}

/*
 * Headphone driver. On power-up: depop config then enable. On power-down: drop
 * the output driver back to the idle baseline -- DAPM powers this down before
 * the DAC (output-to-input order), muting the HP before the feed stops.
 */
static int mt6323_hp_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct mt6323_codec_priv *priv =
		snd_soc_component_get_drvdata(snd_soc_dapm_to_component(w->dapm));

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_write(priv->regmap, AUDTOP_CON(6), 0xf5ba);	/* HP drv + depop */
		regmap_write(priv->regmap, AUDTOP_CON(4), 0x007c);	/* HP enable */
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_write(priv->regmap, AUDTOP_CON(4), 0x0000);	/* HP off */
		regmap_write(priv->regmap, AUDTOP_CON(6), 0x37e2);	/* baseline */
		break;
	}
	return 0;
}

/*
 * Internal class-D speaker (SPK_CON) fed by the AUDTOP voice/LCH DAC; the 1.35V
 * CM buffer comes up with the shared "DAC" widget. Sequence and values mirror
 * the stock HAL DEVICE_OUT_SPEAKER (class-D) path.
 */
static int mt6323_speaker_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct mt6323_codec_priv *priv =
		snd_soc_component_get_drvdata(snd_soc_dapm_to_component(w->dapm));
	int i;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Set voice buffer to smallest -22dB. */
		regmap_write(priv->regmap, AUDTOP_CON(7), 0x2400);
		/* enable input short of HP to prevent voice signal leakage . Enable 2.4V. */
		regmap_write(priv->regmap, AUDTOP_CON(6), 0xb7f6);
		/* enable clean 1.35VCM buffer in audioUL */
		regmap_update_bits(priv->regmap, AUDTOP_CON(0), 0x7000, 0xf000);
		/* enable audio bias. enable LCH DAC */
		regmap_write(priv->regmap, AUDTOP_CON(4), 0x0014);
		fsleep(10000);
		/* enable voice buffer and -11dB gain. Inter-connect voice buffer to SPK AMP */
		regmap_write(priv->regmap, AUDTOP_CON(7), 0x3550);
		/* Speaker clock */
		regmap_write(priv->regmap, MT6323_TOP_CKPDN1_CLR, PMIC_RG_AUD_SPK_PDN);
		/* enable classAB OC function */
		regmap_write(priv->regmap, SPK_CON(2), 0x0214);

		/* SPK_CON9 PGA gain is owned by the "Speaker Volume" control. */

		/* enable SPK-Amp with 0dB gain, enable SPK amp offset triming, select class D mode */
		regmap_write(priv->regmap, SPK_CON(0), 0x3008);
		/* Enable Class ABD */
		regmap_write(priv->regmap, SPK_CON(0), 0x3009);
		fsleep(5000);

		/* enable SPK AMP with 0dB gain, select Class D. enable Amp. */
		regmap_write(priv->regmap, SPK_CON(0), 0x3001);
		/* spk output stage enable and enable */
		regmap_write(priv->regmap, SPK_CON(12), 0x0a00);
		for (i = 6; i <= 11; i++) {
			fsleep(1000);
			/* enable voice buffer and +1dB gain. Inter-connect voice buffer to SPK AMP */
			regmap_write(priv->regmap, AUDTOP_CON(7), 0x3500 | (i << 4));
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		for (i = 10; i >= 5; i--) {
			/* ramp to -11dB. Inter-connect voice buffer to SPK AMP */
			regmap_write(priv->regmap, AUDTOP_CON(7), 0x3500 | (i << 4));
			fsleep(1000);
		}

		/* Mute Spk amp, select to original class AB mode. disable class-D Amp */
		regmap_write(priv->regmap, SPK_CON(0), 0x0004);
		/* Disable SPK output stage, disable spk amp. */
		regmap_write(priv->regmap, SPK_CON(12), 0x0000);
		/* Disable Speaker clock */
		regmap_write(priv->regmap, MT6323_TOP_CKPDN1_SET, PMIC_RG_AUD_SPK_PDN);
		/* set voice buffer gain as -22dB */
		regmap_write(priv->regmap, AUDTOP_CON(7), 0x2500);
		/* Disable voice buffer */
		regmap_write(priv->regmap, AUDTOP_CON(7), 0x2400);
		/* Disable audio bias and L-DAC */
		regmap_write(priv->regmap, AUDTOP_CON(4), 0x0000);
		/* Disable input short of HP drivers for voice signal leakage prevent and disable 2.4V reference buffer , audio DAC clock. */
		regmap_write(priv->regmap, AUDTOP_CON(6), 0x37e2);
		break;
	}
	return 0;
}

#define AUDTOP_CON7_TEARDOWN_MASK	GENMASK(7, 4)

/* Toggle the ABB top-control apply trigger (stock TopCtlChangeTrigger). */
static void mt6323_top_trigger(struct mt6323_codec_priv *priv)
{
	unsigned int con11;

	regmap_read(priv->regmap, ABB_AFE_CON(11), &con11);
	regmap_update_bits(priv->regmap, ABB_AFE_CON(11), ABB_AFE_CON11_TOP_TRIG,
			   (con11 & BIT(0)) ? 0 : ABB_AFE_CON11_TOP_TRIG);
}

/* Apply the mic input mux: L in AUDTOP_CON0[3:0], R follows via the swap bit */
/* (vendor single-mic default: both channels take the same mic). */
static void mt6323_mic_mux_apply(struct mt6323_codec_priv *priv)
{
	switch (priv->mic_mux) {
	case 1:	/* MIC2 */
		regmap_update_bits(priv->regmap, AUDTOP_CON(3), AUDTOP_CON3_R_SWAP, 0);
		regmap_update_bits(priv->regmap, AUDTOP_CON(0), AUDTOP_CON0_L_MUX_MASK, 0x0001);
		regmap_update_bits(priv->regmap, AUDTOP_CON(1), AUDTOP_CON1_R_MUX_MASK, 0x0010);
		break;
	default: /* MIC1 */
		regmap_update_bits(priv->regmap, AUDTOP_CON(3), AUDTOP_CON3_R_SWAP,
				   AUDTOP_CON3_R_SWAP);
		regmap_update_bits(priv->regmap, AUDTOP_CON(0), AUDTOP_CON0_L_MUX_MASK, 0x0000);
		regmap_update_bits(priv->regmap, AUDTOP_CON(1), AUDTOP_CON1_R_MUX_MASK, 0x0040);
		break;
	}
}

/*
 * MIC uplink analog: PMIC UL interface + AUDTOP mic path (buffers, PGA, ADC,
 * micbias), mirroring the stock HAL AnalogOpen/Close(DEVICE_IN_ADC). The
 * input mux (MIC1/MIC2) is the "MIC Input Mux" control; the physical mic pin
 * of the board is not known to the driver, so userspace can switch it.
 */
static int mt6323_mic_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct mt6323_codec_priv *priv = snd_soc_component_get_drvdata(
		snd_soc_dapm_to_component(w->dapm));
	unsigned int ul_newif = (priv->mic_rate == 48000) ? 3 : 1;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* PMIC UL digital interface (stock AnalogOpen DEVICE_IN_ADC) */
		regmap_write(priv->regmap, ABB_AFE_PMIC_NEWIF_CFG2,
			     NEWIF_CFG2_UL_BASE | (ul_newif << 10));
		regmap_update_bits(priv->regmap, ABB_AFE_CON(1), ABB_AFE_CON1_UL_RATE,
				   (priv->mic_rate == 48000) ? ABB_AFE_CON1_UL_RATE : 0);
		mt6323_top_trigger(priv);
		regmap_update_bits(priv->regmap, ABB_AFE_CON(0), ABB_AFE_CON0_UL_ON,
				   ABB_AFE_CON0_UL_ON);

		/* AUDTOP mic analog (stock AudioMachineDevice DEVICE_IN_ADC) */
		regmap_update_bits(priv->regmap, AUDTOP_CON(0),
				   AUDTOP_CON0_L_VCM_MASK, AUDTOP_CON0_L_VCM_ON);
		regmap_update_bits(priv->regmap, AUDTOP_CON(2),
				   AUDTOP_CON2_R_VCM_MASK, AUDTOP_CON2_R_VCM_ON);
		mt6323_mic_mux_apply(priv);
		regmap_update_bits(priv->regmap, AUDTOP_CON(1),
				   AUDTOP_CON1_R_PGA_MASK, AUDTOP_CON1_R_PGA_6DB);
		regmap_update_bits(priv->regmap, AUDTOP_CON(0),
				   AUDTOP_CON0_L_PGA_MASK, AUDTOP_CON0_L_PGA_6DB);
		regmap_update_bits(priv->regmap, AUDTOP_CON(8),
				   AUDTOP_CON8_MICBIAS, AUDTOP_CON8_MICBIAS);
		regmap_update_bits(priv->regmap, AUDTOP_CON(0),
				   AUDTOP_CON0_L_ADC_MASK, AUDTOP_CON0_L_ADC_MASK);
		regmap_update_bits(priv->regmap, AUDTOP_CON(2),
				   AUDTOP_CON2_R_ADC_MASK, AUDTOP_CON2_R_ADC_MASK);
		priv->mic_pwr = true;
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(priv->regmap, AUDTOP_CON(0),
				   AUDTOP_CON0_L_ADC_MASK, 0);
		regmap_update_bits(priv->regmap, AUDTOP_CON(2),
				   AUDTOP_CON2_R_ADC_MASK, 0);
		/* LCH VCM: drop 2.4V; keep the 1.4V rail while the DAC is on */
		regmap_update_bits(priv->regmap, AUDTOP_CON(0),
				   AUDTOP_CON0_L_VCM_MASK,
				   priv->dl_pwr ? 0x7000 : AUDTOP_CON0_L_VCM_OFF);
		regmap_update_bits(priv->regmap, AUDTOP_CON(2),
				   AUDTOP_CON2_R_VCM_MASK, AUDTOP_CON2_R_VCM_OFF);
		/* back to the vendor's open-mux idle state */
		regmap_update_bits(priv->regmap, AUDTOP_CON(3), AUDTOP_CON3_R_SWAP, 0);
		regmap_update_bits(priv->regmap, AUDTOP_CON(0),
				   AUDTOP_CON0_L_MUX_MASK, AUDTOP_CON0_L_MUX_OPEN);
		regmap_update_bits(priv->regmap, AUDTOP_CON(1),
				   AUDTOP_CON1_R_MUX_MASK, AUDTOP_CON1_R_MUX_OPEN);
		regmap_update_bits(priv->regmap, AUDTOP_CON(8),
				   AUDTOP_CON8_MICBIAS, 0);
		regmap_update_bits(priv->regmap, ABB_AFE_CON(0), ABB_AFE_CON0_UL_ON, 0);
		priv->mic_pwr = false;
		break;
	}
	return 0;
}

static const char * const mt6323_mic_mux_texts[] = { "MIC1", "MIC2" };
static const struct soc_enum mt6323_mic_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(mt6323_mic_mux_texts),
			mt6323_mic_mux_texts);

static int mt6323_mic_mux_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	/*
	 * Z1 (2026-11-08): this control is created with SOC_ENUM_EXT, i.e. it is
	 * a PLAIN kcontrol, not a DAPM one.  snd_soc_dapm_kcontrol_to_component()
	 * treats kcontrol->private_value as a snd_soc_dapm_widget and reads
	 * widget->dapm at offset 4, which for a soc_enum* is NULL -> oops at
	 * 0x00000004 the first time tinymix lists controls (the board rebooted
	 * in a loop with PC=snd_soc_dapm_kcontrol_to_component,
	 * LR=mt6323_mic_mux_get).  Use the plain-kcontrol accessor instead.
	 */
	struct mt6323_codec_priv *priv = snd_soc_component_get_drvdata(
		(struct snd_soc_component *)snd_kcontrol_chip(kcontrol));

	ucontrol->value.enumerated.item[0] = priv->mic_mux;
	return 0;
}

static int mt6323_mic_mux_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	/* see mt6323_mic_mux_get(): plain kcontrol, not a DAPM one */
	struct mt6323_codec_priv *priv = snd_soc_component_get_drvdata(
		(struct snd_soc_component *)snd_kcontrol_chip(kcontrol));
	unsigned int item = ucontrol->value.enumerated.item[0];

	if (item > 1)
		return -EINVAL;
	if (item == priv->mic_mux)
		return 0;
	priv->mic_mux = item;
	/* live re-route while the mic path is powered, else applied on power-up */
	if (priv->mic_pwr)
		mt6323_mic_mux_apply(priv);
	return 1;
}


static int mt6323_earpiece_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *kcontrol, int event)
{
	struct mt6323_codec_priv *priv = snd_soc_component_get_drvdata(
		snd_soc_dapm_to_component(w->dapm));
	u32 val, target_vol;
	int i;

	regmap_read(priv->regmap, AUDTOP_CON(7), &val);
	target_vol = FIELD_GET(AUDTOP_CON7_TEARDOWN_MASK, val);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Set voice buffer to smallest -22dB */
		regmap_write(priv->regmap, AUDTOP_CON(7), 0x2400);
		/* enable input short of HP to prevent voice signal leakage . Enable 2.4V. */
		regmap_write(priv->regmap, AUDTOP_CON(6), 0xb7f6);
		/* enable clean 1.35VCM buffer in audioUL */
		regmap_update_bits(priv->regmap, AUDTOP_CON(0), 0x7000, 0xf000);
		/* enable audio bias. enable LCH DAC */
		regmap_write(priv->regmap, AUDTOP_CON(4), 0x0014);

		for (i = 0; i <= target_vol; i++) {
			fsleep(5000);
			regmap_write(priv->regmap, AUDTOP_CON(7), 0x2500 | (i << 4));
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		for (i = target_vol - 1; i >= 0; i--) {
			fsleep(5000);
			regmap_write(priv->regmap, AUDTOP_CON(7), 0x2500 | (i << 4));
		}

		/* Disable voice buffer but keep volume so we know the state */
		regmap_write(priv->regmap, AUDTOP_CON(7), 0x2400 | (target_vol << 4));
		/* Disable audio bias and L-DAC */
		regmap_write(priv->regmap, AUDTOP_CON(4), 0x0000);
		/* Disable input short of HP drivers for voice signal leakage prevent and disable 2.4V reference buffer , audio DAC clock. */
		regmap_write(priv->regmap, AUDTOP_CON(6), 0x37e2);
		break;
	}
	return 0;
}

static const struct snd_kcontrol_new spk_route_switch =
	SOC_DAPM_SINGLE_VIRT("Switch", 1);

static const struct snd_kcontrol_new earpiece_route_switch =
	SOC_DAPM_SINGLE_VIRT("Switch", 1);

static const struct snd_kcontrol_new mic_route_switch =
	SOC_DAPM_SINGLE_VIRT("Switch", 1);

static const struct snd_soc_dapm_widget mt6323_dapm_widgets[] = {
	SND_SOC_DAPM_SWITCH("Speaker Route", SND_SOC_NOPM, 0, 0, &spk_route_switch),
	SND_SOC_DAPM_SWITCH("Earpiece Route", SND_SOC_NOPM, 0, 0, &earpiece_route_switch),
	SND_SOC_DAPM_INPUT("MIC Input"),
	SND_SOC_DAPM_SWITCH("MIC Route", SND_SOC_NOPM, 0, 0, &mic_route_switch),
	SND_SOC_DAPM_SUPPLY("MIC UL", SND_SOC_NOPM, 0, 0, mt6323_mic_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY("AUDCLK", SND_SOC_NOPM, 0, 0, mt6323_clk_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("NEWIF", SND_SOC_NOPM, 0, 0, mt6323_newif_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("DAC", NULL, SND_SOC_NOPM, 0, 0, mt6323_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUT_DRV_E("HP Driver", SND_SOC_NOPM, 0, 0, NULL, 0,
			       mt6323_hp_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUTPUT("Headphone"),
	SND_SOC_DAPM_OUT_DRV_E("Speaker Driver", SND_SOC_NOPM, 0, 0, NULL, 0,
			       mt6323_speaker_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUT_DRV_E("Earpiece Driver", SND_SOC_NOPM, 0, 0, NULL, 0,
			       mt6323_earpiece_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route mt6323_dapm_routes[] = {
	{ "DAC", NULL, "AIF1 Playback" },
	{ "DAC", NULL, "AUDCLK" },
	{ "DAC", NULL, "NEWIF" },
	{ "HP Driver", NULL, "DAC" },
	{ "Headphone", NULL, "HP Driver" },

	{ "Speaker Route", "Switch", "DAC" },
	{ "Speaker Driver", NULL, "Speaker Route" },

	{ "Earpiece Route", "Switch", "DAC" },
	{ "Earpiece Driver", NULL, "Earpiece Route" },

	{ "MIC Route", "Switch", "MIC Input" },
	/* Z1: MIC UL is a SUPPLY; it may not be the sink of a switch.
	 * Feed the switch from the supply instead (same power-up ordering). */
	{ "MIC Route", NULL, "MIC UL" },
	{ "AIF1 Capture", NULL, "MIC UL" },
};

/*
 * Per-output mute switches. Component-scoped pin-switch ops -- the generic
 * SOC_DAPM_PIN_SWITCH expects a card and faults on a component.
 */
#define MT6323_PIN_SWITCH(xname) { \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname " Switch", \
	.info = snd_soc_dapm_info_pin_switch, \
	.get = snd_soc_dapm_get_component_pin_switch, \
	.put = snd_soc_dapm_put_component_pin_switch, \
	.private_value = (unsigned long)xname }

/* Output volume: -10dB .. +8dB in 1dB steps (the inverted ZCD gain field). */
static const DECLARE_TLV_DB_SCALE(mt6323_dl_tlv, -1000, 100, 0);

/*
 * Class-D speaker PGA gain SPK_CON9[11:8]. Stock dB table {-60,0,4,5,...,17} for
 * index 0..15 (0 = mute, 1 = 0dB, 2 = +4dB, then +1dB/step). Analog volume --
 * scales after the DAC, unlike the digital "Playback Volume".
 */
static const DECLARE_TLV_DB_RANGE(mt6323_spk_tlv,
	0, 0, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 1),
	1, 1, TLV_DB_SCALE_ITEM(0, 0, 0),
	2, 15, TLV_DB_SCALE_ITEM(400, 100, 0));

/* Voice buffer gain AUDTOP_CON7[7:4]. -22dB to +8dB in 2dB steps. */
static const DECLARE_TLV_DB_SCALE(mt6323_voice_tlv, -2200, 200, 0);

static const struct snd_kcontrol_new mt6323_snd_controls[] = {
	MT6323_PIN_SWITCH("Headphone"),
	SOC_DOUBLE_TLV("Headphone Volume", ZCD_CON2, 0, 7, ZCD_GAIN_CTL_MAX, 1,
		       mt6323_dl_tlv),
	SOC_DOUBLE_TLV("Lineout Volume", ZCD_CON1, 0, 7, ZCD_GAIN_CTL_MAX, 1,
		       mt6323_dl_tlv),
	SOC_SINGLE_TLV("Speaker Volume", SPK_CON(9), 8, 0x0f, 0, mt6323_spk_tlv),
	SOC_SINGLE_TLV("Earpiece Volume", AUDTOP_CON(7), 4, 15, 0, mt6323_voice_tlv),
	SOC_ENUM_EXT("MIC Input Mux", mt6323_mic_mux_enum,
		     mt6323_mic_mux_get, mt6323_mic_mux_put),
};

static int mt6323_component_probe(struct snd_soc_component *component)
{
	struct mt6323_codec_priv *priv = snd_soc_component_get_drvdata(component);

	/*
	 * SOC_* mixer controls reach the PMIC through the component regmap. Without
	 * this the control writes go to a soft cache only -- reads look right but
	 * nothing changes in hardware, and snd_soc_put_volsw returns -EIO (which
	 * userspace tools report as "invalid value"). The DAPM events write
	 * priv->regmap directly, so they were unaffected.
	 */
	snd_soc_component_init_regmap(component, priv->regmap);
	return 0;
}

/* Remember the capture rate: the PMIC UL interface is rate-configured. */
static int mt6323_hw_params(struct snd_soc_component *component,
			    struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params)
{
	struct mt6323_codec_priv *priv = snd_soc_component_get_drvdata(component);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		priv->mic_rate = params_rate(params);
	return 0;
}

static const struct snd_soc_component_driver mt6323_soc_component_driver = {
	.probe			= mt6323_component_probe,
	.controls		= mt6323_snd_controls,
	.num_controls		= ARRAY_SIZE(mt6323_snd_controls),
	.dapm_widgets		= mt6323_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(mt6323_dapm_widgets),
	.dapm_routes		= mt6323_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(mt6323_dapm_routes),
	.hw_params		= mt6323_hw_params,
	.endianness		= 1,
};

static struct snd_soc_dai_driver mt6323_dai_driver[] = {
	{
		.name = "mt6323-snd-codec-aif1",
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MT6323_CODEC_RATES,
			.formats = MT6323_CODEC_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MT6323_CODEC_RATES,
			.formats = MT6323_CODEC_FORMATS,
		},
	},
};

static int mt6323_codec_probe(struct platform_device *pdev)
{
	struct mt6397_chip *mt6397 = dev_get_drvdata(pdev->dev.parent);
	struct mt6323_codec_priv *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	priv->regmap = mt6397->regmap;
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);
	priv->mic_mux = 0;		/* MIC1, the vendor single-mic default */
	priv->mic_rate = 48000;
	platform_set_drvdata(pdev, priv);

	/* Analog + NEWIF idle baseline; DAPM powers the output path per-stream. */
	ret = regmap_multi_reg_write(priv->regmap, mt6323_codec_init,
				     ARRAY_SIZE(mt6323_codec_init));
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to init analog codec\n");

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &mt6323_soc_component_driver,
					      mt6323_dai_driver,
					      ARRAY_SIZE(mt6323_dai_driver));
	if (ret)
		return ret;

	return 0;
}

static const struct of_device_id mt6323_codec_of_match[] = {
	/*
	 * Z1 tree binds the codec under the PMIC's "mt6397codec" node with
	 * compatible "mediatek,mt6323-codec" (arch/arm/boot/dts/mediatek/
	 * mt6323.dtsi). Upstream gabin8 used "mediatek,mt6323-sound"; the
	 * compatible is unified here to match the in-tree DT.
	 */
	{ .compatible = "mediatek,mt6323-codec", },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6323_codec_of_match);

static struct platform_driver mt6323_codec_driver = {
	.driver = {
		.name = "mt6323-sound",
		.of_match_table = mt6323_codec_of_match,
	},
	.probe = mt6323_codec_probe,
};
module_platform_driver(mt6323_codec_driver);

MODULE_DESCRIPTION("MediaTek MT6323 PMIC audio codec driver");
MODULE_LICENSE("GPL");
