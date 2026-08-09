/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt6572-afe-reg.h  --  Mediatek MT6572 audio front end register definition
 *
 * All offsets are relative to the AFE base 0x11140000 (32-bit registers).
 * Values taken from the vendor 3.4.67 kernel:
 *   wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/
 *     drivers/sound/AudDrv_Afe.h
 *
 * Copyright (c) 2016 MediaTek Inc.
 */

#ifndef _MT6572_REG_H_
#define _MT6572_REG_H_

/* ---- AFE global / top registers ---- */
#define AUDIO_AFE_TOP_CON0	0x0000
#define AUDIO_AFE_TOP_CON1	0x0004
#define AUDIO_AFE_TOP_CON2	0x0008
#define AUDIO_AFE_TOP_CON3	0x000c

#define AFE_DAC_CON0		0x0010
#define AFE_DAC_CON1		0x0014
#define AFE_I2S_CON		0x0018

#define AFE_CONN0		0x0020
#define AFE_CONN1		0x0024
#define AFE_CONN2		0x0028
#define AFE_CONN3		0x002c
#define AFE_CONN4		0x0030

#define AFE_I2S_CON1		0x0034
#define AFE_I2S_CON2		0x0038
#define AFE_I2S_CON3		0x004c

/* ---- Memory interfaces (memif) ---- */
#define AFE_DL1_BASE		0x0040
#define AFE_DL1_CUR		0x0044
#define AFE_DL1_END		0x0048
#define AFE_DL2_BASE		0x0050
#define AFE_DL2_CUR		0x0054
#define AFE_DL2_END		0x0058
#define AFE_AWB_BASE		0x0070
#define AFE_AWB_END		0x0078
#define AFE_AWB_CUR		0x007c
#define AFE_VUL_BASE		0x0080
#define AFE_VUL_END		0x0088
#define AFE_VUL_CUR		0x008c

#define AFE_MEMIF_MON0		0x00d0
#define AFE_MEMIF_MON1		0x00d4
#define AFE_MEMIF_MON2		0x00d8
#define AFE_MEMIF_MON4		0x00e0

/* ---- ADDA ---- */
#define AFE_ADDA_DL_SRC2_CON0	0x0108
#define AFE_ADDA_DL_SRC2_CON1	0x010c
#define AFE_ADDA_UL_SRC_CON0	0x0114
#define AFE_ADDA_UL_SRC_CON1	0x0118
#define AFE_ADDA_TOP_CON0	0x0120
#define AFE_ADDA_UL_DL_CON0	0x0124
#define AFE_ADDA_SRC_DEBUG	0x012c
#define AFE_ADDA_SRC_DEBUG_MON0	0x0130
#define AFE_ADDA_SRC_DEBUG_MON1	0x0134
#define AFE_ADDA_NEWIF_CFG0	0x0138
#define AFE_ADDA_NEWIF_CFG1	0x013c

/* ---- Top power / IRQ / maxlen ---- */
#define AFE_TOP_CON0		0x0200

#define AFE_ADDA_PREDIS_CON0	0x0260
#define AFE_ADDA_PREDIS_CON1	0x0264

#define AFE_MOD_PCM_BASE	0x0330
#define AFE_MOD_PCM_END		0x0338
#define AFE_MOD_PCM_CUR		0x033c

#define AFE_IRQ_CON		0x03a0
#define AFE_IRQ_STATUS		0x03a4
#define AFE_IRQ_CLR		0x03a8
#define AFE_IRQ_CNT1		0x03ac
#define AFE_IRQ_CNT2		0x03b0
#define AFE_IRQ_MON2		0x03b8
#define AFE_IRQ1_CNT_MON	0x03c0
#define AFE_IRQ2_CNT_MON	0x03c4
#define AFE_IRQ1_EN_CNT_MON	0x03c8
#define AFE_MEMIF_MAXLEN	0x03d4
#define AFE_MEMIF_PBUF_SIZE	0x03d8

/* ---- ASRC ---- */
#define AFE_ASRC_CON0		0x0500
#define AFE_ASRC_CON21		0x0570

#define AFE_MAX_REGISTER	AFE_ASRC_CON21

/* ---- AFE_TOP_CON0 (0x0200) bit masks ---- */
#define AFE_TOP_CON0_PDN_AFE	BIT(2)
#define AFE_TOP_CON0_PDN_ADC	BIT(5)
#define AFE_TOP_CON0_PDN_I2S	BIT(6)
#define AFE_TOP_CON0_APB_W2T	BIT(12)
#define AFE_TOP_CON0_APB_R2T	BIT(13)
#define AFE_TOP_CON0_APB3_SEL	BIT(14)

/* ---- AFE_DAC_CON0 (0x0010) bit masks ---- */
#define AFE_DAC_CON0_AFE_ON	BIT(0)
#define AFE_DAC_CON0_DL1_ON	BIT(1)
#define AFE_DAC_CON0_DL2_ON	BIT(2)
#define AFE_DAC_CON0_VUL_ON	BIT(3)
#define AFE_DAC_CON0_DAI_ON	BIT(4)
#define AFE_DAC_CON0_I2S_ON	BIT(5)
#define AFE_DAC_CON0_AWB_ON	BIT(6)

/* enable bit positions for memif (used with 1 << shift) */
#define AFE_DAC_CON0_DL1_ON_SHIFT	1
#define AFE_DAC_CON0_DL2_ON_SHIFT	2
#define AFE_DAC_CON0_VUL_ON_SHIFT	3
#define AFE_DAC_CON0_AWB_ON_SHIFT	6

/* ---- AFE_DAC_CON1 (0x0014) field positions / masks ---- */
#define AFE_DAC_CON1_DL1_MODE_POS	0
#define AFE_DAC_CON1_DL1_MODE_MASK	(0xf << 0)
#define AFE_DAC_CON1_DL2_MODE_POS	4
#define AFE_DAC_CON1_DL2_MODE_MASK	(0xf << 4)
#define AFE_DAC_CON1_I2S_MODE_POS	8
#define AFE_DAC_CON1_I2S_MODE_MASK	(0xf << 8)
#define AFE_DAC_CON1_AWB_MODE_POS	12
#define AFE_DAC_CON1_AWB_MODE_MASK	(0xf << 12)
#define AFE_DAC_CON1_VUL_MODE_POS	16
#define AFE_DAC_CON1_VUL_MODE_MASK	(0xf << 16)
#define AFE_DAC_CON1_VUL_R_MONO_POS	28
#define AFE_DAC_CON1_VUL_R_MONO_MASK	BIT(28)

/* ---- AFE_IRQ_CON (0x03a0) field positions ---- */
#define AFE_IRQ_CON_IRQ1_ON_SHIFT	0
#define AFE_IRQ_CON_IRQ2_ON_SHIFT	1
#define AFE_IRQ_CON_IRQ1_FS_SHIFT	4
#define AFE_IRQ_CON_IRQ2_FS_SHIFT	8
#define AFE_IRQ_CON_IRQ_FS_MASK		0xf

/* AFE_IRQ_STATUS (0x03a4) bit masks */
#define AFE_IRQ_STATUS_IRQ1	BIT(0)
#define AFE_IRQ_STATUS_IRQ2	BIT(1)

/* AFE_IRQ_CLR (0x03a8) bit masks */
#define AFE_IRQ_CLR_IRQ1	BIT(0)
#define AFE_IRQ_CLR_IRQ2	BIT(1)

#endif /* _MT6572_REG_H_ */
