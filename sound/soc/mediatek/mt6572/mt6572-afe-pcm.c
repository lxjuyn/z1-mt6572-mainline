// SPDX-License-Identifier: GPL-2.0
/*
 * Mediatek ALSA SoC AFE platform driver for MT6572
 *
 * Skeleton driver modeled on sound/soc/mediatek/mt2701/mt2701-afe-pcm.c and
 * the MTK common ASoC layer (sound/soc/mediatek/common/).
 *
 * This is a COMPILE-ONLY skeleton: it builds and registers the platform
 * DAI / component drivers, but the Z1 mainline dtsi does not yet have an
 * audio (audsys) node at 0x11140000, so probe will not bind until that node
 * is added.  No audio path is guaranteed to produce sound.
 *
 * Copyright (c) 2016 MediaTek Inc.
 * Copyright (c) 2026 Z1 mainline port
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#include "mt6572-afe-reg.h"
#include "../common/mtk-base-afe.h"
#include "../common/mtk-afe-platform-driver.h"
#include "../common/mtk-afe-fe-dai.h"

/* ---------------------------------------------------------------------
 * memif / DAI ids
 * ------------------------------------------------------------------ */
enum {
	MT6572_MEMIF_DL1,
	MT6572_MEMIF_VUL,
	MT6572_MEMIF_NUM,
	/* BE DAIs */
	MT6572_IO_ADDA = MT6572_MEMIF_NUM,
};

enum {
	MT6572_IRQ1,
	MT6572_IRQ2,
	MT6572_IRQ_NUM,
};

struct mt6572_afe_private {
	struct clk *clk_afe;		/* CLK_TOP_AUDIO gate */
	struct clk *clk_intbus;		/* CLK_TOP_AUDIO_INTBUS_SEL mux */
};

/* ---------------------------------------------------------------------
 * PCM hardware params (shared by all FE memifs)
 * ------------------------------------------------------------------ */
static const struct snd_pcm_hardware mt6572_afe_hardware = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED
		| SNDRV_PCM_INFO_RESUME | SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.period_bytes_min = 1024,
	.period_bytes_max = 1024 * 256,
	.periods_min = 4,
	.periods_max = 1024,
	.buffer_bytes_max = 1024 * 1024,
	.fifo_size = 0,
};

/* ---------------------------------------------------------------------
 * Sampling-rate -> FS register value mapping (AFE_DAC_CON1 / AFE_IRQ_CON)
 * ------------------------------------------------------------------ */
struct mt6572_afe_rate {
	unsigned int rate;
	unsigned int regvalue;
};

static const struct mt6572_afe_rate mt6572_afe_rates[] = {
	{ .rate = 8000,		.regvalue = 0 },
	{ .rate = 11025,	.regvalue = 1 },
	{ .rate = 12000,	.regvalue = 2 },
	{ .rate = 16000,	.regvalue = 3 },
	{ .rate = 22050,	.regvalue = 4 },
	{ .rate = 24000,	.regvalue = 5 },
	{ .rate = 32000,	.regvalue = 6 },
	{ .rate = 44100,	.regvalue = 7 },
	{ .rate = 48000,	.regvalue = 8 },
};

static int mt6572_afe_fs(unsigned int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mt6572_afe_rates); i++)
		if (mt6572_afe_rates[i].rate == rate)
			return mt6572_afe_rates[i].regvalue;

	return -EINVAL;
}

static int mt6572_memif_fs(struct snd_pcm_substream *substream,
			   unsigned int rate)
{
	return mt6572_afe_fs(rate);
}

static int mt6572_irq_fs(struct snd_pcm_substream *substream,
			 unsigned int rate)
{
	return mt6572_afe_fs(rate);
}

/* ---------------------------------------------------------------------
 * Registers backed up across suspend
 * ------------------------------------------------------------------ */
static const unsigned int mt6572_afe_backup_list[] = {
	AFE_TOP_CON0,
	AFE_DAC_CON0,
	AFE_DAC_CON1,
	AFE_IRQ_CON,
};

/* ---------------------------------------------------------------------
 * Regmap config
 * ------------------------------------------------------------------ */
static const struct regmap_config mt6572_afe_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = AFE_MAX_REGISTER,
};

/* ---------------------------------------------------------------------
 * IRQ data (ASYS / AFE IRQ, one per memif, dynamic allocation)
 * ------------------------------------------------------------------ */
static const struct mtk_base_irq_data irq_data[MT6572_IRQ_NUM] = {
	{
		.id = MT6572_IRQ1,
		.irq_cnt_reg = AFE_IRQ_CNT1,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = AFE_IRQ_CON,
		.irq_fs_shift = AFE_IRQ_CON_IRQ1_FS_SHIFT,
		.irq_fs_maskbit = AFE_IRQ_CON_IRQ_FS_MASK,
		.irq_en_reg = AFE_IRQ_CON,
		.irq_en_shift = AFE_IRQ_CON_IRQ1_ON_SHIFT,
		.irq_clr_reg = AFE_IRQ_CLR,
		.irq_clr_shift = 0,
	},
	{
		.id = MT6572_IRQ2,
		.irq_cnt_reg = AFE_IRQ_CNT2,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = AFE_IRQ_CON,
		.irq_fs_shift = AFE_IRQ_CON_IRQ2_FS_SHIFT,
		.irq_fs_maskbit = AFE_IRQ_CON_IRQ_FS_MASK,
		.irq_en_reg = AFE_IRQ_CON,
		.irq_en_shift = AFE_IRQ_CON_IRQ2_ON_SHIFT,
		.irq_clr_reg = AFE_IRQ_CLR,
		.irq_clr_shift = 1,
	},
};

static irqreturn_t mt6572_afe_isr(int irq_id, void *dev)
{
	int id;
	struct mtk_base_afe *afe = dev;
	struct mtk_base_afe_memif *memif;
	struct mtk_base_afe_irq *irq;
	u32 status;

	regmap_read(afe->regmap, AFE_IRQ_STATUS, &status);
	regmap_write(afe->regmap, AFE_IRQ_CLR, status);

	for (id = 0; id < MT6572_MEMIF_NUM; ++id) {
		memif = &afe->memif[id];
		if (memif->irq_usage < 0)
			continue;

		irq = &afe->irqs[memif->irq_usage];
		if (status & BIT(irq->irq_data->irq_clr_shift))
			snd_pcm_period_elapsed(memif->substream);
	}

	return IRQ_HANDLED;
}

/* ---------------------------------------------------------------------
 * memif data (FE DAIs)
 * ------------------------------------------------------------------ */
static const struct mtk_base_memif_data memif_data_array[MT6572_MEMIF_NUM] = {
	[MT6572_MEMIF_DL1] = {
		.name = "DL1",
		.id = MT6572_MEMIF_DL1,
		.reg_ofs_base = AFE_DL1_BASE,
		.reg_ofs_cur = AFE_DL1_CUR,
		.reg_ofs_end = AFE_DL1_END,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = AFE_DAC_CON1_DL1_MODE_POS,
		.fs_maskbit = 0xf,
		.mono_reg = -1,
		.mono_shift = -1,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = AFE_DAC_CON0_DL1_ON_SHIFT,
		.hd_reg = -1,
		.hd_shift = 0,
		.hd_align_reg = -1,
		.hd_align_mshift = 0,
		.msb_reg = -1,
		.agent_disable_reg = -1,
		.agent_disable_shift = 0,
	},
	[MT6572_MEMIF_VUL] = {
		.name = "VUL",
		.id = MT6572_MEMIF_VUL,
		.reg_ofs_base = AFE_VUL_BASE,
		.reg_ofs_cur = AFE_VUL_CUR,
		.reg_ofs_end = AFE_VUL_END,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = AFE_DAC_CON1_VUL_MODE_POS,
		.fs_maskbit = 0xf,
		.mono_reg = AFE_DAC_CON1,
		.mono_shift = AFE_DAC_CON1_VUL_R_MONO_POS,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = AFE_DAC_CON0_VUL_ON_SHIFT,
		.hd_reg = -1,
		.hd_shift = 0,
		.hd_align_reg = -1,
		.hd_align_mshift = 0,
		.msb_reg = -1,
		.agent_disable_reg = -1,
		.agent_disable_shift = 0,
	},
};

/* ---------------------------------------------------------------------
 * Clock control
 * ------------------------------------------------------------------ */
static int mt6572_afe_enable_clock(struct mtk_base_afe *afe)
{
	struct mt6572_afe_private *afe_priv = afe->platform_priv;
	int ret;

	/* Enable the intbus mux and the AFE gate clock if present in DT. */
	if (afe_priv->clk_intbus) {
		ret = clk_prepare_enable(afe_priv->clk_intbus);
		if (ret)
			return ret;
	}

	if (afe_priv->clk_afe) {
		ret = clk_prepare_enable(afe_priv->clk_afe);
		if (ret)
			goto err_clk;
	}

	/* Power up the AFE: clear PDN_AFE / PDN_ADC (active-low). */
	regmap_update_bits(afe->regmap, AFE_TOP_CON0,
			   AFE_TOP_CON0_PDN_AFE | AFE_TOP_CON0_PDN_ADC, 0);

	/* Turn on the AFE core. */
	regmap_update_bits(afe->regmap, AFE_DAC_CON0,
			   AFE_DAC_CON0_AFE_ON, AFE_DAC_CON0_AFE_ON);

	return 0;

err_clk:
	clk_disable_unprepare(afe_priv->clk_intbus);
	return ret;
}

static int mt6572_afe_disable_clock(struct mtk_base_afe *afe)
{
	struct mt6572_afe_private *afe_priv = afe->platform_priv;

	regmap_update_bits(afe->regmap, AFE_DAC_CON0,
			   AFE_DAC_CON0_AFE_ON, 0);
	regmap_update_bits(afe->regmap, AFE_TOP_CON0,
			   AFE_TOP_CON0_PDN_AFE | AFE_TOP_CON0_PDN_ADC,
			   AFE_TOP_CON0_PDN_AFE | AFE_TOP_CON0_PDN_ADC);

	if (afe_priv->clk_afe)
		clk_disable_unprepare(afe_priv->clk_afe);
	if (afe_priv->clk_intbus)
		clk_disable_unprepare(afe_priv->clk_intbus);

	return 0;
}

static int mt6572_afe_init_clock(struct mtk_base_afe *afe)
{
	struct mt6572_afe_private *afe_priv = afe->platform_priv;

	/*
	 * TODO: the mt6572.dtsi audsys node does not exist yet.  Once the
	 * following node is added, these two clock names will resolve from
	 * topckgen (CLK_TOP_AUDIO, CLK_TOP_AUDIO_INTBUS_SEL):
	 *   audio: audio@11140000 {
	 *       compatible = "mediatek,mt6572-audio";
	 *       reg = <0x11140000 0x600>;
	 *       interrupts = <GIC_SPI ASYS_IRQ IRQ_TYPE_LEVEL_LOW>;
	 *       clocks = <&topckgen CLK_TOP_AUDIO>,
	 *                <&topckgen CLK_TOP_AUDIO_INTBUS_SEL>;
	 *       clock-names = "audio", "audio_intbus_sel";
	 *   };
	 */
	afe_priv->clk_afe = devm_clk_get(afe->dev, "audio");
	if (IS_ERR(afe_priv->clk_afe)) {
		if (PTR_ERR(afe_priv->clk_afe) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_warn(afe->dev, "failed to get \"audio\" clock (%ld), continuing\n",
			 PTR_ERR(afe_priv->clk_afe));
		afe_priv->clk_afe = NULL;
	}

	afe_priv->clk_intbus = devm_clk_get(afe->dev, "audio_intbus_sel");
	if (IS_ERR(afe_priv->clk_intbus)) {
		if (PTR_ERR(afe_priv->clk_intbus) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_warn(afe->dev, "failed to get \"audio_intbus_sel\" clock (%ld), continuing\n",
			 PTR_ERR(afe_priv->clk_intbus));
		afe_priv->clk_intbus = NULL;
	}

	return 0;
}

static int mt6572_afe_runtime_suspend(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);

	return mt6572_afe_disable_clock(afe);
}

static int mt6572_afe_runtime_resume(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);

	return mt6572_afe_enable_clock(afe);
}

/* ---------------------------------------------------------------------
 * FE DAI ops (thin wrappers over the common MTK helpers)
 * ------------------------------------------------------------------ */
static const struct snd_soc_dai_ops mt6572_memif_dai_ops = {
	.startup	= mtk_afe_fe_startup,
	.shutdown	= mtk_afe_fe_shutdown,
	.hw_params	= mtk_afe_fe_hw_params,
	.hw_free	= mtk_afe_fe_hw_free,
	.prepare	= mtk_afe_fe_prepare,
	.trigger	= mtk_afe_fe_trigger,
};

/* ---------------------------------------------------------------------
 * DAIs: FE = DL1 / VUL memif, BE = ADDA
 * ------------------------------------------------------------------ */
static struct snd_soc_dai_driver mt6572_afe_pcm_dais[] = {
	/* FE DAIs: memory interfaces to CPU */
	{
		.name = "PCM_DL1",
		.id = MT6572_MEMIF_DL1,
		.playback = {
			.stream_name = "DL1",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &mt6572_memif_dai_ops,
	},
	{
		.name = "PCM_VUL",
		.id = MT6572_MEMIF_VUL,
		.capture = {
			.stream_name = "VUL",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &mt6572_memif_dai_ops,
	},
	/* BE DAI: ADDA */
	{
		.name = "ADDA",
		.id = MT6572_IO_ADDA,
		.playback = {
			.stream_name = "ADDA Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.stream_name = "ADDA Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
	},
};

/* ---------------------------------------------------------------------
 * Minimal DAPM: ADDA path (DL1 -> DAC, ADC -> VUL)
 * ------------------------------------------------------------------ */
static const struct snd_soc_dapm_widget mt6572_afe_pcm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("ADDA Playback", "ADDA Playback", 0,
			    SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("ADDA Capture", "ADDA Capture", 0,
			     SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route mt6572_afe_pcm_routes[] = {
	/* DL1 (FE playback) -> ADDA DAC */
	{ "ADDA Playback", NULL, "DL1" },
	/* ADDA ADC -> VUL (FE capture) */
	{ "VUL", NULL, "ADDA Capture" },
};

static int mt6572_afe_pcm_probe(struct snd_soc_component *component)
{
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);

	snd_soc_component_init_regmap(component, afe->regmap);

	return 0;
}

static const struct snd_soc_component_driver mt6572_afe_pcm_dai_component = {
	.probe = mt6572_afe_pcm_probe,
	.name = "mt6572-afe-pcm-dai",
	.dapm_widgets = mt6572_afe_pcm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt6572_afe_pcm_widgets),
	.dapm_routes = mt6572_afe_pcm_routes,
	.num_dapm_routes = ARRAY_SIZE(mt6572_afe_pcm_routes),
	.suspend = mtk_afe_suspend,
	.resume = mtk_afe_resume,
};

/* ---------------------------------------------------------------------
 * Platform driver
 * ------------------------------------------------------------------ */
static int mt6572_afe_pcm_dev_probe(struct platform_device *pdev)
{
	struct mtk_base_afe *afe;
	struct mt6572_afe_private *afe_priv;
	struct device *dev;
	int i, irq_id, ret;

	afe = devm_kzalloc(&pdev->dev, sizeof(*afe), GFP_KERNEL);
	if (!afe)
		return -ENOMEM;

	afe->platform_priv = devm_kzalloc(&pdev->dev, sizeof(*afe_priv),
					  GFP_KERNEL);
	if (!afe->platform_priv)
		return -ENOMEM;

	afe_priv = afe->platform_priv;
	afe->dev = &pdev->dev;
	dev = afe->dev;

	/* AFE registers live at 0x11140000 (see reg property in DT node). */
	afe->base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(afe->base_addr))
		return PTR_ERR(afe->base_addr);

	afe->regmap = devm_regmap_init_mmio(dev, afe->base_addr,
					    &mt6572_afe_regmap_config);
	if (IS_ERR(afe->regmap))
		return PTR_ERR(afe->regmap);

	mutex_init(&afe->irq_alloc_lock);

	/* memif initialize */
	afe->memif_size = MT6572_MEMIF_NUM;
	afe->memif = devm_kcalloc(dev, afe->memif_size, sizeof(*afe->memif),
				  GFP_KERNEL);
	if (!afe->memif)
		return -ENOMEM;

	for (i = 0; i < afe->memif_size; i++) {
		afe->memif[i].data = &memif_data_array[i];
		afe->memif[i].irq_usage = -1;
	}

	/* irq initialize */
	afe->irqs_size = MT6572_IRQ_NUM;
	afe->irqs = devm_kcalloc(dev, afe->irqs_size, sizeof(*afe->irqs),
				 GFP_KERNEL);
	if (!afe->irqs)
		return -ENOMEM;

	for (i = 0; i < afe->irqs_size; i++)
		afe->irqs[i].irq_data = &irq_data[i];

	/* TODO: ASYS/AFE IRQ number for MT6572 is not confirmed yet; the
	 * vendor kernel routes it through the ASYS IRQ of the topckgen /
	 * sysirq domain.  platform_get_irq() reads the DT "interrupts"
	 * property once the audsys node exists.
	 *
	 * Framework-validation placeholder: the real MT6572 ASYS/AFE IRQ is
	 * not confirmed, so a missing/unrequestable IRQ must NOT abort the
	 * AFE probe.  Log and continue so the ASoC framework + MT6323 codec
	 * path can still be exercised on Z1 without sound output.
	 */
	irq_id = platform_get_irq(pdev, 0);
	if (irq_id < 0) {
		dev_warn(dev, "no ASYS/AFE IRQ (%d), continuing framework-only\n",
			 irq_id);
	} else {
		ret = devm_request_irq(dev, irq_id, mt6572_afe_isr,
				       IRQF_TRIGGER_NONE, "mt6572-afe-isr",
				       (void *)afe);
		if (ret)
			dev_warn(dev,
				 "request_irq for afe-isr failed (%d), continuing framework-only\n",
				 ret);
	}

	afe->mtk_afe_hardware = &mt6572_afe_hardware;
	afe->memif_fs = mt6572_memif_fs;
	afe->irq_fs = mt6572_irq_fs;
	afe->reg_back_up_list = mt6572_afe_backup_list;
	afe->reg_back_up_list_num = ARRAY_SIZE(mt6572_afe_backup_list);
	afe->runtime_resume = mt6572_afe_runtime_resume;
	afe->runtime_suspend = mt6572_afe_runtime_suspend;

	ret = mt6572_afe_init_clock(afe);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, afe);

	pm_runtime_enable(dev);
	if (!pm_runtime_enabled(dev)) {
		ret = mt6572_afe_runtime_resume(dev);
		if (ret)
			goto err_pm_disable;
	}
	pm_runtime_get_sync(dev);

	ret = devm_snd_soc_register_component(&pdev->dev, &mtk_afe_pcm_platform,
					      NULL, 0);
	if (ret) {
		dev_warn(dev, "err_platform\n");
		goto err_platform;
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &mt6572_afe_pcm_dai_component,
					      mt6572_afe_pcm_dais,
					      ARRAY_SIZE(mt6572_afe_pcm_dais));
	if (ret) {
		dev_warn(dev, "err_dai_component\n");
		goto err_platform;
	}

	return 0;

err_platform:
	pm_runtime_put_sync(dev);
err_pm_disable:
	pm_runtime_disable(dev);

	return ret;
}

static void mt6572_afe_pcm_dev_remove(struct platform_device *pdev)
{
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		mt6572_afe_runtime_suspend(&pdev->dev);
}

static const struct of_device_id mt6572_afe_pcm_dt_match[] = {
	{ .compatible = "mediatek,mt6572-audio" },
	{},
};
MODULE_DEVICE_TABLE(of, mt6572_afe_pcm_dt_match);

static const struct dev_pm_ops mt6572_afe_pm_ops = {
	RUNTIME_PM_OPS(mt6572_afe_runtime_suspend,
		       mt6572_afe_runtime_resume, NULL)
};

static struct platform_driver mt6572_afe_pcm_driver = {
	.driver = {
		.name = "mt6572-audio",
		.of_match_table = mt6572_afe_pcm_dt_match,
		.pm = pm_ptr(&mt6572_afe_pm_ops),
	},
	.probe = mt6572_afe_pcm_dev_probe,
	.remove = mt6572_afe_pcm_dev_remove,
};

module_platform_driver(mt6572_afe_pcm_driver);

MODULE_DESCRIPTION("Mediatek ALSA SoC AFE platform driver for 6572");
MODULE_AUTHOR("MediaTek Inc.");
MODULE_LICENSE("GPL v2");
