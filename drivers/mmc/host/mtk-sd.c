// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2015, 2022 MediaTek Inc.
 * Author: Chaotian.Jing <chaotian.jing@mediatek.com>
 */

#include <linux/module.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeirq.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/reset.h>
#include <linux/ktime.h>

#include <linux/mfd/mt6323/registers.h>

#include <linux/mmc/card.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>

#include "cqhci.h"
#include "mmc_hsq.h"
#include "../core/core.h"

#define MAX_BD_NUM          1024
#define MSDC_NR_CLOCKS      3

/*
 * Z1 read-only MC0 pad snapshot registers.  These are whole-register reads:
 * GPIO44/45/46 use MODE 0x350, GPIO54 uses MODE 0x360; GPIO bank 1 contains
 * DIR/DO/DI.  IOCFG-B contains the shared MC0 IES/SMT/drive and pull fields.
 */
#define MT6572_GPIO_BASE		0x10005000
#define MT6572_GPIO_DIR1		(MT6572_GPIO_BASE + 0x0010)
#define MT6572_GPIO_DO1		(MT6572_GPIO_BASE + 0x0110)
#define MT6572_GPIO_DI1		(MT6572_GPIO_BASE + 0x0210)
#define MT6572_GPIO_MODE44_46		(MT6572_GPIO_BASE + 0x0350)
#define MT6572_GPIO_MODE54		(MT6572_GPIO_BASE + 0x0360)
#define MT6572_IOCFG_B_BASE		0x10209000
#define MT6572_IOCFG_B_MMC0_IES	(MT6572_IOCFG_B_BASE + 0x0000)
#define MT6572_IOCFG_B_MMC0_SMT	(MT6572_IOCFG_B_BASE + 0x0020)
#define MT6572_IOCFG_B_MMC0_R0R1	(MT6572_IOCFG_B_BASE + 0x0040)
#define MT6572_IOCFG_B_MMC0_PUPD	(MT6572_IOCFG_B_BASE + 0x0050)
#define MT6572_IOCFG_B_MMC0_DRV	(MT6572_IOCFG_B_BASE + 0x0060)

/*--------------------------------------------------------------------------*/
/* Common Definition                                                        */
/*--------------------------------------------------------------------------*/
#define MSDC_BUS_1BITS          0x0
#define MSDC_BUS_4BITS          0x1
#define MSDC_BUS_8BITS          0x2

#define MSDC_BURST_64B          0x6

/*--------------------------------------------------------------------------*/
/* Register Offset                                                          */
/*--------------------------------------------------------------------------*/
#define MSDC_CFG         0x0
#define MSDC_IOCON       0x04
#define MSDC_PS          0x08
#define MSDC_INT         0x0c
#define MSDC_INTEN       0x10
#define MSDC_FIFOCS      0x14
#define SDC_CFG          0x30
#define SDC_CMD          0x34
#define SDC_ARG          0x38
#define SDC_STS          0x3c
#define SDC_RESP0        0x40
#define SDC_RESP1        0x44
#define SDC_RESP2        0x48
#define SDC_RESP3        0x4c
#define SDC_BLK_NUM      0x50
#define SDC_ADV_CFG0     0x64
#define MSDC_NEW_RX_CFG  0x68
#define EMMC_IOCON       0x7c
#define SDC_ACMD_RESP    0x80
#define DMA_SA_H4BIT     0x8c
#define MSDC_DMA_SA      0x90
#define MSDC_DMA_CTRL    0x98
#define MSDC_DMA_CFG     0x9c
#define MSDC_PATCH_BIT   0xb0
#define MSDC_PATCH_BIT1  0xb4
#define MSDC_PATCH_BIT2  0xb8
#define MSDC_PAD_TUNE    0xec
#define MSDC_PAD_TUNE0   0xf0
#define PAD_DS_TUNE      0x188
#define PAD_CMD_TUNE     0x18c
#define EMMC51_CFG0	 0x204
#define EMMC50_CFG0      0x208
#define EMMC50_CFG1      0x20c
#define EMMC50_CFG2      0x21c
#define EMMC50_CFG3      0x220
#define SDC_FIFO_CFG     0x228
#define CQHCI_SETTING	 0x7fc

/*--------------------------------------------------------------------------*/
/* Top Pad Register Offset                                                  */
/*--------------------------------------------------------------------------*/
#define EMMC_TOP_CONTROL	0x00
#define EMMC_TOP_CMD		0x04
#define EMMC50_PAD_DS_TUNE	0x0c
#define LOOP_TEST_CONTROL	0x30

/*--------------------------------------------------------------------------*/
/* Register Mask                                                            */
/*--------------------------------------------------------------------------*/

/* MSDC_CFG mask */
#define MSDC_CFG_MODE           BIT(0)	/* RW */
#define MSDC_CFG_CKPDN          BIT(1)	/* RW */
#define MSDC_CFG_RST            BIT(2)	/* RW */
#define MSDC_CFG_PIO            BIT(3)	/* RW */
#define MSDC_CFG_CKDRVEN        BIT(4)	/* RW */
#define MSDC_CFG_BV18SDT        BIT(5)	/* RW */
#define MSDC_CFG_BV18PSS        BIT(6)	/* R  */
#define MSDC_CFG_CKSTB          BIT(7)	/* R  */
#define MSDC_CFG_CKDIV          GENMASK(15, 8)	/* RW */
#define MSDC_CFG_CKMOD          GENMASK(17, 16)	/* RW */
#define MSDC_CFG_HS400_CK_MODE  BIT(18)	/* RW */
#define MSDC_CFG_HS400_CK_MODE_EXTRA  BIT(22)	/* RW */
#define MSDC_CFG_CKDIV_EXTRA    GENMASK(19, 8)	/* RW */
#define MSDC_CFG_CKMOD_EXTRA    GENMASK(21, 20)	/* RW */

/* MSDC_IOCON mask */
#define MSDC_IOCON_SDR104CKS    BIT(0)	/* RW */
#define MSDC_IOCON_RSPL         BIT(1)	/* RW */
#define MSDC_IOCON_DSPL         BIT(2)	/* RW */
#define MSDC_IOCON_DDLSEL       BIT(3)	/* RW */
#define MSDC_IOCON_DDR50CKD     BIT(4)	/* RW */
#define MSDC_IOCON_DSPLSEL      BIT(5)	/* RW */
#define MSDC_IOCON_W_DSPL       BIT(8)	/* RW */
#define MSDC_IOCON_D0SPL        BIT(16)	/* RW */
#define MSDC_IOCON_D1SPL        BIT(17)	/* RW */
#define MSDC_IOCON_D2SPL        BIT(18)	/* RW */
#define MSDC_IOCON_D3SPL        BIT(19)	/* RW */
#define MSDC_IOCON_D4SPL        BIT(20)	/* RW */
#define MSDC_IOCON_D5SPL        BIT(21)	/* RW */
#define MSDC_IOCON_D6SPL        BIT(22)	/* RW */
#define MSDC_IOCON_D7SPL        BIT(23)	/* RW */
#define MSDC_IOCON_RISCSZ       GENMASK(25, 24)	/* RW */

/* MSDC_PS mask */
#define MSDC_PS_CDEN            BIT(0)	/* RW */
#define MSDC_PS_CDSTS           BIT(1)	/* R  */
#define MSDC_PS_CDDEBOUNCE      GENMASK(15, 12)	/* RW */
#define MSDC_PS_DAT             GENMASK(23, 16)	/* R  */
#define MSDC_PS_DATA1           BIT(17)	/* R  */
#define MSDC_PS_CMD             BIT(24)	/* R  */
#define MSDC_PS_WP              BIT(31)	/* R  */

/* MSDC_INT mask */
#define MSDC_INT_MMCIRQ         BIT(0)	/* W1C */
#define MSDC_INT_CDSC           BIT(1)	/* W1C */
#define MSDC_INT_ACMDRDY        BIT(3)	/* W1C */
#define MSDC_INT_ACMDTMO        BIT(4)	/* W1C */
#define MSDC_INT_ACMDCRCERR     BIT(5)	/* W1C */
#define MSDC_INT_DMAQ_EMPTY     BIT(6)	/* W1C */
#define MSDC_INT_SDIOIRQ        BIT(7)	/* W1C */
#define MSDC_INT_CMDRDY         BIT(8)	/* W1C */
#define MSDC_INT_CMDTMO         BIT(9)	/* W1C */
#define MSDC_INT_RSPCRCERR      BIT(10)	/* W1C */
#define MSDC_INT_CSTA           BIT(11)	/* R */
#define MSDC_INT_XFER_COMPL     BIT(12)	/* W1C */
#define MSDC_INT_DXFER_DONE     BIT(13)	/* W1C */
#define MSDC_INT_DATTMO         BIT(14)	/* W1C */
#define MSDC_INT_DATCRCERR      BIT(15)	/* W1C */
#define MSDC_INT_ACMD19_DONE    BIT(16)	/* W1C */
#define MSDC_INT_DMA_BDCSERR    BIT(17)	/* W1C */
#define MSDC_INT_DMA_GPDCSERR   BIT(18)	/* W1C */
#define MSDC_INT_DMA_PROTECT    BIT(19)	/* W1C */
#define MSDC_INT_CMDQ           BIT(28)	/* W1C */

/* MSDC_INTEN mask */
#define MSDC_INTEN_MMCIRQ       BIT(0)	/* RW */
#define MSDC_INTEN_CDSC         BIT(1)	/* RW */
#define MSDC_INTEN_ACMDRDY      BIT(3)	/* RW */
#define MSDC_INTEN_ACMDTMO      BIT(4)	/* RW */
#define MSDC_INTEN_ACMDCRCERR   BIT(5)	/* RW */
#define MSDC_INTEN_DMAQ_EMPTY   BIT(6)	/* RW */
#define MSDC_INTEN_SDIOIRQ      BIT(7)	/* RW */
#define MSDC_INTEN_CMDRDY       BIT(8)	/* RW */
#define MSDC_INTEN_CMDTMO       BIT(9)	/* RW */
#define MSDC_INTEN_RSPCRCERR    BIT(10)	/* RW */
#define MSDC_INTEN_CSTA         BIT(11)	/* RW */
#define MSDC_INTEN_XFER_COMPL   BIT(12)	/* RW */
#define MSDC_INTEN_DXFER_DONE   BIT(13)	/* RW */
#define MSDC_INTEN_DATTMO       BIT(14)	/* RW */
#define MSDC_INTEN_DATCRCERR    BIT(15)	/* RW */
#define MSDC_INTEN_ACMD19_DONE  BIT(16)	/* RW */
#define MSDC_INTEN_DMA_BDCSERR  BIT(17)	/* RW */
#define MSDC_INTEN_DMA_GPDCSERR BIT(18)	/* RW */
#define MSDC_INTEN_DMA_PROTECT  BIT(19)	/* RW */

/* MSDC_FIFOCS mask */
#define MSDC_FIFOCS_RXCNT       GENMASK(7, 0)	/* R */
#define MSDC_FIFOCS_TXCNT       GENMASK(23, 16)	/* R */
#define MSDC_FIFOCS_CLR         BIT(31)	/* RW */

/* SDC_CFG mask */
#define SDC_CFG_SDIOINTWKUP     BIT(0)	/* RW */
#define SDC_CFG_INSWKUP         BIT(1)	/* RW */
#define SDC_CFG_WRDTOC          GENMASK(14, 2)  /* RW */
#define SDC_CFG_BUSWIDTH        GENMASK(17, 16)	/* RW */
#define SDC_CFG_SDIO            BIT(19)	/* RW */
#define SDC_CFG_SDIOIDE         BIT(20)	/* RW */
#define SDC_CFG_INTATGAP        BIT(21)	/* RW */
#define SDC_CFG_DTOC            GENMASK(31, 24)	/* RW */

/* SDC_STS mask */
#define SDC_STS_SDCBUSY         BIT(0)	/* RW */
#define SDC_STS_CMDBUSY         BIT(1)	/* RW */
#define SDC_STS_SWR_COMPL       BIT(31)	/* RW */

/* SDC_ADV_CFG0 mask */
#define SDC_DAT1_IRQ_TRIGGER	BIT(19)	/* RW */
#define SDC_RX_ENHANCE_EN	BIT(20)	/* RW */
#define SDC_NEW_TX_EN		BIT(31)	/* RW */

/* MSDC_NEW_RX_CFG mask */
#define MSDC_NEW_RX_PATH_SEL	BIT(0)	/* RW */

/* DMA_SA_H4BIT mask */
#define DMA_ADDR_HIGH_4BIT      GENMASK(3, 0)	/* RW */

/* MSDC_DMA_CTRL mask */
#define MSDC_DMA_CTRL_START     BIT(0)	/* W */
#define MSDC_DMA_CTRL_STOP      BIT(1)	/* W */
#define MSDC_DMA_CTRL_RESUME    BIT(2)	/* W */
#define MSDC_DMA_CTRL_MODE      BIT(8)	/* RW */
#define MSDC_DMA_CTRL_LASTBUF   BIT(10)	/* RW */
#define MSDC_DMA_CTRL_BRUSTSZ   GENMASK(14, 12)	/* RW */

/* MSDC_DMA_CFG mask */
#define MSDC_DMA_CFG_STS        BIT(0)	/* R */
#define MSDC_DMA_CFG_DECSEN     BIT(1)	/* RW */
#define MSDC_DMA_CFG_AHBHPROT2  BIT(9)	/* RW */
#define MSDC_DMA_CFG_ACTIVEEN   BIT(13)	/* RW */
#define MSDC_DMA_CFG_CS12B16B   BIT(16)	/* RW */

/* MSDC_PATCH_BIT mask */
#define MSDC_PATCH_BIT_ODDSUPP    BIT(1)	/* RW */
#define MSDC_PATCH_BIT_DIS_WRMON  BIT(2)	/* RW */
#define MSDC_PATCH_BIT_RD_DAT_SEL BIT(3)	/* RW */
#define MSDC_PATCH_BIT_DESCUP_SEL BIT(6)	/* RW */
#define MSDC_INT_DAT_LATCH_CK_SEL GENMASK(9, 7)
#define MSDC_CKGEN_MSDC_DLY_SEL   GENMASK(14, 10)
#define MSDC_PATCH_BIT_IODSSEL    BIT(16)	/* RW */
#define MSDC_PATCH_BIT_IOINTSEL   BIT(17)	/* RW */
#define MSDC_PATCH_BIT_BUSYDLY    GENMASK(21, 18)	/* RW */
#define MSDC_PATCH_BIT_WDOD       GENMASK(25, 22)	/* RW */
#define MSDC_PATCH_BIT_IDRTSEL    BIT(26)	/* RW */
#define MSDC_PATCH_BIT_CMDFSEL    BIT(27)	/* RW */
#define MSDC_PATCH_BIT_INTDLSEL   BIT(28)	/* RW */
#define MSDC_PATCH_BIT_SPCPUSH    BIT(29)	/* RW */
#define MSDC_PATCH_BIT_DECRCTMO   BIT(30)	/* RW */

/* MSDC_PATCH_BIT1 mask */
#define MSDC_PB1_WRDAT_CRC_TACNTR GENMASK(2, 0)     /* RW */
#define MSDC_PATCH_BIT1_CMDTA     GENMASK(5, 3)     /* RW */
#define MSDC_PB1_BUSY_CHECK_SEL   BIT(7)    /* RW */
#define MSDC_PATCH_BIT1_STOP_DLY  GENMASK(11, 8)    /* RW */
#define MSDC_PB1_DDR_CMD_FIX_SEL  BIT(14)           /* RW */
#define MSDC_PB1_SINGLE_BURST     BIT(16)           /* RW */
#define MSDC_PB1_RSVD20           GENMASK(18, 17)   /* RW */
#define MSDC_PB1_AUTO_SYNCST_CLR  BIT(19)           /* RW */
#define MSDC_PB1_MARK_POP_WATER   BIT(20)           /* RW */
#define MSDC_PB1_LP_DCM_EN        BIT(21)           /* RW */
#define MSDC_PB1_RSVD3            BIT(22)           /* RW */
#define MSDC_PB1_AHB_GDMA_HCLK    BIT(23)           /* RW */
#define MSDC_PB1_MSDC_CLK_ENFEAT  GENMASK(31, 24)   /* RW */

/* MSDC_PATCH_BIT2 mask */
#define MSDC_PATCH_BIT2_CFGRESP   BIT(15)   /* RW */
#define MSDC_PATCH_BIT2_CFGCRCSTS BIT(28)   /* RW */
#define MSDC_PB2_SUPPORT_64G      BIT(1)    /* RW */
#define MSDC_PB2_RESPWAIT         GENMASK(3, 2)   /* RW */
#define MSDC_PB2_RESPSTSENSEL     GENMASK(18, 16) /* RW */
#define MSDC_PB2_POP_EN_CNT       GENMASK(23, 20) /* RW */
#define MSDC_PB2_CFGCRCSTSEDGE    BIT(25)   /* RW */
#define MSDC_PB2_CRCSTSENSEL      GENMASK(31, 29) /* RW */

#define MSDC_PAD_TUNE_DATWRDLY	  GENMASK(4, 0)		/* RW */
#define MSDC_PAD_TUNE_DATRRDLY	  GENMASK(12, 8)	/* RW */
#define MSDC_PAD_TUNE_DATRRDLY2	  GENMASK(12, 8)	/* RW */
#define MSDC_PAD_TUNE_CMDRDLY	  GENMASK(20, 16)	/* RW */
#define MSDC_PAD_TUNE_CMDRDLY2	  GENMASK(20, 16)	/* RW */
#define MSDC_PAD_TUNE_CMDRRDLY	  GENMASK(26, 22)	/* RW */
#define MSDC_PAD_TUNE_CLKTDLY	  GENMASK(31, 27)	/* RW */
#define MSDC_PAD_TUNE_RXDLYSEL	  BIT(15)   /* RW */
#define MSDC_PAD_TUNE_RD_SEL	  BIT(13)   /* RW */
#define MSDC_PAD_TUNE_CMD_SEL	  BIT(21)   /* RW */
#define MSDC_PAD_TUNE_RD2_SEL	  BIT(13)   /* RW */
#define MSDC_PAD_TUNE_CMD2_SEL	  BIT(21)   /* RW */

#define PAD_DS_TUNE_DLY_SEL       BIT(0)	  /* RW */
#define PAD_DS_TUNE_DLY2_SEL      BIT(1)	  /* RW */
#define PAD_DS_TUNE_DLY1	  GENMASK(6, 2)   /* RW */
#define PAD_DS_TUNE_DLY2	  GENMASK(11, 7)  /* RW */
#define PAD_DS_TUNE_DLY3	  GENMASK(16, 12) /* RW */

#define PAD_CMD_TUNE_RX_DLY3	  GENMASK(5, 1)   /* RW */

/* EMMC51_CFG0 mask */
#define CMDQ_RDAT_CNT		  GENMASK(21, 12) /* RW */

#define EMMC50_CFG_PADCMD_LATCHCK BIT(0)   /* RW */
#define EMMC50_CFG_CRCSTS_EDGE    BIT(3)   /* RW */
#define EMMC50_CFG_CFCSTS_SEL     BIT(4)   /* RW */
#define EMMC50_CFG_CMD_RESP_SEL   BIT(9)   /* RW */

/* EMMC50_CFG1 mask */
#define EMMC50_CFG1_DS_CFG        BIT(28)  /* RW */

/* EMMC50_CFG2 mask */
#define EMMC50_CFG2_AXI_SET_LEN   GENMASK(27, 24) /* RW */

#define EMMC50_CFG3_OUTS_WR       GENMASK(4, 0)   /* RW */

#define SDC_FIFO_CFG_WRVALIDSEL   BIT(24)  /* RW */
#define SDC_FIFO_CFG_RDVALIDSEL   BIT(25)  /* RW */

/* CQHCI_SETTING */
#define CQHCI_RD_CMD_WND_SEL	  BIT(14) /* RW */
#define CQHCI_WR_CMD_WND_SEL	  BIT(15) /* RW */

/* EMMC_TOP_CONTROL mask */
#define PAD_RXDLY_SEL           BIT(0)      /* RW */
#define DELAY_EN                BIT(1)      /* RW */
#define PAD_DAT_RD_RXDLY2       GENMASK(6, 2)     /* RW */
#define PAD_DAT_RD_RXDLY        GENMASK(11, 7)    /* RW */
#define PAD_DAT_RD_RXDLY2_SEL   BIT(12)     /* RW */
#define PAD_DAT_RD_RXDLY_SEL    BIT(13)     /* RW */
#define DATA_K_VALUE_SEL        BIT(14)     /* RW */
#define SDC_RX_ENH_EN           BIT(15)     /* TW */

/* EMMC_TOP_CMD mask */
#define PAD_CMD_RXDLY2          GENMASK(4, 0)	/* RW */
#define PAD_CMD_RXDLY           GENMASK(9, 5)	/* RW */
#define PAD_CMD_RD_RXDLY2_SEL   BIT(10)		/* RW */
#define PAD_CMD_RD_RXDLY_SEL    BIT(11)		/* RW */
#define PAD_CMD_TX_DLY          GENMASK(16, 12)	/* RW */

/* EMMC50_PAD_DS_TUNE mask */
#define PAD_DS_DLY_SEL		BIT(16)	/* RW */
#define PAD_DS_DLY2_SEL		BIT(15)	/* RW */
#define PAD_DS_DLY1		GENMASK(14, 10)	/* RW */
#define PAD_DS_DLY3		GENMASK(4, 0)	/* RW */

/* LOOP_TEST_CONTROL mask */
#define TEST_LOOP_DSCLK_MUX_SEL        BIT(0)	/* RW */
#define TEST_LOOP_LATCH_MUX_SEL        BIT(1)	/* RW */
#define LOOP_EN_SEL_CLK                BIT(20)	/* RW */
#define TEST_HS400_CMD_LOOP_MUX_SEL    BIT(31)	/* RW */

#define REQ_CMD_EIO  BIT(0)
#define REQ_CMD_TMO  BIT(1)
#define REQ_DAT_ERR  BIT(2)
#define REQ_STOP_EIO BIT(3)
#define REQ_STOP_TMO BIT(4)
#define REQ_CMD_BUSY BIT(5)

#define MSDC_PREPARE_FLAG BIT(0)
#define MSDC_ASYNC_FLAG BIT(1)
#define MSDC_MMAP_FLAG BIT(2)

#define MTK_MMC_AUTOSUSPEND_DELAY	50
#define CMD_TIMEOUT         (HZ/10 * 5)	/* 100ms x5 */
#define DAT_TIMEOUT         (HZ    * 5)	/* 1000ms x5 */

#define DEFAULT_DEBOUNCE	(8)	/* 8 cycles CD debounce */

extern int mtk_pmic_wrap_z1_read(u32 reg, u32 *value) __weak;
extern int mtk_pmic_wrap_z1_probe_status(void) __weak;
extern bool mtk_pmic_wrap_z1_write_deny_active(void) __weak;

#define Z1_MT6323_VEMC_EN		BIT(14)
#define Z1_MT6323_VEMC_QI		BIT(15)
#define Z1_MT6323_VIO18_EN		BIT(14)
#define Z1_MT6323_VIO18_QI		BIT(15)
#define Z1_DIAG_MAX_CLOCK_HZ		400000

#define TUNING_REG2_FIXED_OFFEST	4
#define PAD_DELAY_HALF	32 /* PAD delay cells */
#define PAD_DELAY_FULL	64
/*--------------------------------------------------------------------------*/
/* Descriptor Structure                                                     */
/*--------------------------------------------------------------------------*/
struct mt_gpdma_desc {
	u32 gpd_info;
#define GPDMA_DESC_HWO		BIT(0)
#define GPDMA_DESC_BDP		BIT(1)
#define GPDMA_DESC_CHECKSUM	GENMASK(15, 8)
#define GPDMA_DESC_INT		BIT(16)
#define GPDMA_DESC_NEXT_H4	GENMASK(27, 24)
#define GPDMA_DESC_PTR_H4	GENMASK(31, 28)
	u32 next;
	u32 ptr;
	u32 gpd_data_len;
#define GPDMA_DESC_BUFLEN	GENMASK(15, 0)
#define GPDMA_DESC_EXTLEN	GENMASK(23, 16)
	u32 arg;
	u32 blknum;
	u32 cmd;
};

struct mt_bdma_desc {
	u32 bd_info;
#define BDMA_DESC_EOL		BIT(0)
#define BDMA_DESC_CHECKSUM	GENMASK(15, 8)
#define BDMA_DESC_BLKPAD	BIT(17)
#define BDMA_DESC_DWPAD		BIT(18)
#define BDMA_DESC_NEXT_H4	GENMASK(27, 24)
#define BDMA_DESC_PTR_H4	GENMASK(31, 28)
	u32 next;
	u32 ptr;
	u32 bd_data_len;
#define BDMA_DESC_BUFLEN	GENMASK(15, 0)
#define BDMA_DESC_BUFLEN_EXT	GENMASK(23, 0)
};

struct msdc_dma {
	struct scatterlist *sg;	/* I/O scatter list */
	struct mt_gpdma_desc *gpd;		/* pointer to gpd array */
	struct mt_bdma_desc *bd;		/* pointer to bd array */
	dma_addr_t gpd_addr;	/* the physical address of gpd array */
	dma_addr_t bd_addr;	/* the physical address of bd array */
};

struct msdc_save_para {
	u32 msdc_cfg;
	u32 iocon;
	u32 sdc_cfg;
	u32 pad_tune;
	u32 patch_bit0;
	u32 patch_bit1;
	u32 patch_bit2;
	u32 pad_ds_tune;
	u32 pad_cmd_tune;
	u32 emmc50_cfg0;
	u32 emmc50_cfg3;
	u32 sdc_fifo_cfg;
	u32 emmc_top_control;
	u32 emmc_top_cmd;
	u32 emmc50_pad_ds_tune;
	u32 loop_test_control;
};

struct mtk_mmc_compatible {
	u8 clk_div_bits;
	bool recheck_sdio_irq;
	bool hs400_tune; /* only used for MT8173 */
	bool needs_top_base;
	u32 pad_tune_reg;
	bool async_fifo;
	bool data_tune;
	bool busy_check;
	bool stop_clk_fix;
	u8 stop_dly_sel;
	u8 pop_en_cnt;
	bool enhance_rx;
	bool support_64g;
	bool use_internal_cd;
	bool support_new_tx;
	bool support_new_rx;
};

struct msdc_tune_para {
	u32 iocon;
	u32 pad_tune;
	u32 pad_cmd_tune;
	u32 emmc_top_control;
	u32 emmc_top_cmd;
};

struct msdc_delay_phase {
	u8 maxlen;
	u8 start;
	u8 final_phase;
};

struct msdc_host {
	struct device *dev;
	const struct mtk_mmc_compatible *dev_comp;
	int cmd_rsp;

	spinlock_t lock;
	struct mmc_request *mrq;
	struct mmc_command *cmd;
	struct mmc_data *data;
	int error;

	void __iomem *base;		/* host base address */
	void __iomem *top_base;		/* host top register base address */

	struct msdc_dma dma;	/* dma channel */
	u64 dma_mask;

	u32 timeout_ns;		/* data timeout ns */
	u32 timeout_clks;	/* data timeout clks */

	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_uhs;
	struct pinctrl_state *pins_eint;
	struct delayed_work req_timeout;
	int irq;		/* host interrupt */
	int eint_irq;		/* interrupt from sdio device for waking up system */
	struct reset_control *reset;

	struct clk *src_clk;	/* msdc source clock */
	struct clk *h_clk;      /* msdc h_clk */
	struct clk *bus_clk;	/* bus clock which used to access register */
	struct clk *src_clk_cg; /* msdc source clock control gate */
	struct clk *sys_clk_cg;	/* msdc subsys clock control gate */
	struct clk *crypto_clk; /* msdc crypto clock control gate */
	struct clk_bulk_data bulk_clks[MSDC_NR_CLOCKS];
	u32 mclk;		/* mmc subsystem clock frequency */
	u32 src_clk_freq;	/* source clock frequency */
	unsigned char timing;
	bool vqmmc_enabled;
	u32 latch_ck;
	u32 hs400_ds_delay;
	u32 hs400_ds_dly3;
	u32 hs200_cmd_int_delay; /* cmd internal delay for HS200/SDR104 */
	u32 hs400_cmd_int_delay; /* cmd internal delay for HS400 */
	u32 tuning_step;
	bool hs400_cmd_resp_sel_rising;
				 /* cmd response sample selection for HS400 */
	bool hs400_mode;	/* current eMMC will run at hs400 mode */
	bool hs400_tuning;	/* hs400 mode online tuning */
	bool internal_cd;	/* Use internal card-detect logic */
	bool cqhci;		/* support eMMC hw cmdq */
	bool hsq_en;		/* Host Software Queue is enabled */
	bool z1_direct_cid;
	bool z1_frozen;
	u64 z1_cmd1_start_ns;
	struct msdc_save_para save_para; /* used when gate HCLK */
	struct msdc_tune_para def_tune_para; /* default tune setting */
	struct msdc_tune_para saved_tune_para; /* tune result of CMD21/CMD19 */
	struct cqhci_host *cq_host;
	u32 cq_ssc1_time;
};

static const struct mtk_mmc_compatible mt6572_compat = {
	.clk_div_bits = 8,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE,
	.async_fifo = false,
	.data_tune = false,
	.busy_check = false,
	.stop_clk_fix = false,
	.enhance_rx = false,
	.support_64g = false,
};

static const struct mtk_mmc_compatible mt2701_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = false,
	.stop_clk_fix = false,
	.enhance_rx = false,
	.support_64g = false,
};

static const struct mtk_mmc_compatible mt2712_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = false,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.stop_dly_sel = 3,
	.enhance_rx = true,
	.support_64g = true,
};

static const struct mtk_mmc_compatible mt6779_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = false,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.stop_dly_sel = 3,
	.enhance_rx = true,
	.support_64g = true,
};

static const struct mtk_mmc_compatible mt6795_compat = {
	.clk_div_bits = 8,
	.recheck_sdio_irq = false,
	.hs400_tune = true,
	.pad_tune_reg = MSDC_PAD_TUNE,
	.async_fifo = false,
	.data_tune = false,
	.busy_check = false,
	.stop_clk_fix = false,
	.enhance_rx = false,
	.support_64g = false,
};

static const struct mtk_mmc_compatible mt7620_compat = {
	.clk_div_bits = 8,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE,
	.async_fifo = false,
	.data_tune = false,
	.busy_check = false,
	.stop_clk_fix = false,
	.enhance_rx = false,
	.use_internal_cd = true,
};

static const struct mtk_mmc_compatible mt7622_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.stop_dly_sel = 3,
	.enhance_rx = true,
	.support_64g = false,
};

static const struct mtk_mmc_compatible mt7986_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.needs_top_base = true,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.stop_dly_sel = 3,
	.enhance_rx = true,
	.support_64g = true,
};

static const struct mtk_mmc_compatible mt8135_compat = {
	.clk_div_bits = 8,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE,
	.async_fifo = false,
	.data_tune = false,
	.busy_check = false,
	.stop_clk_fix = false,
	.enhance_rx = false,
	.support_64g = false,
};

static const struct mtk_mmc_compatible mt8173_compat = {
	.clk_div_bits = 8,
	.recheck_sdio_irq = true,
	.hs400_tune = true,
	.pad_tune_reg = MSDC_PAD_TUNE,
	.async_fifo = false,
	.data_tune = false,
	.busy_check = false,
	.stop_clk_fix = false,
	.enhance_rx = false,
	.support_64g = false,
};

static const struct mtk_mmc_compatible mt8183_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = false,
	.hs400_tune = false,
	.needs_top_base = true,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.stop_dly_sel = 3,
	.enhance_rx = true,
	.support_64g = true,
};

static const struct mtk_mmc_compatible mt8516_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.stop_dly_sel = 3,
};

static const struct mtk_mmc_compatible mt8196_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = false,
	.hs400_tune = false,
	.needs_top_base = true,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.stop_dly_sel = 1,
	.pop_en_cnt = 2,
	.enhance_rx = true,
	.support_64g = true,
	.support_new_tx = true,
	.support_new_rx = true,
};

static const struct of_device_id msdc_of_ids[] = {
	{ .compatible = "mediatek,mt6572-mmc", .data = &mt6572_compat},
	{ .compatible = "mediatek,mt2701-mmc", .data = &mt2701_compat},
	{ .compatible = "mediatek,mt2712-mmc", .data = &mt2712_compat},
	{ .compatible = "mediatek,mt6779-mmc", .data = &mt6779_compat},
	{ .compatible = "mediatek,mt6795-mmc", .data = &mt6795_compat},
	{ .compatible = "mediatek,mt7620-mmc", .data = &mt7620_compat},
	{ .compatible = "mediatek,mt7622-mmc", .data = &mt7622_compat},
	{ .compatible = "mediatek,mt7986-mmc", .data = &mt7986_compat},
	{ .compatible = "mediatek,mt7988-mmc", .data = &mt7986_compat},
	{ .compatible = "mediatek,mt8135-mmc", .data = &mt8135_compat},
	{ .compatible = "mediatek,mt8173-mmc", .data = &mt8173_compat},
	{ .compatible = "mediatek,mt8183-mmc", .data = &mt8183_compat},
	{ .compatible = "mediatek,mt8196-mmc", .data = &mt8196_compat},
	{ .compatible = "mediatek,mt8516-mmc", .data = &mt8516_compat},

	{}
};
MODULE_DEVICE_TABLE(of, msdc_of_ids);

static void sdr_set_bits(void __iomem *reg, u32 bs)
{
	u32 val = readl(reg);

	val |= bs;
	writel(val, reg);
}

static void sdr_clr_bits(void __iomem *reg, u32 bs)
{
	u32 val = readl(reg);

	val &= ~bs;
	writel(val, reg);
}

static void sdr_set_field(void __iomem *reg, u32 field, u32 val)
{
	unsigned int tv = readl(reg);

	tv &= ~field;
	tv |= ((val) << (ffs((unsigned int)field) - 1));
	writel(tv, reg);
}

static void sdr_get_field(void __iomem *reg, u32 field, u32 *val)
{
	unsigned int tv = readl(reg);

	*val = ((tv & field) >> (ffs((unsigned int)field) - 1));
}

static void msdc_reset_hw(struct msdc_host *host)
{
	u32 val;

	sdr_set_bits(host->base + MSDC_CFG, MSDC_CFG_RST);
	readl_poll_timeout_atomic(host->base + MSDC_CFG, val, !(val & MSDC_CFG_RST), 0, 0);

	sdr_set_bits(host->base + MSDC_FIFOCS, MSDC_FIFOCS_CLR);
	readl_poll_timeout_atomic(host->base + MSDC_FIFOCS, val,
				  !(val & MSDC_FIFOCS_CLR), 0, 0);

	val = readl(host->base + MSDC_INT);
	writel(val, host->base + MSDC_INT);
}

static void msdc_recover_hw(struct msdc_host *host)
{
	/*
	 * Resetting MSDC_CFG/FIFO is a host-local operation; it does not touch
	 * eMMC media or its power rails.  LK leaves this controller configured,
	 * while MT6572 vendor init resets it before CMD1.  Stage A needs the same
	 * clean command/FIFO state, while retaining its no-mmcblk/no-write policy.
	 */
	msdc_reset_hw(host);
}

static void msdc_cmd_next(struct msdc_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd);
static void __msdc_enable_sdio_irq(struct msdc_host *host, int enb);

static const u32 cmd_ints_mask = MSDC_INTEN_CMDRDY | MSDC_INTEN_RSPCRCERR |
			MSDC_INTEN_CMDTMO | MSDC_INTEN_ACMDRDY |
			MSDC_INTEN_ACMDCRCERR | MSDC_INTEN_ACMDTMO;
static const u32 data_ints_mask = MSDC_INTEN_XFER_COMPL | MSDC_INTEN_DATTMO |
			MSDC_INTEN_DATCRCERR | MSDC_INTEN_DMA_BDCSERR |
			MSDC_INTEN_DMA_GPDCSERR | MSDC_INTEN_DMA_PROTECT;

static u8 msdc_dma_calcs(u8 *buf, u32 len)
{
	u32 i, sum = 0;

	for (i = 0; i < len; i++)
		sum += buf[i];
	return 0xff - (u8) sum;
}

static inline void msdc_dma_setup(struct msdc_host *host, struct msdc_dma *dma,
		struct mmc_data *data)
{
	unsigned int j, dma_len;
	dma_addr_t dma_address;
	u32 dma_ctrl;
	struct scatterlist *sg;
	struct mt_gpdma_desc *gpd;
	struct mt_bdma_desc *bd;

	sg = data->sg;

	gpd = dma->gpd;
	bd = dma->bd;

	/* modify gpd */
	gpd->gpd_info |= GPDMA_DESC_HWO;
	gpd->gpd_info |= GPDMA_DESC_BDP;
	/* need to clear first. use these bits to calc checksum */
	gpd->gpd_info &= ~GPDMA_DESC_CHECKSUM;
	gpd->gpd_info |= msdc_dma_calcs((u8 *) gpd, 16) << 8;

	/* modify bd */
	for_each_sg(data->sg, sg, data->sg_count, j) {
		dma_address = sg_dma_address(sg);
		dma_len = sg_dma_len(sg);

		/* init bd */
		bd[j].bd_info &= ~BDMA_DESC_BLKPAD;
		bd[j].bd_info &= ~BDMA_DESC_DWPAD;
		bd[j].ptr = lower_32_bits(dma_address);
		if (host->dev_comp->support_64g) {
			bd[j].bd_info &= ~BDMA_DESC_PTR_H4;
			bd[j].bd_info |= (upper_32_bits(dma_address) & 0xf)
					 << 28;
		}

		if (host->dev_comp->support_64g) {
			bd[j].bd_data_len &= ~BDMA_DESC_BUFLEN_EXT;
			bd[j].bd_data_len |= (dma_len & BDMA_DESC_BUFLEN_EXT);
		} else {
			bd[j].bd_data_len &= ~BDMA_DESC_BUFLEN;
			bd[j].bd_data_len |= (dma_len & BDMA_DESC_BUFLEN);
		}

		if (j == data->sg_count - 1) /* the last bd */
			bd[j].bd_info |= BDMA_DESC_EOL;
		else
			bd[j].bd_info &= ~BDMA_DESC_EOL;

		/* checksum need to clear first */
		bd[j].bd_info &= ~BDMA_DESC_CHECKSUM;
		bd[j].bd_info |= msdc_dma_calcs((u8 *)(&bd[j]), 16) << 8;
	}

	sdr_set_field(host->base + MSDC_DMA_CFG, MSDC_DMA_CFG_DECSEN, 1);
	dma_ctrl = readl_relaxed(host->base + MSDC_DMA_CTRL);
	dma_ctrl &= ~(MSDC_DMA_CTRL_BRUSTSZ | MSDC_DMA_CTRL_MODE);
	dma_ctrl |= (MSDC_BURST_64B << 12 | BIT(8));
	writel_relaxed(dma_ctrl, host->base + MSDC_DMA_CTRL);
	if (host->dev_comp->support_64g)
		sdr_set_field(host->base + DMA_SA_H4BIT, DMA_ADDR_HIGH_4BIT,
			      upper_32_bits(dma->gpd_addr) & 0xf);
	writel(lower_32_bits(dma->gpd_addr), host->base + MSDC_DMA_SA);
}

static void msdc_prepare_data(struct msdc_host *host, struct mmc_data *data)
{
	if (!(data->host_cookie & MSDC_PREPARE_FLAG)) {
		data->sg_count = dma_map_sg(host->dev, data->sg, data->sg_len,
					    mmc_get_dma_dir(data));
		if (data->sg_count)
			data->host_cookie |= MSDC_PREPARE_FLAG;
	}
}

static bool msdc_data_prepared(struct mmc_data *data)
{
	return data->host_cookie & MSDC_PREPARE_FLAG;
}

static void msdc_unprepare_data(struct msdc_host *host, struct mmc_data *data)
{
	if (data->host_cookie & MSDC_ASYNC_FLAG)
		return;

	if (data->host_cookie & MSDC_PREPARE_FLAG) {
		dma_unmap_sg(host->dev, data->sg, data->sg_len,
			     mmc_get_dma_dir(data));
		data->host_cookie &= ~MSDC_PREPARE_FLAG;
	}
}

static u64 msdc_timeout_cal(struct msdc_host *host, u64 ns, u64 clks)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	u64 timeout;
	u32 clk_ns, mode = 0;

	if (mmc->actual_clock == 0) {
		timeout = 0;
	} else {
		clk_ns = 1000000000U / mmc->actual_clock;
		timeout = ns + clk_ns - 1;
		do_div(timeout, clk_ns);
		timeout += clks;
		/* in 1048576 sclk cycle unit */
		timeout = DIV_ROUND_UP(timeout, BIT(20));
		if (host->dev_comp->clk_div_bits == 8)
			sdr_get_field(host->base + MSDC_CFG,
				      MSDC_CFG_CKMOD, &mode);
		else
			sdr_get_field(host->base + MSDC_CFG,
				      MSDC_CFG_CKMOD_EXTRA, &mode);
		/*DDR mode will double the clk cycles for data timeout */
		timeout = mode >= 2 ? timeout * 2 : timeout;
		timeout = timeout > 1 ? timeout - 1 : 0;
	}
	return timeout;
}

/* clock control primitives */
static void msdc_set_timeout(struct msdc_host *host, u64 ns, u64 clks)
{
	u64 timeout;

	host->timeout_ns = ns;
	host->timeout_clks = clks;

	timeout = msdc_timeout_cal(host, ns, clks);
	sdr_set_field(host->base + SDC_CFG, SDC_CFG_DTOC,
		      min_t(u32, timeout, 255));
}

static void msdc_set_busy_timeout(struct msdc_host *host, u64 ns, u64 clks)
{
	u64 timeout;

	timeout = msdc_timeout_cal(host, ns, clks);
	sdr_set_field(host->base + SDC_CFG, SDC_CFG_WRDTOC,
		      min_t(u32, timeout, 8191));
}

static void msdc_gate_clock(struct msdc_host *host)
{
	clk_bulk_disable_unprepare(MSDC_NR_CLOCKS, host->bulk_clks);
	clk_disable_unprepare(host->crypto_clk);
	clk_disable_unprepare(host->src_clk_cg);
	clk_disable_unprepare(host->src_clk);
	clk_disable_unprepare(host->bus_clk);
	clk_disable_unprepare(host->h_clk);
}

static int msdc_ungate_clock(struct msdc_host *host)
{
	u32 val;
	int ret;

	ret = clk_prepare_enable(host->h_clk);
	if (ret)
		return ret;
	ret = clk_prepare_enable(host->bus_clk);
	if (ret)
		goto err_hclk;
	ret = clk_prepare_enable(host->src_clk);
	if (ret)
		goto err_bus_clk;
	ret = clk_prepare_enable(host->src_clk_cg);
	if (ret)
		goto err_src_clk;
	ret = clk_prepare_enable(host->crypto_clk);
	if (ret)
		goto err_src_clk_cg;
	ret = clk_bulk_prepare_enable(MSDC_NR_CLOCKS, host->bulk_clks);
	if (ret) {
		dev_err(host->dev, "Cannot enable pclk/axi/ahb clock gates\n");
		goto err_crypto_clk;
	}
	ret = readl_poll_timeout(host->base + MSDC_CFG, val,
				 (val & MSDC_CFG_CKSTB), 1, 20000);
	if (!ret)
		return 0;
	clk_bulk_disable_unprepare(MSDC_NR_CLOCKS, host->bulk_clks);
err_crypto_clk:
	clk_disable_unprepare(host->crypto_clk);
err_src_clk_cg:
	clk_disable_unprepare(host->src_clk_cg);
err_src_clk:
	clk_disable_unprepare(host->src_clk);
err_bus_clk:
	clk_disable_unprepare(host->bus_clk);
err_hclk:
	clk_disable_unprepare(host->h_clk);
	return ret;
}

static void msdc_new_tx_setting(struct msdc_host *host)
{
	u32 val;

	if (!host->top_base)
		return;

	val = readl(host->top_base + LOOP_TEST_CONTROL);
	val |= TEST_LOOP_DSCLK_MUX_SEL;
	val |= TEST_LOOP_LATCH_MUX_SEL;
	val &= ~TEST_HS400_CMD_LOOP_MUX_SEL;

	switch (host->timing) {
	case MMC_TIMING_LEGACY:
	case MMC_TIMING_MMC_HS:
	case MMC_TIMING_SD_HS:
	case MMC_TIMING_UHS_SDR12:
	case MMC_TIMING_UHS_SDR25:
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_MMC_DDR52:
		val &= ~LOOP_EN_SEL_CLK;
		break;
	case MMC_TIMING_UHS_SDR50:
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_MMC_HS200:
	case MMC_TIMING_MMC_HS400:
		val |= LOOP_EN_SEL_CLK;
		break;
	default:
		break;
	}
	writel(val, host->top_base + LOOP_TEST_CONTROL);
}

static int msdc_set_mclk(struct msdc_host *host, unsigned char timing, u32 hz)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	u32 mode;
	u32 flags;
	u32 div;
	u32 sclk;
	u32 tune_reg = host->dev_comp->pad_tune_reg;
	u32 val;
	bool timing_changed;
	int ret;

	if (!hz) {
		dev_dbg(host->dev, "set mclk to 0\n");
		host->mclk = 0;
		mmc->actual_clock = 0;
		sdr_clr_bits(host->base + MSDC_CFG, MSDC_CFG_CKPDN);
		return 0;
	}

	if (host->timing != timing)
		timing_changed = true;
	else
		timing_changed = false;

	flags = readl(host->base + MSDC_INTEN);
	sdr_clr_bits(host->base + MSDC_INTEN, flags);
	if (host->dev_comp->clk_div_bits == 8)
		sdr_clr_bits(host->base + MSDC_CFG, MSDC_CFG_HS400_CK_MODE);
	else
		sdr_clr_bits(host->base + MSDC_CFG,
			     MSDC_CFG_HS400_CK_MODE_EXTRA);
	if (timing == MMC_TIMING_UHS_DDR50 ||
	    timing == MMC_TIMING_MMC_DDR52 ||
	    timing == MMC_TIMING_MMC_HS400) {
		if (timing == MMC_TIMING_MMC_HS400)
			mode = 0x3;
		else
			mode = 0x2; /* ddr mode and use divisor */

		if (hz >= (host->src_clk_freq >> 2)) {
			div = 0; /* mean div = 1/4 */
			sclk = host->src_clk_freq >> 2; /* sclk = clk / 4 */
		} else {
			div = (host->src_clk_freq + ((hz << 2) - 1)) / (hz << 2);
			sclk = (host->src_clk_freq >> 2) / div;
			div = (div >> 1);
		}

		if (timing == MMC_TIMING_MMC_HS400 &&
		    hz >= (host->src_clk_freq >> 1)) {
			if (host->dev_comp->clk_div_bits == 8)
				sdr_set_bits(host->base + MSDC_CFG,
					     MSDC_CFG_HS400_CK_MODE);
			else
				sdr_set_bits(host->base + MSDC_CFG,
					     MSDC_CFG_HS400_CK_MODE_EXTRA);
			sclk = host->src_clk_freq >> 1;
			div = 0; /* div is ignore when bit18 is set */
		}
	} else if (hz >= host->src_clk_freq) {
		mode = 0x1; /* no divisor */
		div = 0;
		sclk = host->src_clk_freq;
	} else {
		mode = 0x0; /* use divisor */
		if (hz >= (host->src_clk_freq >> 1)) {
			div = 0; /* mean div = 1/2 */
			sclk = host->src_clk_freq >> 1; /* sclk = clk / 2 */
		} else {
			div = (host->src_clk_freq + ((hz << 2) - 1)) / (hz << 2);
			sclk = (host->src_clk_freq >> 2) / div;
		}
	}
	sdr_clr_bits(host->base + MSDC_CFG, MSDC_CFG_CKPDN);

	clk_disable_unprepare(host->src_clk_cg);
	if (host->dev_comp->clk_div_bits == 8)
		sdr_set_field(host->base + MSDC_CFG,
			      MSDC_CFG_CKMOD | MSDC_CFG_CKDIV,
			      (mode << 8) | div);
	else
		sdr_set_field(host->base + MSDC_CFG,
			      MSDC_CFG_CKMOD_EXTRA | MSDC_CFG_CKDIV_EXTRA,
			      (mode << 12) | div);

	ret = clk_prepare_enable(host->src_clk_cg);
	if (ret) {
		sdr_set_bits(host->base + MSDC_CFG, MSDC_CFG_CKPDN);
		sdr_set_bits(host->base + MSDC_INTEN, flags);
		return ret;
	}
	readl_poll_timeout(host->base + MSDC_CFG, val, (val & MSDC_CFG_CKSTB), 0, 0);
	sdr_set_bits(host->base + MSDC_CFG, MSDC_CFG_CKPDN);
	mmc->actual_clock = sclk;
	host->mclk = hz;
	host->timing = timing;
	/* need because clk changed. */
	msdc_set_timeout(host, host->timeout_ns, host->timeout_clks);
	sdr_set_bits(host->base + MSDC_INTEN, flags);

	/*
	 * mmc_select_hs400() will drop to 50Mhz and High speed mode,
	 * tune result of hs200/200Mhz is not suitable for 50Mhz
	 */
	if (mmc->actual_clock <= 52000000) {
		writel(host->def_tune_para.iocon, host->base + MSDC_IOCON);
		if (host->top_base) {
			writel(host->def_tune_para.emmc_top_control,
			       host->top_base + EMMC_TOP_CONTROL);
			writel(host->def_tune_para.emmc_top_cmd,
			       host->top_base + EMMC_TOP_CMD);
		} else {
			writel(host->def_tune_para.pad_tune, host->base +
			       (host->z1_direct_cid ? MSDC_PAD_TUNE : tune_reg));
		}
	} else {
		writel(host->saved_tune_para.iocon, host->base + MSDC_IOCON);
		writel(host->saved_tune_para.pad_cmd_tune,
		       host->base + PAD_CMD_TUNE);
		if (host->top_base) {
			writel(host->saved_tune_para.emmc_top_control,
			       host->top_base + EMMC_TOP_CONTROL);
			writel(host->saved_tune_para.emmc_top_cmd,
			       host->top_base + EMMC_TOP_CMD);
		} else {
			writel(host->saved_tune_para.pad_tune, host->base +
			       (host->z1_direct_cid ? MSDC_PAD_TUNE : tune_reg));
		}
	}

	if (timing == MMC_TIMING_MMC_HS400 &&
	    host->dev_comp->hs400_tune)
		sdr_set_field(host->base + tune_reg,
			      MSDC_PAD_TUNE_CMDRRDLY,
			      host->hs400_cmd_int_delay);
	if (host->dev_comp->support_new_tx && timing_changed)
		msdc_new_tx_setting(host);

	dev_dbg(host->dev, "sclk: %d, timing: %d\n", mmc->actual_clock,
		timing);
	return 0;
}

static inline u32 msdc_cmd_find_resp(struct msdc_host *host,
		struct mmc_command *cmd)
{
	u32 resp;

	switch (mmc_resp_type(cmd)) {
	/* Actually, R1, R5, R6, R7 are the same */
	case MMC_RSP_R1:
		resp = 0x1;
		break;
	case MMC_RSP_R1B:
	case MMC_RSP_R1B_NO_CRC:
		resp = 0x7;
		break;
	case MMC_RSP_R2:
		resp = 0x2;
		break;
	case MMC_RSP_R3:
		resp = 0x3;
		break;
	case MMC_RSP_NONE:
	default:
		resp = 0x0;
		break;
	}

	return resp;
}

static inline u32 msdc_cmd_prepare_raw_cmd(struct msdc_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	/* rawcmd :
	 * vol_swt << 30 | auto_cmd << 28 | blklen << 16 | go_irq << 15 |
	 * stop << 14 | rw << 13 | dtype << 11 | rsptyp << 7 | brk << 6 | opcode
	 */
	u32 opcode = cmd->opcode;
	u32 resp = msdc_cmd_find_resp(host, cmd);
	u32 rawcmd = (opcode & 0x3f) | ((resp & 0x7) << 7);

	host->cmd_rsp = resp;

	/*
	 * The MT6572 vendor host programs host->blksz (512) into SDC_CMD even
	 * for CMD1 (sd.c:2890).  Generic mainline leaves BLKLEN clear when no
	 * data request accompanies the command.  Keep the vendor encoding scoped
	 * to the Z1 CID-only probe so its effect can be A/B tested without adding
	 * any eMMC operation beyond CMD0/CMD1/CMD2.
	 */
	if (host->z1_direct_cid && opcode == MMC_SEND_OP_COND && !cmd->data)
		rawcmd |= 512 << 16;

	if ((opcode == SD_IO_RW_DIRECT && cmd->flags == (unsigned int) -1) ||
	    opcode == MMC_STOP_TRANSMISSION)
		rawcmd |= BIT(14);
	else if (opcode == SD_SWITCH_VOLTAGE)
		rawcmd |= BIT(30);
	else if (opcode == SD_APP_SEND_SCR ||
		 opcode == SD_APP_SEND_NUM_WR_BLKS ||
		 (opcode == SD_SWITCH && mmc_cmd_type(cmd) == MMC_CMD_ADTC) ||
		 (opcode == SD_APP_SD_STATUS && mmc_cmd_type(cmd) == MMC_CMD_ADTC) ||
		 (opcode == MMC_SEND_EXT_CSD && mmc_cmd_type(cmd) == MMC_CMD_ADTC))
		rawcmd |= BIT(11);

	if (cmd->data) {
		struct mmc_data *data = cmd->data;

		if (mmc_op_multi(opcode)) {
			if (mmc_card_mmc(mmc->card) && mrq->sbc &&
			    !(mrq->sbc->arg & 0xFFFF0000))
				rawcmd |= BIT(29); /* AutoCMD23 */
		}

		rawcmd |= ((data->blksz & 0xFFF) << 16);
		if (data->flags & MMC_DATA_WRITE)
			rawcmd |= BIT(13);
		if (data->blocks > 1)
			rawcmd |= BIT(12);
		else
			rawcmd |= BIT(11);
		/* Always use dma mode */
		sdr_clr_bits(host->base + MSDC_CFG, MSDC_CFG_PIO);

		if (host->timeout_ns != data->timeout_ns ||
		    host->timeout_clks != data->timeout_clks)
			msdc_set_timeout(host, data->timeout_ns,
					data->timeout_clks);

		writel(data->blocks, host->base + SDC_BLK_NUM);
	}
	return rawcmd;
}

static void msdc_start_data(struct msdc_host *host, struct mmc_command *cmd,
		struct mmc_data *data)
{
	bool read;

	WARN_ON(host->data);
	host->data = data;
	read = data->flags & MMC_DATA_READ;

	mod_delayed_work(system_percpu_wq, &host->req_timeout, DAT_TIMEOUT);
	msdc_dma_setup(host, &host->dma, data);
	sdr_set_bits(host->base + MSDC_INTEN, data_ints_mask);
	sdr_set_field(host->base + MSDC_DMA_CTRL, MSDC_DMA_CTRL_START, 1);
	dev_dbg(host->dev, "DMA start\n");
	dev_dbg(host->dev, "%s: cmd=%d DMA data: %d blocks; read=%d\n",
			__func__, cmd->opcode, data->blocks, read);
}

static int msdc_auto_cmd_done(struct msdc_host *host, int events,
		struct mmc_command *cmd)
{
	u32 *rsp = cmd->resp;

	rsp[0] = readl(host->base + SDC_ACMD_RESP);

	if (events & MSDC_INT_ACMDRDY) {
		cmd->error = 0;
	} else {
		msdc_recover_hw(host);
		if (events & MSDC_INT_ACMDCRCERR) {
			cmd->error = -EILSEQ;
			host->error |= REQ_STOP_EIO;
		} else if (events & MSDC_INT_ACMDTMO) {
			cmd->error = -ETIMEDOUT;
			host->error |= REQ_STOP_TMO;
		}
		dev_err(host->dev,
			"%s: AUTO_CMD%d arg=%08X; rsp %08X; cmd_error=%d\n",
			__func__, cmd->opcode, cmd->arg, rsp[0], cmd->error);
	}
	return cmd->error;
}

/*
 * msdc_recheck_sdio_irq - recheck whether the SDIO irq is lost
 *
 * Host controller may lost interrupt in some special case.
 * Add SDIO irq recheck mechanism to make sure all interrupts
 * can be processed immediately
 */
static void msdc_recheck_sdio_irq(struct msdc_host *host)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	u32 reg_int, reg_inten, reg_ps;

	if (mmc->caps & MMC_CAP_SDIO_IRQ) {
		reg_inten = readl(host->base + MSDC_INTEN);
		if (reg_inten & MSDC_INTEN_SDIOIRQ) {
			reg_int = readl(host->base + MSDC_INT);
			reg_ps = readl(host->base + MSDC_PS);
			if (!(reg_int & MSDC_INT_SDIOIRQ ||
			      reg_ps & MSDC_PS_DATA1)) {
				__msdc_enable_sdio_irq(host, 0);
				sdio_signal_irq(mmc);
			}
		}
	}
}

static void msdc_track_cmd_data(struct msdc_host *host, struct mmc_command *cmd)
{
	if (host->error &&
	    ((!mmc_op_tuning(cmd->opcode) && !host->hs400_tuning) ||
	     cmd->error == -ETIMEDOUT))
		dev_warn(host->dev, "%s: cmd=%d arg=%08X; host->error=0x%08X\n",
			 __func__, cmd->opcode, cmd->arg, host->error);
}

static void msdc_request_done(struct msdc_host *host, struct mmc_request *mrq)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	unsigned long flags;
	bool hsq_req_done;

	/*
	 * No need check the return value of cancel_delayed_work, as only ONE
	 * path will go here!
	 */
	cancel_delayed_work(&host->req_timeout);

	/*
	 * If the request was handled from Host Software Queue, there's almost
	 * nothing to do here, and we also don't need to reset mrq as any race
	 * condition would not have any room to happen, since HSQ stores the
	 * "scheduled" mrqs in an internal array of mrq slots anyway.
	 * However, if the controller experienced an error, we still want to
	 * reset it as soon as possible.
	 *
	 * Note that non-HSQ requests will still be happening at times, even
	 * though it is enabled, and that's what is going to reset host->mrq.
	 * Also, msdc_unprepare_data() is going to be called by HSQ when needed
	 * as HSQ request finalization will eventually call the .post_req()
	 * callback of this driver which, in turn, unprepares the data.
	 */
	hsq_req_done = host->hsq_en ? mmc_hsq_finalize_request(mmc, mrq) : false;
	if (hsq_req_done) {
		if (host->error)
			msdc_recover_hw(host);
		return;
	}

	spin_lock_irqsave(&host->lock, flags);
	host->mrq = NULL;
	spin_unlock_irqrestore(&host->lock, flags);

	msdc_track_cmd_data(host, mrq->cmd);
	if (mrq->data)
		msdc_unprepare_data(host, mrq->data);
	if (host->error)
		msdc_recover_hw(host);
	mmc_request_done(mmc, mrq);
	if (host->dev_comp->recheck_sdio_irq)
		msdc_recheck_sdio_irq(host);
}

/* returns true if command is fully handled; returns false otherwise */
static bool msdc_cmd_done(struct msdc_host *host, int events,
			  struct mmc_request *mrq, struct mmc_command *cmd)
{
	bool done = false;
	bool sbc_error;
	unsigned long flags;
	u32 *rsp;

	if (mrq->sbc && cmd == mrq->cmd &&
	    (events & (MSDC_INT_ACMDRDY | MSDC_INT_ACMDCRCERR
				   | MSDC_INT_ACMDTMO)))
		msdc_auto_cmd_done(host, events, mrq->sbc);

	sbc_error = mrq->sbc && mrq->sbc->error;

	if (!sbc_error && !(events & (MSDC_INT_CMDRDY
					| MSDC_INT_RSPCRCERR
					| MSDC_INT_CMDTMO)))
		return done;

	spin_lock_irqsave(&host->lock, flags);
	done = !host->cmd;
	host->cmd = NULL;
	spin_unlock_irqrestore(&host->lock, flags);

	if (done)
		return true;
	rsp = cmd->resp;

	sdr_clr_bits(host->base + MSDC_INTEN, cmd_ints_mask);

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			rsp[0] = readl(host->base + SDC_RESP3);
			rsp[1] = readl(host->base + SDC_RESP2);
			rsp[2] = readl(host->base + SDC_RESP1);
			rsp[3] = readl(host->base + SDC_RESP0);
		} else {
			rsp[0] = readl(host->base + SDC_RESP0);
		}
	}

	if (host->z1_direct_cid && cmd->opcode == MMC_SEND_OP_COND) {
		u64 complete_ns = ktime_get_ns();
		u32 sts_pre, sts_post, resp_retry, ps;

		/*
		 * Capture timing evidence for the CMD1-zero-response bug.
		 * CMDRDY (MSDC_INT bit8) firing does NOT guarantee the 48-bit
		 * R3 response is latched into SDC_RESP0 — the vendor ISR reads
		 * RESP0 only inside the IRQ, after CMDRDY, with no explicit
		 * SDC_STS wait.  Read SDC_STS before AND after the RESP0 read to
		 * test whether the response is racing the read.  A non-zero
		 * resp_retry would prove a latch-timing bug.
		 *
		 * MSDC_PS[23:16] = DAT[7:0] line levels; bit24 = CMD line level.
		 * During eMMC idle (after CMD0) CMD/DAT0 must read high; if they
		 * read low here the bus is stuck / pad not driven by the eMMC.
		 * MMC_CARD_BUSY = MSDC_PS_DAT all-high is the "busy" state the
		 * open-drain CMD1 R3 response should drive DAT0 high into.
		 */
		sts_pre = readl(host->base + SDC_STS);
		resp_retry = readl(host->base + SDC_RESP0);
		sts_post = readl(host->base + SDC_STS);
		ps = readl(host->base + MSDC_PS);
		dev_info(host->dev,
			 "Z1 CMD1 completion: complete_ns=%llu launch_ns=%llu elapsed_ns=%llu events=%08x inten=%08x resp0_first=%08x resp0_second=%08x sts_pre=%08x sts_post=%08x ps=%08x arg=%08x rawcmd=%08x\n",
			 complete_ns, host->z1_cmd1_start_ns,
			 complete_ns - host->z1_cmd1_start_ns, events,
			 readl(host->base + MSDC_INTEN), rsp[0], resp_retry,
			 sts_pre, sts_post, ps,
			 readl(host->base + SDC_ARG), readl(host->base + SDC_CMD));
	}

	if (!sbc_error && !(events & MSDC_INT_CMDRDY)) {
		if ((events & MSDC_INT_CMDTMO && !host->hs400_tuning) ||
		    (!mmc_op_tuning(cmd->opcode) && !host->hs400_tuning))
			/*
			 * should not clear fifo/interrupt as the tune data
			 * may have already come when cmd19/cmd21 gets response
			 * CRC error.
			 */
			msdc_recover_hw(host);
		if (events & MSDC_INT_RSPCRCERR &&
		    mmc_resp_type(cmd) != MMC_RSP_R1B_NO_CRC) {
			cmd->error = -EILSEQ;
			host->error |= REQ_CMD_EIO;
		} else if (events & MSDC_INT_CMDTMO) {
			cmd->error = -ETIMEDOUT;
			host->error |= REQ_CMD_TMO;
		}
	}
	if (cmd->error) {
		if (host->z1_direct_cid)
			dev_err(host->dev,
				"%s: cmd=%d arg=%08X; rsp %08X; cmd_error=%d events pending\n",
				__func__, cmd->opcode, cmd->arg, rsp[0],
				cmd->error);
		else
			dev_dbg(host->dev,
				"%s: cmd=%d arg=%08X; rsp %08X; cmd_error=%d\n",
				__func__, cmd->opcode, cmd->arg, rsp[0],
				cmd->error);
	}

	msdc_cmd_next(host, mrq, cmd);
	return true;
}

/* It is the core layer's responsibility to ensure card status
 * is correct before issue a request. but host design do below
 * checks recommended.
 */
static inline bool msdc_cmd_is_ready(struct msdc_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd)
{
	u32 val;
	int ret;

	/* The max busy time we can endure is 20ms */
	ret = readl_poll_timeout_atomic(host->base + SDC_STS, val,
					!(val & SDC_STS_CMDBUSY), 1, 20000);
	if (ret) {
		dev_err(host->dev, "CMD bus busy detected\n");
		host->error |= REQ_CMD_BUSY;
		msdc_cmd_done(host, MSDC_INT_CMDTMO, mrq, cmd);
		return false;
	}

	if (mmc_resp_type(cmd) == MMC_RSP_R1B || cmd->data) {
		/* R1B or with data, should check SDCBUSY */
		ret = readl_poll_timeout_atomic(host->base + SDC_STS, val,
						!(val & SDC_STS_SDCBUSY), 1, 20000);
		if (ret) {
			dev_err(host->dev, "Controller busy detected\n");
			host->error |= REQ_CMD_BUSY;
			msdc_cmd_done(host, MSDC_INT_CMDTMO, mrq, cmd);
			return false;
		}
	}
	return true;
}

static void msdc_start_command(struct msdc_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd)
{
	u32 rawcmd;
	unsigned long flags;

	WARN_ON(host->cmd);
	host->cmd = cmd;

	mod_delayed_work(system_percpu_wq, &host->req_timeout, DAT_TIMEOUT);
	if (!msdc_cmd_is_ready(host, mrq, cmd))
		return;

	if ((readl(host->base + MSDC_FIFOCS) & MSDC_FIFOCS_TXCNT) >> 16 ||
	    readl(host->base + MSDC_FIFOCS) & MSDC_FIFOCS_RXCNT) {
		dev_err(host->dev, "TX/RX FIFO non-empty before start of IO. Reset\n");
		msdc_recover_hw(host);
	}

	cmd->error = 0;
	rawcmd = msdc_cmd_prepare_raw_cmd(host, mrq, cmd);

	spin_lock_irqsave(&host->lock, flags);
	sdr_set_bits(host->base + MSDC_INTEN, cmd_ints_mask);
	spin_unlock_irqrestore(&host->lock, flags);

	writel(cmd->arg, host->base + SDC_ARG);
	if (host->z1_direct_cid && cmd->opcode == MMC_SEND_OP_COND) {
		host->z1_cmd1_start_ns = ktime_get_ns();
		dev_info(host->dev,
			 "Z1 CMD1 launch-pre: ts_ns=%llu OCR=%08x rawcmd=%08x cfg=%08x iocon=%08x ps=%08x sts=%08x int=%08x inten=%08x patch1=%08x pad_tune=%08x\n",
			 host->z1_cmd1_start_ns, cmd->arg, rawcmd,
			 readl(host->base + MSDC_CFG), readl(host->base + MSDC_IOCON),
			 readl(host->base + MSDC_PS), readl(host->base + SDC_STS),
			 readl(host->base + MSDC_INT), readl(host->base + MSDC_INTEN),
			 readl(host->base + MSDC_PATCH_BIT1),
			 readl(host->base + MSDC_PAD_TUNE));
	}
	writel(rawcmd, host->base + SDC_CMD);
	if (host->z1_direct_cid && cmd->opcode == MMC_SEND_OP_COND)
		dev_info(host->dev,
			 "Z1 CMD1 launch-post: ts_ns=%llu OCR=%08x rawcmd=%08x cfg=%08x iocon=%08x ps=%08x sts=%08x int=%08x inten=%08x patch1=%08x pad_tune=%08x\n",
			 ktime_get_ns(), cmd->arg, rawcmd,
			 readl(host->base + MSDC_CFG), readl(host->base + MSDC_IOCON),
			 readl(host->base + MSDC_PS), readl(host->base + SDC_STS),
			 readl(host->base + MSDC_INT), readl(host->base + MSDC_INTEN),
			 readl(host->base + MSDC_PATCH_BIT1),
			 readl(host->base + MSDC_PAD_TUNE));
}

static void msdc_cmd_next(struct msdc_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd)
{
	if ((cmd->error && !host->hs400_tuning &&
	     !(cmd->error == -EILSEQ &&
	     mmc_op_tuning(cmd->opcode))) ||
	    (mrq->sbc && mrq->sbc->error))
		msdc_request_done(host, mrq);
	else if (cmd == mrq->sbc)
		msdc_start_command(host, mrq, mrq->cmd);
	else if (!cmd->data)
		msdc_request_done(host, mrq);
	else
		msdc_start_data(host, cmd, cmd->data);
}

static void msdc_ops_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct msdc_host *host = mmc_priv(mmc);

	host->error = 0;
	WARN_ON(!host->hsq_en && host->mrq);
	host->mrq = mrq;

	/* z1_direct_cid now drives only the BOOTRST card release during probe;
	 * the full mmc core enumeration (CMD0..CMD6, data transfers) must be
	 * allowed to reach the host, otherwise the registered mmcblk cannot
	 * complete any read and the card is reported "unusable". */
	if (mrq->data) {
		msdc_prepare_data(host, mrq->data);
		if (!msdc_data_prepared(mrq->data)) {
			host->mrq = NULL;
			/*
			 * Failed to prepare DMA area, fail fast before
			 * starting any commands.
			 */
			mrq->cmd->error = -ENOSPC;
			mmc_request_done(mmc_from_priv(host), mrq);
			return;
		}
	}

	/* if SBC is required, we have HW option and SW option.
	 * if HW option is enabled, and SBC does not have "special" flags,
	 * use HW option,  otherwise use SW option
	 */
	if (mrq->sbc && (!mmc_card_mmc(mmc->card) ||
	    (mrq->sbc->arg & 0xFFFF0000)))
		msdc_start_command(host, mrq, mrq->sbc);
	else
		msdc_start_command(host, mrq, mrq->cmd);
}

static void msdc_pre_req(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	if (!data)
		return;

	msdc_prepare_data(host, data);
	data->host_cookie |= MSDC_ASYNC_FLAG;
}

static void msdc_post_req(struct mmc_host *mmc, struct mmc_request *mrq,
		int err)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	if (!data)
		return;

	if (data->host_cookie) {
		data->host_cookie &= ~MSDC_ASYNC_FLAG;
		msdc_unprepare_data(host, data);
	}
}

static void msdc_data_xfer_next(struct msdc_host *host, struct mmc_request *mrq)
{
	if (mmc_op_multi(mrq->cmd->opcode) && mrq->stop && !mrq->stop->error &&
	    !mrq->sbc)
		msdc_start_command(host, mrq, mrq->stop);
	else
		msdc_request_done(host, mrq);
}

static void msdc_data_xfer_done(struct msdc_host *host, u32 events,
				struct mmc_request *mrq, struct mmc_data *data)
{
	struct mmc_command *stop;
	unsigned long flags;
	bool done;
	unsigned int check_data = events &
	    (MSDC_INT_XFER_COMPL | MSDC_INT_DATCRCERR | MSDC_INT_DATTMO
	     | MSDC_INT_DMA_BDCSERR | MSDC_INT_DMA_GPDCSERR
	     | MSDC_INT_DMA_PROTECT);
	u32 val;
	int ret;

	spin_lock_irqsave(&host->lock, flags);
	done = !host->data;
	if (check_data)
		host->data = NULL;
	spin_unlock_irqrestore(&host->lock, flags);

	if (done)
		return;
	stop = data->stop;

	if (check_data || (stop && stop->error)) {
		dev_dbg(host->dev, "DMA status: 0x%8X\n",
				readl(host->base + MSDC_DMA_CFG));
		sdr_set_field(host->base + MSDC_DMA_CTRL, MSDC_DMA_CTRL_STOP,
				1);

		ret = readl_poll_timeout_atomic(host->base + MSDC_DMA_CTRL, val,
						!(val & MSDC_DMA_CTRL_STOP), 1, 20000);
		if (ret)
			dev_dbg(host->dev, "DMA stop timed out\n");

		ret = readl_poll_timeout_atomic(host->base + MSDC_DMA_CFG, val,
						!(val & MSDC_DMA_CFG_STS), 1, 20000);
		if (ret)
			dev_dbg(host->dev, "DMA inactive timed out\n");

		sdr_clr_bits(host->base + MSDC_INTEN, data_ints_mask);
		dev_dbg(host->dev, "DMA stop\n");

		if ((events & MSDC_INT_XFER_COMPL) && (!stop || !stop->error)) {
			data->bytes_xfered = data->blocks * data->blksz;
		} else {
			dev_dbg(host->dev, "interrupt events: %x\n", events);
			msdc_recover_hw(host);
			host->error |= REQ_DAT_ERR;
			data->bytes_xfered = 0;

			if (events & MSDC_INT_DATTMO)
				data->error = -ETIMEDOUT;
			else if (events & MSDC_INT_DATCRCERR)
				data->error = -EILSEQ;

			dev_dbg(host->dev, "%s: cmd=%d; blocks=%d",
				__func__, mrq->cmd->opcode, data->blocks);
			dev_dbg(host->dev, "data_error=%d xfer_size=%d\n",
				(int)data->error, data->bytes_xfered);
		}

		msdc_data_xfer_next(host, mrq);
	}
}

static void msdc_set_buswidth(struct msdc_host *host, u32 width)
{
	u32 val = readl(host->base + SDC_CFG);

	val &= ~SDC_CFG_BUSWIDTH;

	switch (width) {
	default:
	case MMC_BUS_WIDTH_1:
		val |= (MSDC_BUS_1BITS << 16);
		break;
	case MMC_BUS_WIDTH_4:
		val |= (MSDC_BUS_4BITS << 16);
		break;
	case MMC_BUS_WIDTH_8:
		val |= (MSDC_BUS_8BITS << 16);
		break;
	}

	writel(val, host->base + SDC_CFG);
	dev_dbg(host->dev, "Bus Width = %d", width);
}

static int msdc_ops_switch_volt(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct msdc_host *host = mmc_priv(mmc);
	int ret;

	/* z1_direct_cid no longer short-circuits voltage switching; the full
	 * enumeration path may call this for HS/1.8V transitions.  When there is
	 * no vqmmc supply (Z1 keeps ocr_avail hardcoded and skips the regulator),
	 * mmc->supply.vqmmc is NULL/ERR and the branch below is skipped, which is
	 * correct for the existing 1-bit legacy config. */
	if (!IS_ERR_OR_NULL(mmc->supply.vqmmc)) {
		if (ios->signal_voltage != MMC_SIGNAL_VOLTAGE_330 &&
		    ios->signal_voltage != MMC_SIGNAL_VOLTAGE_180) {
			dev_err(host->dev, "Unsupported signal voltage!\n");
			return -EINVAL;
		}

		ret = mmc_regulator_set_vqmmc(mmc, ios);
		if (ret < 0) {
			dev_dbg(host->dev, "Regulator set error %d (%d)\n",
				ret, ios->signal_voltage);
			return ret;
		}

		/* Apply different pinctrl settings for different signal voltage */
		if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_180)
			pinctrl_select_state(host->pinctrl, host->pins_uhs);
		else
			pinctrl_select_state(host->pinctrl, host->pins_default);
	}
	return 0;
}

static int msdc_card_busy(struct mmc_host *mmc)
{
	struct msdc_host *host = mmc_priv(mmc);
	u32 status = readl(host->base + MSDC_PS);

	/* only check if data0 is low */
	return !(status & BIT(16));
}

static void msdc_request_timeout(struct work_struct *work)
{
	struct msdc_host *host = container_of(work, struct msdc_host,
			req_timeout.work);

	/* simulate HW timeout status */
	dev_err(host->dev, "%s: aborting cmd/data/mrq\n", __func__);
	if (host->mrq) {
		dev_err(host->dev, "%s: aborting mrq=%p cmd=%d\n", __func__,
				host->mrq, host->mrq->cmd->opcode);
		if (host->cmd) {
			dev_err(host->dev, "%s: aborting cmd=%d\n",
					__func__, host->cmd->opcode);
			msdc_cmd_done(host, MSDC_INT_CMDTMO, host->mrq,
					host->cmd);
		} else if (host->data) {
			dev_err(host->dev, "%s: abort data: cmd%d; %d blocks\n",
					__func__, host->mrq->cmd->opcode,
					host->data->blocks);
			msdc_data_xfer_done(host, MSDC_INT_DATTMO, host->mrq,
					host->data);
		}
	}
}

static void __msdc_enable_sdio_irq(struct msdc_host *host, int enb)
{
	if (enb) {
		sdr_set_bits(host->base + MSDC_INTEN, MSDC_INTEN_SDIOIRQ);
		sdr_set_bits(host->base + SDC_CFG, SDC_CFG_SDIOIDE);
		if (host->dev_comp->recheck_sdio_irq)
			msdc_recheck_sdio_irq(host);
	} else {
		sdr_clr_bits(host->base + MSDC_INTEN, MSDC_INTEN_SDIOIRQ);
		sdr_clr_bits(host->base + SDC_CFG, SDC_CFG_SDIOIDE);
	}
}

static void msdc_enable_sdio_irq(struct mmc_host *mmc, int enb)
{
	struct msdc_host *host = mmc_priv(mmc);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&host->lock, flags);
	__msdc_enable_sdio_irq(host, enb);
	spin_unlock_irqrestore(&host->lock, flags);

	if (mmc_card_enable_async_irq(mmc->card) && host->pins_eint) {
		if (enb) {
			/*
			 * In dev_pm_set_dedicated_wake_irq_reverse(), eint pin will be set to
			 * GPIO mode. We need to restore it to SDIO DAT1 mode after that.
			 * Since the current pinstate is pins_uhs, to ensure pinctrl select take
			 * affect successfully, we change the pinstate to pins_eint firstly.
			 */
			pinctrl_select_state(host->pinctrl, host->pins_eint);
			ret = dev_pm_set_dedicated_wake_irq_reverse(host->dev, host->eint_irq);

			if (ret) {
				dev_err(host->dev, "Failed to register SDIO wakeup irq!\n");
				host->pins_eint = NULL;
				pm_runtime_get_noresume(host->dev);
			} else {
				dev_dbg(host->dev, "SDIO eint irq: %d!\n", host->eint_irq);
			}

			pinctrl_select_state(host->pinctrl, host->pins_uhs);
		} else {
			dev_pm_clear_wake_irq(host->dev);
		}
	} else {
		if (enb) {
			/* Ensure host->pins_eint is NULL */
			host->pins_eint = NULL;
			pm_runtime_get_noresume(host->dev);
		} else {
			pm_runtime_put_noidle(host->dev);
		}
	}
}

static irqreturn_t msdc_cmdq_irq(struct msdc_host *host, u32 intsts)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	int cmd_err = 0, dat_err = 0;

	if (intsts & MSDC_INT_RSPCRCERR) {
		cmd_err = -EILSEQ;
		dev_err(host->dev, "%s: CMD CRC ERR", __func__);
	} else if (intsts & MSDC_INT_CMDTMO) {
		cmd_err = -ETIMEDOUT;
		dev_err(host->dev, "%s: CMD TIMEOUT ERR", __func__);
	}

	if (intsts & MSDC_INT_DATCRCERR) {
		dat_err = -EILSEQ;
		dev_err(host->dev, "%s: DATA CRC ERR", __func__);
	} else if (intsts & MSDC_INT_DATTMO) {
		dat_err = -ETIMEDOUT;
		dev_err(host->dev, "%s: DATA TIMEOUT ERR", __func__);
	}

	if (cmd_err || dat_err) {
		dev_err(host->dev, "cmd_err = %d, dat_err = %d, intsts = 0x%x",
			cmd_err, dat_err, intsts);
	}

	return cqhci_irq(mmc, 0, cmd_err, dat_err);
}

static irqreturn_t msdc_irq(int irq, void *dev_id)
{
	struct msdc_host *host = (struct msdc_host *) dev_id;
	struct mmc_host *mmc = mmc_from_priv(host);

	while (true) {
		struct mmc_request *mrq;
		struct mmc_command *cmd;
		struct mmc_data *data;
		u32 events, event_mask;

		spin_lock(&host->lock);
		events = readl(host->base + MSDC_INT);
		event_mask = readl(host->base + MSDC_INTEN);
		if ((events & event_mask) & MSDC_INT_SDIOIRQ)
			__msdc_enable_sdio_irq(host, 0);
		/* clear interrupts */
		writel(events & event_mask, host->base + MSDC_INT);

		mrq = host->mrq;
		cmd = host->cmd;
		data = host->data;
		spin_unlock(&host->lock);

		if ((events & event_mask) & MSDC_INT_SDIOIRQ)
			sdio_signal_irq(mmc);

		if ((events & event_mask) & MSDC_INT_CDSC) {
			if (host->internal_cd)
				mmc_detect_change(mmc, msecs_to_jiffies(20));
			events &= ~MSDC_INT_CDSC;
		}

		if (!(events & (event_mask & ~MSDC_INT_SDIOIRQ)))
			break;

		if ((mmc->caps2 & MMC_CAP2_CQE) &&
		    (events & MSDC_INT_CMDQ)) {
			msdc_cmdq_irq(host, events);
			/* clear interrupts */
			writel(events, host->base + MSDC_INT);
			return IRQ_HANDLED;
		}

		if (!mrq) {
			dev_err(host->dev,
				"%s: MRQ=NULL; events=%08X; event_mask=%08X\n",
				__func__, events, event_mask);
			WARN_ON(1);
			break;
		}

		dev_dbg(host->dev, "%s: events=%08X\n", __func__, events);

		if (cmd)
			msdc_cmd_done(host, events, mrq, cmd);
		else if (data)
			msdc_data_xfer_done(host, events, mrq, data);
	}

	return IRQ_HANDLED;
}

static void msdc_init_hw(struct msdc_host *host)
{
	u32 val, pb1_val, pb2_val;
	u32 tune_reg = host->dev_comp->pad_tune_reg;
	struct mmc_host *mmc = mmc_from_priv(host);

	if (host->reset && !host->z1_direct_cid) {
		reset_control_assert(host->reset);
		usleep_range(10, 50);
		reset_control_deassert(host->reset);
	}

	/* New tx/rx enable bit need to be 0->1 for hardware check */
	if (host->dev_comp->support_new_tx) {
		sdr_clr_bits(host->base + SDC_ADV_CFG0, SDC_NEW_TX_EN);
		sdr_set_bits(host->base + SDC_ADV_CFG0, SDC_NEW_TX_EN);
		msdc_new_tx_setting(host);
	}
	if (host->dev_comp->support_new_rx) {
		sdr_clr_bits(host->base + MSDC_NEW_RX_CFG, MSDC_NEW_RX_PATH_SEL);
		sdr_set_bits(host->base + MSDC_NEW_RX_CFG, MSDC_NEW_RX_PATH_SEL);
	}

	/* Configure to MMC/SD mode, clock free running */
	sdr_set_bits(host->base + MSDC_CFG, MSDC_CFG_MODE | MSDC_CFG_CKPDN);

	/* Reset, except on Z1 where the LK handoff is the A/B baseline. */
	if (!host->z1_direct_cid)
		msdc_recover_hw(host);
	else
		dev_info(host->dev, "Z1 preserving LK MSDC handoff (skip init reset)\n");

	/* Disable and clear all interrupts */
	writel(0, host->base + MSDC_INTEN);
	val = readl(host->base + MSDC_INT);
	writel(val, host->base + MSDC_INT);

	/* Configure card detection */
	if (host->internal_cd) {
		sdr_set_field(host->base + MSDC_PS, MSDC_PS_CDDEBOUNCE,
			      DEFAULT_DEBOUNCE);
		sdr_set_bits(host->base + MSDC_PS, MSDC_PS_CDEN);
		sdr_set_bits(host->base + MSDC_INTEN, MSDC_INTEN_CDSC);
		sdr_set_bits(host->base + SDC_CFG, SDC_CFG_INSWKUP);
	} else {
		sdr_clr_bits(host->base + SDC_CFG, SDC_CFG_INSWKUP);
		sdr_clr_bits(host->base + MSDC_PS, MSDC_PS_CDEN);
		sdr_clr_bits(host->base + MSDC_INTEN, MSDC_INTEN_CDSC);
	}

	if (host->top_base) {
		writel(0, host->top_base + EMMC_TOP_CONTROL);
		writel(0, host->top_base + EMMC_TOP_CMD);
	} else {
		/*
		 * MT6572 calls the generic MT2701 compatibility path, whose
		 * PAD_TUNE0 offset (0xf0) is DAT_RDDLY0 on MT6572.  The vendor
		 * MT6572 initialization clears its actual PAD_TUNE register at
		 * 0xec before issuing commands.  Restrict this correction to the
		 * Z1 CID-only diagnostic; all other controllers keep tune_reg.
		 */
		writel(0, host->base + (host->z1_direct_cid ? MSDC_PAD_TUNE :
						      tune_reg));
	}
	writel(0, host->base + MSDC_IOCON);
	/*
	 * MT6572 vendor MSDC initialization enables the delay-line selector
	 * after clearing IOCON. Keep that board-family setting confined to
	 * the Z1 direct-CID path; other compatible controllers retain their
	 * established default.
	 */
	sdr_set_field(host->base + MSDC_IOCON, MSDC_IOCON_DDLSEL,
		      host->z1_direct_cid ? 1 : 0);

	/*
	 * Patch bit 0 and 1 are completely rewritten, but for patch bit 2
	 * defaults are retained and, if necessary, only some bits are fixed
	 * up: read the PB2 register here for later usage in this function.
	 */
	pb2_val = readl(host->base + MSDC_PATCH_BIT2);

	/* Enable odd number support for 8-bit data bus */
	val = MSDC_PATCH_BIT_ODDSUPP;

	/* Disable SD command register write monitor */
	val |= MSDC_PATCH_BIT_DIS_WRMON;

	/* Issue transfer done interrupt after GPD update */
	val |= MSDC_PATCH_BIT_DESCUP_SEL;

	/* Extend R1B busy detection delay (in clock cycles) */
	val |= FIELD_PREP(MSDC_PATCH_BIT_BUSYDLY, 15);

	/* Enable CRC phase timeout during data write operation */
	val |= MSDC_PATCH_BIT_DECRCTMO;

	/* Set CKGEN delay to one stage */
	val |= FIELD_PREP(MSDC_CKGEN_MSDC_DLY_SEL, 1);

	/* First MSDC_PATCH_BIT setup is done: pull the trigger! */
	writel(val, host->base + MSDC_PATCH_BIT);

	/* Set wr data, crc status, cmd response turnaround period for UHS104 */
	pb1_val = FIELD_PREP(MSDC_PB1_WRDAT_CRC_TACNTR, 1);
	pb1_val |= FIELD_PREP(MSDC_PATCH_BIT1_CMDTA, 1);
	pb1_val |= MSDC_PB1_DDR_CMD_FIX_SEL;

	/* Support 'single' burst type only when AXI_LEN is 0 */
	sdr_get_field(host->base + EMMC50_CFG2, EMMC50_CFG2_AXI_SET_LEN, &val);
	if (!val)
		pb1_val |= MSDC_PB1_SINGLE_BURST;

	/* Set auto sync state clear, block gap stop clk */
	pb1_val |= MSDC_PB1_RSVD20 | MSDC_PB1_AUTO_SYNCST_CLR | MSDC_PB1_MARK_POP_WATER;

	/* Set low power DCM, use HCLK for GDMA, use MSDC CLK for everything else */
	pb1_val |= MSDC_PB1_LP_DCM_EN | MSDC_PB1_RSVD3 |
		   MSDC_PB1_AHB_GDMA_HCLK | MSDC_PB1_MSDC_CLK_ENFEAT;

	/* If needed, enable R1b command busy check at controller init time */
	if (!host->dev_comp->busy_check)
		pb1_val |= MSDC_PB1_BUSY_CHECK_SEL;

	if (host->dev_comp->stop_clk_fix) {
		if (host->dev_comp->stop_dly_sel)
			pb1_val |= FIELD_PREP(MSDC_PATCH_BIT1_STOP_DLY,
					      host->dev_comp->stop_dly_sel);

		if (host->dev_comp->pop_en_cnt) {
			pb2_val &= ~MSDC_PB2_POP_EN_CNT;
			pb2_val |= FIELD_PREP(MSDC_PB2_POP_EN_CNT,
					      host->dev_comp->pop_en_cnt);
		}

		sdr_clr_bits(host->base + SDC_FIFO_CFG, SDC_FIFO_CFG_WRVALIDSEL);
		sdr_clr_bits(host->base + SDC_FIFO_CFG, SDC_FIFO_CFG_RDVALIDSEL);
	}

	if (host->dev_comp->async_fifo) {
		/* Set CMD response timeout multiplier to 65 + (16 * 3) cycles */
		pb2_val &= ~MSDC_PB2_RESPWAIT;
		pb2_val |= FIELD_PREP(MSDC_PB2_RESPWAIT, 3);

		/* eMMC4.5: Select async FIFO path for CMD resp and CRC status */
		pb2_val &= ~MSDC_PATCH_BIT2_CFGRESP;
		pb2_val |= MSDC_PATCH_BIT2_CFGCRCSTS;

		if (!host->dev_comp->enhance_rx) {
			/* eMMC4.5: Delay 2T for CMD resp and CRC status EN signals */
			pb2_val &= ~(MSDC_PB2_RESPSTSENSEL | MSDC_PB2_CRCSTSENSEL);
			pb2_val |= FIELD_PREP(MSDC_PB2_RESPSTSENSEL, 2);
			pb2_val |= FIELD_PREP(MSDC_PB2_CRCSTSENSEL, 2);
		} else if (host->top_base) {
			sdr_set_bits(host->top_base + EMMC_TOP_CONTROL, SDC_RX_ENH_EN);
		} else {
			sdr_set_bits(host->base + SDC_ADV_CFG0, SDC_RX_ENHANCE_EN);
		}
	}

	if (host->dev_comp->support_64g)
		pb2_val |= MSDC_PB2_SUPPORT_64G;

	/* Patch Bit 1/2 setup is done: pull the trigger! */
	writel(pb1_val, host->base + MSDC_PATCH_BIT1);
	writel(pb2_val, host->base + MSDC_PATCH_BIT2);
	sdr_set_bits(host->base + EMMC50_CFG0, EMMC50_CFG_CFCSTS_SEL);

	if (host->dev_comp->data_tune) {
		if (host->top_base) {
			u32 top_ctl_val = readl(host->top_base + EMMC_TOP_CONTROL);
			u32 top_cmd_val = readl(host->top_base + EMMC_TOP_CMD);

			top_cmd_val |= PAD_CMD_RD_RXDLY_SEL;
			top_ctl_val |= PAD_DAT_RD_RXDLY_SEL;
			top_ctl_val &= ~DATA_K_VALUE_SEL;
			if (host->tuning_step > PAD_DELAY_HALF) {
				top_cmd_val |= PAD_CMD_RD_RXDLY2_SEL;
				top_ctl_val |= PAD_DAT_RD_RXDLY2_SEL;
			}

			writel(top_ctl_val, host->top_base + EMMC_TOP_CONTROL);
			writel(top_cmd_val, host->top_base + EMMC_TOP_CMD);
		} else {
			sdr_set_bits(host->base + tune_reg,
				     MSDC_PAD_TUNE_RD_SEL |
				     MSDC_PAD_TUNE_CMD_SEL);
			if (host->tuning_step > PAD_DELAY_HALF)
				sdr_set_bits(host->base + tune_reg + TUNING_REG2_FIXED_OFFEST,
					     MSDC_PAD_TUNE_RD2_SEL |
					     MSDC_PAD_TUNE_CMD2_SEL);
		}
	} else {
		/* choose clock tune */
		if (host->top_base)
			sdr_set_bits(host->top_base + EMMC_TOP_CONTROL,
				     PAD_RXDLY_SEL);
		else
			sdr_set_bits(host->base + tune_reg,
				     MSDC_PAD_TUNE_RXDLYSEL);
	}

	if (mmc->caps2 & MMC_CAP2_NO_SDIO) {
		sdr_clr_bits(host->base + SDC_CFG, SDC_CFG_SDIO);
		sdr_clr_bits(host->base + MSDC_INTEN, MSDC_INTEN_SDIOIRQ);
		sdr_clr_bits(host->base + SDC_ADV_CFG0, SDC_DAT1_IRQ_TRIGGER);
	} else {
		/* Configure to enable SDIO mode, otherwise SDIO CMD5 fails */
		sdr_set_bits(host->base + SDC_CFG, SDC_CFG_SDIO);

		/* Config SDIO device detect interrupt function */
		sdr_clr_bits(host->base + SDC_CFG, SDC_CFG_SDIOIDE);
		sdr_set_bits(host->base + SDC_ADV_CFG0, SDC_DAT1_IRQ_TRIGGER);
	}

	/* Configure to default data timeout */
	sdr_set_field(host->base + SDC_CFG, SDC_CFG_DTOC, 3);

	host->def_tune_para.iocon = readl(host->base + MSDC_IOCON);
	host->saved_tune_para.iocon = readl(host->base + MSDC_IOCON);
	if (host->top_base) {
		host->def_tune_para.emmc_top_control =
			readl(host->top_base + EMMC_TOP_CONTROL);
		host->def_tune_para.emmc_top_cmd =
			readl(host->top_base + EMMC_TOP_CMD);
		host->saved_tune_para.emmc_top_control =
			readl(host->top_base + EMMC_TOP_CONTROL);
		host->saved_tune_para.emmc_top_cmd =
			readl(host->top_base + EMMC_TOP_CMD);
	} else {
		host->def_tune_para.pad_tune = readl(host->base +
			(host->z1_direct_cid ? MSDC_PAD_TUNE : tune_reg));
		host->saved_tune_para.pad_tune = readl(host->base +
			(host->z1_direct_cid ? MSDC_PAD_TUNE : tune_reg));
	}
	dev_dbg(host->dev, "init hardware done!");
}

static void msdc_deinit_hw(struct msdc_host *host)
{
	u32 val;

	if (host->internal_cd) {
		/* Disabled card-detect */
		sdr_clr_bits(host->base + MSDC_PS, MSDC_PS_CDEN);
		sdr_clr_bits(host->base + SDC_CFG, SDC_CFG_INSWKUP);
	}

	/* Disable and clear all interrupts */
	writel(0, host->base + MSDC_INTEN);

	val = readl(host->base + MSDC_INT);
	writel(val, host->base + MSDC_INT);
}

/* init gpd and bd list in msdc_drv_probe */
static void msdc_init_gpd_bd(struct msdc_host *host, struct msdc_dma *dma)
{
	struct mt_gpdma_desc *gpd = dma->gpd;
	struct mt_bdma_desc *bd = dma->bd;
	dma_addr_t dma_addr;
	int i;

	memset(gpd, 0, sizeof(struct mt_gpdma_desc) * 2);

	dma_addr = dma->gpd_addr + sizeof(struct mt_gpdma_desc);
	gpd->gpd_info = GPDMA_DESC_BDP; /* hwo, cs, bd pointer */
	/* gpd->next is must set for desc DMA
	 * That's why must alloc 2 gpd structure.
	 */
	gpd->next = lower_32_bits(dma_addr);
	if (host->dev_comp->support_64g)
		gpd->gpd_info |= (upper_32_bits(dma_addr) & 0xf) << 24;

	dma_addr = dma->bd_addr;
	gpd->ptr = lower_32_bits(dma->bd_addr); /* physical address */
	if (host->dev_comp->support_64g)
		gpd->gpd_info |= (upper_32_bits(dma_addr) & 0xf) << 28;

	memset(bd, 0, sizeof(struct mt_bdma_desc) * MAX_BD_NUM);
	for (i = 0; i < (MAX_BD_NUM - 1); i++) {
		dma_addr = dma->bd_addr + sizeof(*bd) * (i + 1);
		bd[i].next = lower_32_bits(dma_addr);
		if (host->dev_comp->support_64g)
			bd[i].bd_info |= (upper_32_bits(dma_addr) & 0xf) << 24;
	}
}

static void msdc_ops_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct msdc_host *host = mmc_priv(mmc);
	int ret;

	msdc_set_buswidth(host, ios->bus_width);

	/* Suspend/Resume will do power off/on */
	switch (ios->power_mode) {
	case MMC_POWER_UP:
		/* msdc_init_hw must run even without a vmmc supply (Z1 leaves
		 * the rail powered by LK and has no vmmc-supply DT property, so
		 * mmc->supply.vmmc is NULL rather than ERR_PTR).  Only the
		 * regulator call is gated on the supply existing. */
		msdc_init_hw(host);
		if (!IS_ERR_OR_NULL(mmc->supply.vmmc)) {
			ret = mmc_regulator_set_ocr(mmc, mmc->supply.vmmc,
					ios->vdd);
			if (ret) {
				dev_err(host->dev, "Failed to set vmmc power!\n");
				return;
			}
		}
		break;
	case MMC_POWER_ON:
		if (!IS_ERR_OR_NULL(mmc->supply.vqmmc) && !host->vqmmc_enabled) {
			ret = regulator_enable(mmc->supply.vqmmc);
			if (ret)
				dev_err(host->dev, "Failed to set vqmmc power!\n");
			else
				host->vqmmc_enabled = true;
		}
		break;
	case MMC_POWER_OFF:
		if (!IS_ERR_OR_NULL(mmc->supply.vmmc))
			mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, 0);

		if (!IS_ERR_OR_NULL(mmc->supply.vqmmc) && host->vqmmc_enabled) {
			regulator_disable(mmc->supply.vqmmc);
			host->vqmmc_enabled = false;
		}
		break;
	default:
		break;
	}

	if (host->mclk != ios->clock || host->timing != ios->timing)
		msdc_set_mclk(host, ios->timing, ios->clock);
}

static u64 test_delay_bit(u64 delay, u32 bit)
{
	bit %= PAD_DELAY_FULL;
	return delay & BIT_ULL(bit);
}

static int get_delay_len(u64 delay, u32 start_bit)
{
	int i;

	for (i = 0; i < (PAD_DELAY_FULL - start_bit); i++) {
		if (test_delay_bit(delay, start_bit + i) == 0)
			return i;
	}
	return PAD_DELAY_FULL - start_bit;
}

static struct msdc_delay_phase get_best_delay(struct msdc_host *host, u64 delay)
{
	int start = 0, len = 0;
	int start_final = 0, len_final = 0;
	u8 final_phase = 0xff;
	struct msdc_delay_phase delay_phase = { 0, };

	if (delay == 0) {
		dev_err(host->dev, "phase error: [map:%016llx]\n", delay);
		delay_phase.final_phase = final_phase;
		return delay_phase;
	}

	while (start < PAD_DELAY_FULL) {
		len = get_delay_len(delay, start);
		if (len_final < len) {
			start_final = start;
			len_final = len;
		}
		start += len ? len : 1;
		if (!upper_32_bits(delay) && len >= 12 && start_final < 4)
			break;
	}

	/* The rule is that to find the smallest delay cell */
	if (start_final == 0)
		final_phase = (start_final + len_final / 3) % PAD_DELAY_FULL;
	else
		final_phase = (start_final + len_final / 2) % PAD_DELAY_FULL;
	dev_dbg(host->dev, "phase: [map:%016llx] [maxlen:%d] [final:%d]\n",
		delay, len_final, final_phase);

	delay_phase.maxlen = len_final;
	delay_phase.start = start_final;
	delay_phase.final_phase = final_phase;
	return delay_phase;
}

static inline void msdc_set_cmd_delay(struct msdc_host *host, u32 value)
{
	u32 tune_reg = host->dev_comp->pad_tune_reg;

	if (host->top_base) {
		u32 regval = readl(host->top_base + EMMC_TOP_CMD);

		regval &= ~(PAD_CMD_RXDLY | PAD_CMD_RXDLY2);

		if (value < PAD_DELAY_HALF) {
			regval |= FIELD_PREP(PAD_CMD_RXDLY, value);
		} else {
			regval |= FIELD_PREP(PAD_CMD_RXDLY, PAD_DELAY_HALF - 1);
			regval |= FIELD_PREP(PAD_CMD_RXDLY2, value - PAD_DELAY_HALF);
		}
		writel(regval, host->top_base + EMMC_TOP_CMD);
	} else {
		if (value < PAD_DELAY_HALF) {
			sdr_set_field(host->base + tune_reg, MSDC_PAD_TUNE_CMDRDLY, value);
			sdr_set_field(host->base + tune_reg + TUNING_REG2_FIXED_OFFEST,
				      MSDC_PAD_TUNE_CMDRDLY2, 0);
		} else {
			sdr_set_field(host->base + tune_reg, MSDC_PAD_TUNE_CMDRDLY,
				      PAD_DELAY_HALF - 1);
			sdr_set_field(host->base + tune_reg + TUNING_REG2_FIXED_OFFEST,
				      MSDC_PAD_TUNE_CMDRDLY2, value - PAD_DELAY_HALF);
		}
	}
}

static inline void msdc_set_data_delay(struct msdc_host *host, u32 value)
{
	u32 tune_reg = host->dev_comp->pad_tune_reg;

	if (host->top_base) {
		u32 regval = readl(host->top_base + EMMC_TOP_CONTROL);

		regval &= ~(PAD_DAT_RD_RXDLY | PAD_DAT_RD_RXDLY2);

		if (value < PAD_DELAY_HALF) {
			regval |= FIELD_PREP(PAD_DAT_RD_RXDLY, value);
			regval |= FIELD_PREP(PAD_DAT_RD_RXDLY2, value);
		} else {
			regval |= FIELD_PREP(PAD_DAT_RD_RXDLY, PAD_DELAY_HALF - 1);
			regval |= FIELD_PREP(PAD_DAT_RD_RXDLY2, value - PAD_DELAY_HALF);
		}
		writel(regval, host->top_base + EMMC_TOP_CONTROL);
	} else {
		if (value < PAD_DELAY_HALF) {
			sdr_set_field(host->base + tune_reg, MSDC_PAD_TUNE_DATRRDLY, value);
			sdr_set_field(host->base + tune_reg + TUNING_REG2_FIXED_OFFEST,
				      MSDC_PAD_TUNE_DATRRDLY2, 0);
		} else {
			sdr_set_field(host->base + tune_reg, MSDC_PAD_TUNE_DATRRDLY,
				      PAD_DELAY_HALF - 1);
			sdr_set_field(host->base + tune_reg + TUNING_REG2_FIXED_OFFEST,
				      MSDC_PAD_TUNE_DATRRDLY2, value - PAD_DELAY_HALF);
		}
	}
}

static inline void msdc_set_data_sample_edge(struct msdc_host *host, bool rising)
{
	u32 value = rising ? 0 : 1;

	if (host->dev_comp->support_new_rx) {
		sdr_set_field(host->base + MSDC_PATCH_BIT, MSDC_PATCH_BIT_RD_DAT_SEL, value);
		sdr_set_field(host->base + MSDC_PATCH_BIT2, MSDC_PB2_CFGCRCSTSEDGE, value);
	} else {
		sdr_set_field(host->base + MSDC_IOCON, MSDC_IOCON_DSPL, value);
		sdr_set_field(host->base + MSDC_IOCON, MSDC_IOCON_W_DSPL, value);
	}
}

static int msdc_tune_response(struct mmc_host *mmc, u32 opcode)
{
	struct msdc_host *host = mmc_priv(mmc);
	u64 rise_delay = 0, fall_delay = 0;
	struct msdc_delay_phase final_rise_delay, final_fall_delay = { 0,};
	struct msdc_delay_phase internal_delay_phase;
	u8 final_delay, final_maxlen;
	u32 internal_delay = 0;
	u32 tune_reg = host->dev_comp->pad_tune_reg;
	int cmd_err;
	int i, j;

	if (mmc->ios.timing == MMC_TIMING_MMC_HS200 ||
	    mmc->ios.timing == MMC_TIMING_UHS_SDR104)
		sdr_set_field(host->base + tune_reg,
			      MSDC_PAD_TUNE_CMDRRDLY,
			      host->hs200_cmd_int_delay);

	sdr_clr_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	for (i = 0; i < host->tuning_step; i++) {
		msdc_set_cmd_delay(host, i);
		/*
		 * Using the same parameters, it may sometimes pass the test,
		 * but sometimes it may fail. To make sure the parameters are
		 * more stable, we test each set of parameters 3 times.
		 */
		for (j = 0; j < 3; j++) {
			mmc_send_tuning(mmc, opcode, &cmd_err);
			if (!cmd_err) {
				rise_delay |= BIT_ULL(i);
			} else {
				rise_delay &= ~BIT_ULL(i);
				break;
			}
		}
	}
	final_rise_delay = get_best_delay(host, rise_delay);
	/* if rising edge has enough margin, then do not scan falling edge */
	if (final_rise_delay.maxlen >= 12 ||
	    (final_rise_delay.start == 0 && final_rise_delay.maxlen >= 4))
		goto skip_fall;

	sdr_set_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	for (i = 0; i < host->tuning_step; i++) {
		msdc_set_cmd_delay(host, i);
		/*
		 * Using the same parameters, it may sometimes pass the test,
		 * but sometimes it may fail. To make sure the parameters are
		 * more stable, we test each set of parameters 3 times.
		 */
		for (j = 0; j < 3; j++) {
			mmc_send_tuning(mmc, opcode, &cmd_err);
			if (!cmd_err) {
				fall_delay |= BIT_ULL(i);
			} else {
				fall_delay &= ~BIT_ULL(i);
				break;
			}
		}
	}
	final_fall_delay = get_best_delay(host, fall_delay);

skip_fall:
	final_maxlen = max(final_rise_delay.maxlen, final_fall_delay.maxlen);
	if (final_fall_delay.maxlen >= 12 && final_fall_delay.start < 4)
		final_maxlen = final_fall_delay.maxlen;
	if (final_maxlen == final_rise_delay.maxlen) {
		sdr_clr_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
		final_delay = final_rise_delay.final_phase;
	} else {
		sdr_set_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
		final_delay = final_fall_delay.final_phase;
	}
	msdc_set_cmd_delay(host, final_delay);

	if (host->dev_comp->async_fifo || host->hs200_cmd_int_delay)
		goto skip_internal;

	for (i = 0; i < host->tuning_step; i++) {
		sdr_set_field(host->base + tune_reg,
			      MSDC_PAD_TUNE_CMDRRDLY, i);
		mmc_send_tuning(mmc, opcode, &cmd_err);
		if (!cmd_err)
			internal_delay |= BIT_ULL(i);
	}
	dev_dbg(host->dev, "Final internal delay: 0x%x\n", internal_delay);
	internal_delay_phase = get_best_delay(host, internal_delay);
	sdr_set_field(host->base + tune_reg, MSDC_PAD_TUNE_CMDRRDLY,
		      internal_delay_phase.final_phase);
skip_internal:
	dev_dbg(host->dev, "Final cmd pad delay: %x\n", final_delay);
	return final_delay == 0xff ? -EIO : 0;
}

static int hs400_tune_response(struct mmc_host *mmc, u32 opcode)
{
	struct msdc_host *host = mmc_priv(mmc);
	u32 cmd_delay = 0;
	struct msdc_delay_phase final_cmd_delay = { 0,};
	u8 final_delay;
	int cmd_err;
	int i, j;

	/* select EMMC50 PAD CMD tune */
	sdr_set_bits(host->base + PAD_CMD_TUNE, BIT(0));
	sdr_set_field(host->base + MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMDTA, 2);

	if (mmc->ios.timing == MMC_TIMING_MMC_HS200 ||
	    mmc->ios.timing == MMC_TIMING_UHS_SDR104)
		sdr_set_field(host->base + MSDC_PAD_TUNE,
			      MSDC_PAD_TUNE_CMDRRDLY,
			      host->hs200_cmd_int_delay);

	if (host->hs400_cmd_resp_sel_rising)
		sdr_clr_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	else
		sdr_set_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);

	for (i = 0; i < PAD_DELAY_HALF; i++) {
		sdr_set_field(host->base + PAD_CMD_TUNE,
			      PAD_CMD_TUNE_RX_DLY3, i);
		/*
		 * Using the same parameters, it may sometimes pass the test,
		 * but sometimes it may fail. To make sure the parameters are
		 * more stable, we test each set of parameters 3 times.
		 */
		for (j = 0; j < 3; j++) {
			mmc_send_tuning(mmc, opcode, &cmd_err);
			if (!cmd_err) {
				cmd_delay |= BIT(i);
			} else {
				cmd_delay &= ~BIT(i);
				break;
			}
		}
	}
	final_cmd_delay = get_best_delay(host, cmd_delay);
	sdr_set_field(host->base + PAD_CMD_TUNE, PAD_CMD_TUNE_RX_DLY3,
		      final_cmd_delay.final_phase);
	final_delay = final_cmd_delay.final_phase;

	dev_dbg(host->dev, "Final cmd pad delay: %x\n", final_delay);
	return final_delay == 0xff ? -EIO : 0;
}

static int msdc_tune_data(struct mmc_host *mmc, u32 opcode)
{
	struct msdc_host *host = mmc_priv(mmc);
	u64 rise_delay = 0, fall_delay = 0;
	struct msdc_delay_phase final_rise_delay, final_fall_delay = { 0,};
	u8 final_delay, final_maxlen;
	int i, ret;

	sdr_set_field(host->base + MSDC_PATCH_BIT, MSDC_INT_DAT_LATCH_CK_SEL,
		      host->latch_ck);
	msdc_set_data_sample_edge(host, true);
	for (i = 0; i < host->tuning_step; i++) {
		msdc_set_data_delay(host, i);
		ret = mmc_send_tuning(mmc, opcode, NULL);
		if (!ret)
			rise_delay |= BIT_ULL(i);
	}
	final_rise_delay = get_best_delay(host, rise_delay);
	/* if rising edge has enough margin, then do not scan falling edge */
	if (final_rise_delay.maxlen >= 12 ||
	    (final_rise_delay.start == 0 && final_rise_delay.maxlen >= 4))
		goto skip_fall;

	msdc_set_data_sample_edge(host, false);
	for (i = 0; i < host->tuning_step; i++) {
		msdc_set_data_delay(host, i);
		ret = mmc_send_tuning(mmc, opcode, NULL);
		if (!ret)
			fall_delay |= BIT_ULL(i);
	}
	final_fall_delay = get_best_delay(host, fall_delay);

skip_fall:
	final_maxlen = max(final_rise_delay.maxlen, final_fall_delay.maxlen);
	if (final_maxlen == final_rise_delay.maxlen) {
		msdc_set_data_sample_edge(host, true);
		final_delay = final_rise_delay.final_phase;
	} else {
		msdc_set_data_sample_edge(host, false);
		final_delay = final_fall_delay.final_phase;
	}
	msdc_set_data_delay(host, final_delay);

	dev_dbg(host->dev, "Final data pad delay: %x\n", final_delay);
	return final_delay == 0xff ? -EIO : 0;
}

/*
 * MSDC IP which supports data tune + async fifo can do CMD/DAT tune
 * together, which can save the tuning time.
 */
static int msdc_tune_together(struct mmc_host *mmc, u32 opcode)
{
	struct msdc_host *host = mmc_priv(mmc);
	u64 rise_delay = 0, fall_delay = 0;
	struct msdc_delay_phase final_rise_delay, final_fall_delay = { 0,};
	u8 final_delay, final_maxlen;
	int i, ret;

	sdr_set_field(host->base + MSDC_PATCH_BIT, MSDC_INT_DAT_LATCH_CK_SEL,
		      host->latch_ck);

	sdr_clr_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	msdc_set_data_sample_edge(host, true);
	for (i = 0; i < host->tuning_step; i++) {
		msdc_set_cmd_delay(host, i);
		msdc_set_data_delay(host, i);
		ret = mmc_send_tuning(mmc, opcode, NULL);
		if (!ret)
			rise_delay |= BIT_ULL(i);
	}
	final_rise_delay = get_best_delay(host, rise_delay);
	/* if rising edge has enough margin, then do not scan falling edge */
	if (final_rise_delay.maxlen >= 12 ||
	    (final_rise_delay.start == 0 && final_rise_delay.maxlen >= 4))
		goto skip_fall;

	sdr_set_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	msdc_set_data_sample_edge(host, false);
	for (i = 0; i < host->tuning_step; i++) {
		msdc_set_cmd_delay(host, i);
		msdc_set_data_delay(host, i);
		ret = mmc_send_tuning(mmc, opcode, NULL);
		if (!ret)
			fall_delay |= BIT_ULL(i);
	}
	final_fall_delay = get_best_delay(host, fall_delay);

skip_fall:
	final_maxlen = max(final_rise_delay.maxlen, final_fall_delay.maxlen);
	if (final_maxlen == final_rise_delay.maxlen) {
		sdr_clr_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
		msdc_set_data_sample_edge(host, true);
		final_delay = final_rise_delay.final_phase;
	} else {
		sdr_set_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
		msdc_set_data_sample_edge(host, false);
		final_delay = final_fall_delay.final_phase;
	}

	msdc_set_cmd_delay(host, final_delay);
	msdc_set_data_delay(host, final_delay);

	dev_dbg(host->dev, "Final pad delay: %x\n", final_delay);
	return final_delay == 0xff ? -EIO : 0;
}

static int msdc_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct msdc_host *host = mmc_priv(mmc);
	int ret;
	u32 tune_reg = host->dev_comp->pad_tune_reg;

	if (host->dev_comp->data_tune && host->dev_comp->async_fifo) {
		ret = msdc_tune_together(mmc, opcode);
		if (host->hs400_mode) {
			msdc_set_data_sample_edge(host, true);
			msdc_set_data_delay(host, 0);
		}
		goto tune_done;
	}
	if (host->hs400_mode &&
	    host->dev_comp->hs400_tune)
		ret = hs400_tune_response(mmc, opcode);
	else
		ret = msdc_tune_response(mmc, opcode);
	if (ret == -EIO) {
		dev_err(host->dev, "Tune response fail!\n");
		return ret;
	}
	if (host->hs400_mode == false) {
		ret = msdc_tune_data(mmc, opcode);
		if (ret == -EIO)
			dev_err(host->dev, "Tune data fail!\n");
	}

tune_done:
	host->saved_tune_para.iocon = readl(host->base + MSDC_IOCON);
	host->saved_tune_para.pad_tune = readl(host->base + tune_reg);
	host->saved_tune_para.pad_cmd_tune = readl(host->base + PAD_CMD_TUNE);
	if (host->top_base) {
		host->saved_tune_para.emmc_top_control = readl(host->top_base +
				EMMC_TOP_CONTROL);
		host->saved_tune_para.emmc_top_cmd = readl(host->top_base +
				EMMC_TOP_CMD);
	}
	return ret;
}

static int msdc_prepare_hs400_tuning(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct msdc_host *host = mmc_priv(mmc);

	host->hs400_mode = true;

	if (host->top_base) {
		if (host->hs400_ds_dly3)
			sdr_set_field(host->top_base + EMMC50_PAD_DS_TUNE,
				      PAD_DS_DLY3, host->hs400_ds_dly3);
		if (host->hs400_ds_delay)
			writel(host->hs400_ds_delay,
			       host->top_base + EMMC50_PAD_DS_TUNE);
	} else {
		if (host->hs400_ds_dly3)
			sdr_set_field(host->base + PAD_DS_TUNE,
				      PAD_DS_TUNE_DLY3, host->hs400_ds_dly3);
		if (host->hs400_ds_delay)
			writel(host->hs400_ds_delay, host->base + PAD_DS_TUNE);
	}
	/* hs400 mode must set it to 0 */
	sdr_clr_bits(host->base + MSDC_PATCH_BIT2, MSDC_PATCH_BIT2_CFGCRCSTS);
	/* to improve read performance, set outstanding to 2 */
	sdr_set_field(host->base + EMMC50_CFG3, EMMC50_CFG3_OUTS_WR, 2);

	return 0;
}

static int msdc_execute_hs400_tuning(struct mmc_host *mmc, struct mmc_card *card)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct msdc_delay_phase dly1_delay;
	u32 val, result_dly1 = 0;
	u8 *ext_csd;
	int i, ret;

	if (host->top_base) {
		sdr_set_bits(host->top_base + EMMC50_PAD_DS_TUNE,
			     PAD_DS_DLY_SEL);
		sdr_clr_bits(host->top_base + EMMC50_PAD_DS_TUNE,
			     PAD_DS_DLY2_SEL);
	} else {
		sdr_set_bits(host->base + PAD_DS_TUNE, PAD_DS_TUNE_DLY_SEL);
		sdr_clr_bits(host->base + PAD_DS_TUNE, PAD_DS_TUNE_DLY2_SEL);
	}

	host->hs400_tuning = true;
	for (i = 0; i < PAD_DELAY_HALF; i++) {
		if (host->top_base)
			sdr_set_field(host->top_base + EMMC50_PAD_DS_TUNE,
				      PAD_DS_DLY1, i);
		else
			sdr_set_field(host->base + PAD_DS_TUNE,
				      PAD_DS_TUNE_DLY1, i);
		ret = mmc_get_ext_csd(card, &ext_csd);
		if (!ret) {
			result_dly1 |= BIT(i);
			kfree(ext_csd);
		}
	}
	host->hs400_tuning = false;

	dly1_delay = get_best_delay(host, result_dly1);
	if (dly1_delay.maxlen == 0) {
		dev_err(host->dev, "Failed to get DLY1 delay!\n");
		goto fail;
	}
	if (host->top_base)
		sdr_set_field(host->top_base + EMMC50_PAD_DS_TUNE,
			      PAD_DS_DLY1, dly1_delay.final_phase);
	else
		sdr_set_field(host->base + PAD_DS_TUNE,
			      PAD_DS_TUNE_DLY1, dly1_delay.final_phase);

	if (host->top_base)
		val = readl(host->top_base + EMMC50_PAD_DS_TUNE);
	else
		val = readl(host->base + PAD_DS_TUNE);

	dev_info(host->dev, "Final PAD_DS_TUNE: 0x%x\n", val);

	return 0;

fail:
	dev_err(host->dev, "Failed to tuning DS pin delay!\n");
	return -EIO;
}

static void msdc_hw_reset(struct mmc_host *mmc)
{
	struct msdc_host *host = mmc_priv(mmc);

	if (host->z1_direct_cid)
		return;

	sdr_set_bits(host->base + EMMC_IOCON, 1);
	udelay(10); /* 10us is enough */
	sdr_clr_bits(host->base + EMMC_IOCON, 1);
}

static void msdc_ack_sdio_irq(struct mmc_host *mmc)
{
	unsigned long flags;
	struct msdc_host *host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, flags);
	__msdc_enable_sdio_irq(host, 1);
	spin_unlock_irqrestore(&host->lock, flags);
}

static int msdc_get_cd(struct mmc_host *mmc)
{
	struct msdc_host *host = mmc_priv(mmc);
	int val;

	if (mmc->caps & MMC_CAP_NONREMOVABLE)
		return 1;

	if (!host->internal_cd)
		return mmc_gpio_get_cd(mmc);

	val = readl(host->base + MSDC_PS) & MSDC_PS_CDSTS;
	if (mmc->caps2 & MMC_CAP2_CD_ACTIVE_HIGH)
		return !!val;
	else
		return !val;
}

static void msdc_hs400_enhanced_strobe(struct mmc_host *mmc,
				       struct mmc_ios *ios)
{
	struct msdc_host *host = mmc_priv(mmc);

	if (ios->enhanced_strobe) {
		msdc_prepare_hs400_tuning(mmc, ios);
		sdr_set_field(host->base + EMMC50_CFG0, EMMC50_CFG_PADCMD_LATCHCK, 1);
		sdr_set_field(host->base + EMMC50_CFG0, EMMC50_CFG_CMD_RESP_SEL, 1);
		sdr_set_field(host->base + EMMC50_CFG1, EMMC50_CFG1_DS_CFG, 1);

		sdr_clr_bits(host->base + CQHCI_SETTING, CQHCI_RD_CMD_WND_SEL);
		sdr_clr_bits(host->base + CQHCI_SETTING, CQHCI_WR_CMD_WND_SEL);
		sdr_clr_bits(host->base + EMMC51_CFG0, CMDQ_RDAT_CNT);
	} else {
		sdr_set_field(host->base + EMMC50_CFG0, EMMC50_CFG_PADCMD_LATCHCK, 0);
		sdr_set_field(host->base + EMMC50_CFG0, EMMC50_CFG_CMD_RESP_SEL, 0);
		sdr_set_field(host->base + EMMC50_CFG1, EMMC50_CFG1_DS_CFG, 0);

		sdr_set_bits(host->base + CQHCI_SETTING, CQHCI_RD_CMD_WND_SEL);
		sdr_set_bits(host->base + CQHCI_SETTING, CQHCI_WR_CMD_WND_SEL);
		sdr_set_field(host->base + EMMC51_CFG0, CMDQ_RDAT_CNT, 0xb4);
	}
}

static void msdc_cqe_cit_cal(struct msdc_host *host, u64 timer_ns)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	struct cqhci_host *cq_host = mmc->cqe_private;
	u8 itcfmul;
	u64 hclk_freq, value;

	/*
	 * On MediaTek SoCs the MSDC controller's CQE uses msdc_hclk as ITCFVAL
	 * so we multiply/divide the HCLK frequency by ITCFMUL to calculate the
	 * Send Status Command Idle Timer (CIT) value.
	 */
	hclk_freq = (u64)clk_get_rate(host->h_clk);
	itcfmul = CQHCI_ITCFMUL(cqhci_readl(cq_host, CQHCI_CAP));
	switch (itcfmul) {
	case 0x0:
		do_div(hclk_freq, 1000);
		break;
	case 0x1:
		do_div(hclk_freq, 100);
		break;
	case 0x2:
		do_div(hclk_freq, 10);
		break;
	case 0x3:
		break;
	case 0x4:
		hclk_freq = hclk_freq * 10;
		break;
	default:
		host->cq_ssc1_time = 0x40;
		return;
	}

	value = hclk_freq * timer_ns;
	do_div(value, 1000000000);
	host->cq_ssc1_time = value;
}

static void msdc_cqe_enable(struct mmc_host *mmc)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct cqhci_host *cq_host = mmc->cqe_private;

	/* enable cmdq irq */
	writel(MSDC_INT_CMDQ, host->base + MSDC_INTEN);
	/* enable busy check */
	sdr_set_bits(host->base + MSDC_PATCH_BIT1, MSDC_PB1_BUSY_CHECK_SEL);
	/* default write data / busy timeout 20s */
	msdc_set_busy_timeout(host, 20 * 1000000000ULL, 0);
	/* default read data timeout 1s */
	msdc_set_timeout(host, 1000000000ULL, 0);

	/* Set the send status command idle timer */
	cqhci_writel(cq_host, host->cq_ssc1_time, CQHCI_SSC1);
}

static void msdc_cqe_disable(struct mmc_host *mmc, bool recovery)
{
	struct msdc_host *host = mmc_priv(mmc);
	unsigned int val = 0;

	/* disable cmdq irq */
	sdr_clr_bits(host->base + MSDC_INTEN, MSDC_INT_CMDQ);
	/* disable busy check */
	sdr_clr_bits(host->base + MSDC_PATCH_BIT1, MSDC_PB1_BUSY_CHECK_SEL);

	val = readl(host->base + MSDC_INT);
	writel(val, host->base + MSDC_INT);

	if (recovery) {
		sdr_set_field(host->base + MSDC_DMA_CTRL,
			      MSDC_DMA_CTRL_STOP, 1);
		if (WARN_ON(readl_poll_timeout(host->base + MSDC_DMA_CTRL, val,
			!(val & MSDC_DMA_CTRL_STOP), 1, 3000)))
			return;
		if (WARN_ON(readl_poll_timeout(host->base + MSDC_DMA_CFG, val,
			!(val & MSDC_DMA_CFG_STS), 1, 3000)))
			return;
		msdc_recover_hw(host);
	}
}

static void msdc_cqe_pre_enable(struct mmc_host *mmc)
{
	struct cqhci_host *cq_host = mmc->cqe_private;
	u32 reg;

	reg = cqhci_readl(cq_host, CQHCI_CFG);
	reg |= CQHCI_ENABLE;
	cqhci_writel(cq_host, reg, CQHCI_CFG);
}

static void msdc_cqe_post_disable(struct mmc_host *mmc)
{
	struct cqhci_host *cq_host = mmc->cqe_private;
	u32 reg;

	reg = cqhci_readl(cq_host, CQHCI_CFG);
	reg &= ~CQHCI_ENABLE;
	cqhci_writel(cq_host, reg, CQHCI_CFG);
}

static const struct mmc_host_ops mt_msdc_ops = {
	.post_req = msdc_post_req,
	.pre_req = msdc_pre_req,
	.request = msdc_ops_request,
	.set_ios = msdc_ops_set_ios,
	.get_ro = mmc_gpio_get_ro,
	.get_cd = msdc_get_cd,
	.hs400_enhanced_strobe = msdc_hs400_enhanced_strobe,
	.enable_sdio_irq = msdc_enable_sdio_irq,
	.ack_sdio_irq = msdc_ack_sdio_irq,
	.start_signal_voltage_switch = msdc_ops_switch_volt,
	/* .card_busy deliberately absent: Z1's msdc_card_busy reads MSDC_PS
	 * bit16 = DAT0, which stays low (controller-side busy_check artifact,
	 * not a real card busy) after an R1B CMD6 such as the PART_CONFIG
	 * switch before a block read.  With no card_busy callback the core
	 * falls back to CMD13 polling, which succeeds because the card is
	 * actually idle (rsp 00000900 = TRAN/READY_FOR_DATA). */
	.execute_tuning = msdc_execute_tuning,
	.prepare_hs400_tuning = msdc_prepare_hs400_tuning,
	.execute_hs400_tuning = msdc_execute_hs400_tuning,
	.card_hw_reset = msdc_hw_reset,
};

static const struct cqhci_host_ops msdc_cmdq_ops = {
	.enable         = msdc_cqe_enable,
	.disable        = msdc_cqe_disable,
	.pre_enable = msdc_cqe_pre_enable,
	.post_disable = msdc_cqe_post_disable,
};

static void msdc_of_property_parse(struct platform_device *pdev,
				   struct msdc_host *host)
{
	struct mmc_host *mmc = mmc_from_priv(host);

	of_property_read_u32(pdev->dev.of_node, "mediatek,latch-ck",
			     &host->latch_ck);

	of_property_read_u32(pdev->dev.of_node, "hs400-ds-delay",
			     &host->hs400_ds_delay);

	of_property_read_u32(pdev->dev.of_node, "mediatek,hs400-ds-dly3",
			     &host->hs400_ds_dly3);

	of_property_read_u32(pdev->dev.of_node, "mediatek,hs200-cmd-int-delay",
			     &host->hs200_cmd_int_delay);

	of_property_read_u32(pdev->dev.of_node, "mediatek,hs400-cmd-int-delay",
			     &host->hs400_cmd_int_delay);

	if (of_property_read_bool(pdev->dev.of_node,
				  "mediatek,hs400-cmd-resp-sel-rising"))
		host->hs400_cmd_resp_sel_rising = true;
	else
		host->hs400_cmd_resp_sel_rising = false;

	if (of_property_read_u32(pdev->dev.of_node, "mediatek,tuning-step",
				 &host->tuning_step)) {
		if (mmc->caps2 & MMC_CAP2_NO_MMC)
			host->tuning_step = PAD_DELAY_FULL;
		else
			host->tuning_step = PAD_DELAY_HALF;
	}

	if (of_property_read_bool(pdev->dev.of_node,
				  "supports-cqe"))
		host->cqhci = true;
	else
		host->cqhci = false;
}

static int msdc_of_clock_parse(struct platform_device *pdev,
			       struct msdc_host *host)
{
	int ret;

	host->src_clk = devm_clk_get(&pdev->dev, "source");
	if (IS_ERR(host->src_clk))
		return PTR_ERR(host->src_clk);

	host->h_clk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(host->h_clk))
		return PTR_ERR(host->h_clk);

	host->bus_clk = devm_clk_get_optional(&pdev->dev, "bus_clk");
	if (IS_ERR(host->bus_clk))
		host->bus_clk = NULL;

	/*source clock control gate is optional clock*/
	host->src_clk_cg = devm_clk_get_optional(&pdev->dev, "source_cg");
	if (IS_ERR(host->src_clk_cg))
		return PTR_ERR(host->src_clk_cg);

	/*
	 * Fallback for legacy device-trees: src_clk and HCLK use the same
	 * bit to control gating but they are parented to a different mux,
	 * hence if our intention is to gate only the source, required
	 * during a clk mode switch to avoid hw hangs, we need to gate
	 * its parent (specified as a different clock only on new DTs).
	 */
	if (!host->src_clk_cg) {
		host->src_clk_cg = clk_get_parent(host->src_clk);
		if (IS_ERR(host->src_clk_cg))
			return PTR_ERR(host->src_clk_cg);
	}

	/* Z1 enables this gate only after inherited-power validation. */
	if (!host->z1_direct_cid) {
		host->sys_clk_cg =
			devm_clk_get_optional_enabled(&pdev->dev, "sys_cg");
		if (IS_ERR(host->sys_clk_cg))
			return PTR_ERR(host->sys_clk_cg);
	}

	host->bulk_clks[0].id = "pclk_cg";
	host->bulk_clks[1].id = "axi_cg";
	host->bulk_clks[2].id = "ahb_cg";
	ret = devm_clk_bulk_get_optional(&pdev->dev, MSDC_NR_CLOCKS,
					 host->bulk_clks);
	if (ret) {
		dev_err(&pdev->dev, "Cannot get pclk/axi/ahb clock gates\n");
		return ret;
	}

	return 0;
}

static void msdc_z1_dump_mc0_pads(struct msdc_host *host, const char *tag)
{
	void __iomem *gpio;
	void __iomem *iocfg_b;

	/*
	 * MC0 pads (GPIO44/45/46/54) live in the GPIO bank (0x10005000) and
	 * IOCFG-B bank (0x10209000) only, per pinctrl-mt6572.c:
	 *   - MODE:   gpio+0x350 (pin44/45/46), gpio+0x360 (pin54)
	 *   - IES/SMT/R0R1/PUPD/DRV: iocfg_b+0x00/0x20/0x40/0x50/0x60
	 * Pin 54 (MC0_RSTB) shares the SAME iocfg_b+0x60 DRV word as pins 44-49
	 * (bits [7:6] for pin54 vs bits [2:0] for pins44-49). No MC0 pad field
	 * lives in IOCFG-T; a prior iocfg_t+0x80 RSTB-DRV read was wrong.
	 */
	gpio = ioremap(MT6572_GPIO_BASE, SZ_4K);
	iocfg_b = ioremap(MT6572_IOCFG_B_BASE, SZ_4K);
	if (!gpio || !iocfg_b) {
		dev_err(host->dev, "Z1 %s: cannot map MC0 pad registers\n", tag);
		goto out;
	}

	/*
	 * Log raw words so this diagnostic cannot misrepresent shared pad fields.
	 * The corresponding current pinctrl tables decode: modes 44/45/46 and 54,
	 * bank-1 GPIO state, then MC0 IES/SMT/R1R0/PUPD/drive in IOCFG-B.
	 */
	dev_info(host->dev,
		 "Z1 %s MC0-gpio: mode44_46=%08x mode54=%08x dir1=%08x do1=%08x di1=%08x\n",
		 tag, readl(gpio + 0x0350), readl(gpio + 0x0360),
		 readl(gpio + 0x0010), readl(gpio + 0x0110), readl(gpio + 0x0210));
	udelay(1000);	/* throttle */
	dev_info(host->dev,
		 "Z1 %s MSDC state: cfg=%08x iocon=%08x emmc_iocon=%08x bootrst=%u sdc_cfg=%08x adv=%08x\n",
		 tag, readl(host->base + MSDC_CFG), readl(host->base + MSDC_IOCON),
		 readl(host->base + EMMC_IOCON),
		 !!(readl(host->base + EMMC_IOCON) & BIT(0)),
		 readl(host->base + SDC_CFG), readl(host->base + SDC_ADV_CFG0));
	udelay(1000);	/* throttle */
	dev_info(host->dev,
		 "Z1 %s MSDC vendor-diff: pb0=%08x pb1=%08x pb2=%08x pad=%08x rddly0=%08x rddly1=%08x emmc50=%08x\n",
		 tag, readl(host->base + MSDC_PATCH_BIT),
		 readl(host->base + MSDC_PATCH_BIT1),
		 readl(host->base + MSDC_PATCH_BIT2),
		 readl(host->base + MSDC_PAD_TUNE),
		 readl(host->base + MSDC_PAD_TUNE0),
		 readl(host->base + MSDC_PAD_TUNE0 + sizeof(u32)),
		 readl(host->base + EMMC50_CFG0));
	udelay(1000);	/* throttle */
	dev_info(host->dev,
		 "Z1 %s MC0-iocfg-b: ies=%08x smt=%08x r0r1=%08x pupd=%08x drv=%08x\n",
		 tag, readl(iocfg_b + 0x0000), readl(iocfg_b + 0x0020),
		 readl(iocfg_b + 0x0040), readl(iocfg_b + 0x0050),
		 readl(iocfg_b + 0x0060));
out:
	if (iocfg_b)
		iounmap(iocfg_b);
	if (gpio)
		iounmap(gpio);
}

static void __maybe_unused msdc_z1_dump_smt(struct msdc_host *host, const char *tag)
{
	void __iomem *smt;

	smt = ioremap(MT6572_IOCFG_B_MMC0_SMT, sizeof(u32));
	if (!smt) {
		dev_err(host->dev, "Z1 %s: cannot map MMC0 SMT register\n", tag);
		return;
	}

	dev_info(host->dev, "Z1 %s: MMC0_SMT=%08x\n", tag, readl(smt));
	iounmap(smt);
}

static void msdc_z1_dump_regs(struct msdc_host *host, const char *tag)
{
	struct mmc_host *mmc = mmc_from_priv(host);

	dev_info(host->dev,
		 "Z1 %s: src=%lu Hz mclk=%u actual=%u cfg=%08x iocon=%08x ps=%08x sdc_cfg=%08x sdc_sts=%08x inten=%08x int=%08x patch=%08x pb1=%08x\n",
		 tag,
		 clk_get_rate(host->src_clk),
		 host->mclk,
		 mmc->actual_clock,
		 readl(host->base + MSDC_CFG),
		 readl(host->base + MSDC_IOCON),
		 readl(host->base + MSDC_PS),
		 readl(host->base + SDC_CFG),
		 readl(host->base + SDC_STS),
		 readl(host->base + MSDC_INTEN),
		 readl(host->base + MSDC_INT),
		 readl(host->base + MSDC_PATCH_BIT),
		 readl(host->base + MSDC_PATCH_BIT1));
	udelay(1000);	/* throttle: keep the serial console draining */
	dev_info(host->dev,
		 "Z1 %s-extra: cmd=%08x arg=%08x resp0=%08x fifocs=%08x pb2=%08x pad=%08x rddly0=%08x\n",
		 tag, readl(host->base + SDC_CMD), readl(host->base + SDC_ARG),
		 readl(host->base + SDC_RESP0), readl(host->base + MSDC_FIFOCS),
		 readl(host->base + MSDC_PATCH_BIT2), readl(host->base + MSDC_PAD_TUNE),
		 readl(host->base + MSDC_PAD_TUNE0));
}

/*
 * Z1 eMMC hardware reset (EMMC_IOCON.BOOTRST): assert RST_n low, hold,
 * release.  This is the one step neither the mainline init nor the vendor
 * kernel performs on Z1 (board flag MSDC_EMMC_FLAG lacks MSDC_RST_PIN_EN),
 * yet the vendor preloader/LK uses it as the power-on release.  It clears
 * any boot-mode / stuck-busy card state left by the LK eMMC handoff.
 * Card-side state only: no eMMC command sequence, no media register, no
 * user data is touched; only EMMC_IOCON.BIT0 and read-only snapshots.
 */
static void msdc_z1_emmc_hw_reset(struct msdc_host *host)
{
	sdr_set_bits(host->base + EMMC_IOCON, BIT(0));	/* BOOTRST=1: RST_n low */
	msleep(5);
	sdr_clr_bits(host->base + EMMC_IOCON, BIT(0));	/* BOOTRST=0: RST_n high */
	msleep(10);					/* card power-on init window */
	dev_info(host->dev, "Z1 eMMC HW reset done: emmc_iocon=%08x ps=%08x\n",
		 readl(host->base + EMMC_IOCON), readl(host->base + MSDC_PS));
	msdc_z1_dump_mc0_pads(host, "post-emmc-hw-reset");
	msdc_z1_dump_regs(host, "post-emmc-hw-reset");
}

/*
 * IRQ-independent, bounded command issue for the Z1 CID-only path.
 *
 * The LK-handoff MSDC wedges a command after a few open-drain CMD1s
 * (CMDRDY never fires; on 2026-07-31 the diagnostic froze with no further
 * output and no req_timeout recovery).  Polling MSDC_INT with a hard deadline
 * cannot hang the kernel; on wedge the caller recovers the controller and
 * retries.  Used only inside z1_direct_cid with the MSDC IRQ disabled; no
 * eMMC media register or user data is touched.
 */
static int __maybe_unused msdc_z1_cmd_poll(struct msdc_host *host, struct mmc_command *cmd)
{
	struct mmc_request mrq = { .cmd = cmd };
	u32 rawcmd, events;
	unsigned long deadline;

	cmd->error = 0;
	rawcmd = msdc_cmd_prepare_raw_cmd(host, &mrq, cmd);

	sdr_set_bits(host->base + MSDC_INTEN, cmd_ints_mask);
	writel(cmd->arg, host->base + SDC_ARG);
	writel(rawcmd, host->base + SDC_CMD);

	deadline = jiffies + msecs_to_jiffies(300);
	for (;;) {
		events = readl(host->base + MSDC_INT);
		if (events & (MSDC_INT_CMDRDY | MSDC_INT_CMDTMO |
			      MSDC_INT_RSPCRCERR))
			break;
		if (time_after(jiffies, deadline)) {
			dev_info(host->dev, "Z1 cmd%d poll timeout\n",
				 cmd->opcode);
			events = MSDC_INT_CMDTMO;
			break;
		}
		cpu_relax();
		udelay(1);
	}
	writel(events, host->base + MSDC_INT);	/* write-1-to-clear */

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			cmd->resp[0] = readl(host->base + SDC_RESP3);
			cmd->resp[1] = readl(host->base + SDC_RESP2);
			cmd->resp[2] = readl(host->base + SDC_RESP1);
			cmd->resp[3] = readl(host->base + SDC_RESP0);
		} else {
			cmd->resp[0] = readl(host->base + SDC_RESP0);
		}
	}
	if (events & MSDC_INT_CMDTMO) {
		cmd->error = -ETIMEDOUT;
		return -ETIMEDOUT;
	}
	if (events & MSDC_INT_RSPCRCERR) {
		cmd->error = -EILSEQ;
		return -EILSEQ;
	}
	return 0;
}

/*
 * Controller-only recovery after a wedged command: soft-reset the MSDC,
 * restore the 1-bit/400 kHz clock and re-apply the vendor PATCH_BIT1
 * baseline.  No eMMC command, no media register, no user data is touched.
 */
static void __maybe_unused msdc_z1_recover(struct msdc_host *host)
{
	msdc_reset_hw(host);
	msdc_set_buswidth(host, MMC_BUS_WIDTH_1);
	msdc_set_mclk(host, MMC_TIMING_LEGACY, Z1_DIAG_MAX_CLOCK_HZ);
	writel(0xffff0009, host->base + MSDC_PATCH_BIT1);
	msdc_z1_dump_regs(host, "post-recover");
}

static int __maybe_unused msdc_z1_validate_power(struct msdc_host *host)
{
	u32 vemc, vio18;
	int ret;

	if (!mtk_pmic_wrap_z1_read || !mtk_pmic_wrap_z1_probe_status ||
	    !mtk_pmic_wrap_z1_write_deny_active) {
		dev_info(host->dev,
			 "Z1 waiting for PMIC wrapper diagnostic exports\n");
		return -EPROBE_DEFER;
	}

	ret = mtk_pmic_wrap_z1_probe_status();
	if (ret == -ENODEV) {
		dev_info(host->dev,
			 "Z1 waiting for PMIC wrapper diagnostic readiness\n");
		return -EPROBE_DEFER;
	}
	if (ret)
		return ret;

	if (!mtk_pmic_wrap_z1_write_deny_active())
		return -EPERM;
	ret = mtk_pmic_wrap_z1_read(MT6323_DIGLDO_CON6, &vemc);
	if (ret)
		return ret;
	ret = mtk_pmic_wrap_z1_read(MT6323_DIGLDO_CON49, &vio18);
	if (ret)
		return ret;
	dev_info(host->dev,
		 "Z1 power handoff rails: VEMC@0x%04x=%04x VIO18@0x%04x=%04x\n",
		 MT6323_DIGLDO_CON6, vemc, MT6323_DIGLDO_CON49, vio18);
	if ((vemc & (Z1_MT6323_VEMC_EN | Z1_MT6323_VEMC_QI)) !=
	    (Z1_MT6323_VEMC_EN | Z1_MT6323_VEMC_QI) ||
	    (vio18 & (Z1_MT6323_VIO18_EN | Z1_MT6323_VIO18_QI)) !=
	    (Z1_MT6323_VIO18_EN | Z1_MT6323_VIO18_QI)) {
		dev_err(host->dev,
			"Z1 power handoff rejected: rails not ready (need VEMC/VIO18 EN|QI)\n");
		return -EIO;
	}
	return 0;
}

static void msdc_z1_disable_sys_clk(struct msdc_host *host)
{
	if (!host->sys_clk_cg)
		return;

	devm_clk_put(host->dev, host->sys_clk_cg);
	host->sys_clk_cg = NULL;
}

static void __maybe_unused msdc_z1_freeze(struct msdc_host *host,
			   bool src_clk_cg_already_off)
{
	u32 val;

	host->z1_frozen = true;
	writel(0, host->base + MSDC_INTEN);
	val = readl(host->base + MSDC_INT);
	writel(val, host->base + MSDC_INT);
	disable_irq(host->irq);
	synchronize_irq(host->irq);
	cancel_delayed_work_sync(&host->req_timeout);
	msdc_set_mclk(host, MMC_TIMING_LEGACY, 0);
	msdc_deinit_hw(host);
	clk_bulk_disable_unprepare(MSDC_NR_CLOCKS, host->bulk_clks);
	clk_disable_unprepare(host->crypto_clk);
	if (!src_clk_cg_already_off)
		clk_disable_unprepare(host->src_clk_cg);
	clk_disable_unprepare(host->src_clk);
	clk_disable_unprepare(host->bus_clk);
	clk_disable_unprepare(host->h_clk);
	msdc_z1_disable_sys_clk(host);
}

static int __maybe_unused msdc_z1_read_cid(struct mmc_host *mmc)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct mmc_command cmd = {};
	u32 ocr;
	unsigned int delay_us;
	unsigned long deadline;
	int ret, i;
	int fail_opcode = -1;

	mmc->ios.bus_width = MMC_BUS_WIDTH_1;
	mmc->ios.timing = MMC_TIMING_LEGACY;
	msdc_set_buswidth(host, MMC_BUS_WIDTH_1);
	ret = msdc_set_mclk(host, MMC_TIMING_LEGACY, Z1_DIAG_MAX_CLOCK_HZ);
	if (ret) {
		dev_err(host->dev, "Z1 direct CID clock setup failed: %d\n", ret);
		msdc_z1_freeze(host, true);
		return ret;
	}

	/*
	 * A/B handoff experiment: do not reset MSDC_CFG/FIFO here. LK has
	 * already initialized host0 well enough to boot the kernel, and the
	 * previous CID images proved the post-reset path leaves CMD/DAT0 low.
	 * This is host-register state only; the CID-only request gate remains
	 * unchanged and no eMMC media register or user data is touched.
	 */
	dev_info(host->dev, "Z1 preserving LK MSDC handoff (skip host reset)\n");

	/*
	 * MT6572 vendor host0 programs PATCH_BIT1 to 0xffff0009 before
	 * eMMC identification (vendor sd.c:6715-6720).  The generic mainline
	 * setup changes it to 0xffff4089, setting BUSY_CHECK_SEL and
	 * DDR_CMD_FIX_SEL.  On Z1 the latter state coincides with CMD/DAT0
	 * low and an all-zero CMD1 R3 response.  Restrict this vendor baseline
	 * to the CID-only diagnostic path: it only changes MSDC controller
	 * state and remains before CMD0; no eMMC command beyond CMD0/CMD1/CMD2
	 * is permitted by the z1_direct_cid request gate.
	 *
	 * No register dump immediately after this write: two emmcreset2 boots
	 * froze mid-print at ~127 KB of console output (the serial console
	 * stalls under printk load), so the first readback is deferred to after
	 * the eMMC HW reset below and a settle delay is added.
	 */
	writel(0xffff0009, host->base + MSDC_PATCH_BIT1);
	dev_info(host->dev, "Z1 applying MT6572 host0 PB1 baseline=%08x\n",
		 readl(host->base + MSDC_PATCH_BIT1));
	mdelay(1);
	ret = msdc_z1_validate_power(host);
	if (ret) {
		dev_err(host->dev, "Z1 power check after reset failed: %d\n", ret);
		msdc_z1_freeze(host, false);
		return ret;
	}
	/*
	 * Bold single-variable experiment: force a clean eMMC hardware reset.
	 * The LK handoff leaves CMD/DAT0 low (card busy) in every prior build and
	 * CMD1 R3 has never been non-zero.  A RST_n pulse clears boot-mode /
	 * stuck-busy card state that neither mainline init nor the vendor kernel
	 * ever issues on Z1.  Snapshots around it record the effect; still no
	 * eMMC command beyond the CMD0/CMD1/CMD2 gate below.
	 */
	msdc_z1_emmc_hw_reset(host);
	msleep(10);
	msdc_z1_dump_mc0_pads(host, "before-CMD0");

	mmc_claim_host(mmc);

	/*
	 * Bounded-polled identification: the LK-handoff MSDC can wedge a command
	 * after a few open-drain CMD1s (CMDRDY never fires; the previous build
	 * froze with no req_timeout recovery).  Disable the MSDC IRQ and drive
	 * CMD0/CMD1/CMD2 with a hard-deadline poll so a wedge cannot hang the
	 * kernel; recover the controller and retry.  No eMMC command beyond the
	 * CID-only gate, no media register, no user data is touched.
	 */
	disable_irq(host->irq);
	synchronize_irq(host->irq);

	cmd.opcode = MMC_GO_IDLE_STATE;
	cmd.flags = MMC_RSP_NONE | MMC_CMD_BC;
	ret = msdc_z1_cmd_poll(host, &cmd);
	if (ret) {
		msdc_z1_recover(host);
		ret = msdc_z1_cmd_poll(host, &cmd);
	}
	if (ret) {
		fail_opcode = MMC_GO_IDLE_STATE;
		goto out;
	}
	dev_info(host->dev, "Z1 CMD0 (GO_IDLE) ok\n");

	ocr = 0;
	delay_us = 4 * 1000;
	deadline = jiffies + msecs_to_jiffies(15000);
	for (i = 0; time_before_eq(jiffies, deadline); i++) {
		u32 ps;

		/*
		 * Never write into a busy window: wait for the card to release
		 * the open-drain CMD line before issuing the next CMD1.
		 */
		if (readl_poll_timeout_atomic(host->base + MSDC_PS, ps,
					      (ps & MSDC_PS_CMD), 10, 50000)) {
			dev_info(host->dev,
				 "Z1 CMD1 i=%d CMD line busy, waiting\n", i);
			usleep_range(20000, 40000);
			continue;
		}

		memset(&cmd, 0, sizeof(cmd));
		cmd.opcode = MMC_SEND_OP_COND;
		cmd.arg = ocr;
		cmd.flags = MMC_RSP_R3 | MMC_CMD_BCR;
		ret = msdc_z1_cmd_poll(host, &cmd);
		if (ret) {
			/* Tolerate a wedged/timeout CMD1: recover and retry. */
			dev_info(host->dev,
				 "Z1 CMD1 i=%d err=%d, recover+retry\n",
				 i, ret);
			msdc_z1_recover(host);
			usleep_range(10000, 20000);
			continue;
		}
		if (i == 0 || (i % 10) == 0)
			dev_info(host->dev,
				 "Z1 CMD1 i=%d arg=%08x resp=%08x\n",
				 i, ocr, cmd.resp[0]);
		if (cmd.resp[0] & MMC_CARD_BUSY) {
			ocr = cmd.resp[0];
			goto cid;
		}
		ocr = cmd.resp[0] | BIT(30);
		usleep_range(delay_us, delay_us * 2);
		delay_us = min(delay_us * 2, 64U * 1000);
	}
	dev_info(host->dev, "Z1 CMD1 never busy after bounded polling\n");
	ret = -ETIMEDOUT;
	fail_opcode = MMC_SEND_OP_COND;
	goto out;

cid:
	dev_info(host->dev, "Z1 CMD1 ready OCR=%08x\n", ocr);
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = MMC_ALL_SEND_CID;
	cmd.flags = MMC_RSP_R2 | MMC_CMD_BCR;
	ret = msdc_z1_cmd_poll(host, &cmd);
	if (!ret)
		dev_info(host->dev, "Z1 raw CID: %08x %08x %08x %08x\n",
			 cmd.resp[0], cmd.resp[1], cmd.resp[2], cmd.resp[3]);
	else
		fail_opcode = MMC_ALL_SEND_CID;
out:
	if (ret) {
		dev_err(host->dev,
			"Z1 direct CID failed: %d (opcode=%d)\n",
			ret, fail_opcode);
		msdc_z1_dump_regs(host, "CID-fail");
	}
	mmc_release_host(mmc);
	msdc_z1_freeze(host, false);
	return ret;
}

static int msdc_drv_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct msdc_host *host;
	int ret;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "No DT found\n");
		return -EINVAL;
	}

	/* Allocate MMC host for this device */
	mmc = devm_mmc_alloc_host(&pdev->dev, sizeof(struct msdc_host));
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->z1_direct_cid = of_machine_is_compatible("jbo,z1");
	ret = mmc_of_parse(mmc);
	if (ret)
		return ret;

	if (host->z1_direct_cid) {
		/*
		 * The Z1 eMMC is powered by the fixed MT6323 VEMC LDO (~3.0 V)
		 * and the mmc0 node has no vmmc-supply, so mmc_of_parse() leaves
		 * ocr_avail at 0 and mmc_select_voltage() rejects the card with
		 * -EINVAL ("no support for card's volts").  Advertise the 2.7-3.6 V
		 * window the card reports in its OCR; VEMC is a fixed rail, so no
		 * regulator lookup is needed.
		 */
		mmc->ocr_avail = MMC_VDD_27_28 | MMC_VDD_28_29 | MMC_VDD_29_30 |
				 MMC_VDD_30_31 | MMC_VDD_31_32 | MMC_VDD_32_33 |
				 MMC_VDD_33_34 | MMC_VDD_34_35 | MMC_VDD_35_36;
	}

	host->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(host->base))
		return PTR_ERR(host->base);

	host->dev_comp = of_device_get_match_data(&pdev->dev);

	if (host->dev_comp->needs_top_base) {
		host->top_base = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(host->top_base))
			return PTR_ERR(host->top_base);
	}

	if (!host->z1_direct_cid) {
		ret = mmc_regulator_get_supply(mmc);
		if (ret)
			return ret;
	}

	ret = msdc_of_clock_parse(pdev, host);
	if (ret)
		return ret;

	host->reset = devm_reset_control_get_optional_exclusive(&pdev->dev,
								"hrst");
	if (IS_ERR(host->reset))
		return PTR_ERR(host->reset);

	/* only eMMC has crypto property */
	if (!(mmc->caps2 & MMC_CAP2_NO_MMC)) {
		host->crypto_clk = devm_clk_get_optional(&pdev->dev, "crypto");
		if (IS_ERR(host->crypto_clk))
			return PTR_ERR(host->crypto_clk);
		else if (host->crypto_clk)
			mmc->caps2 |= MMC_CAP2_CRYPTO;
	}

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq < 0)
		return host->irq;

	host->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(host->pinctrl))
		return dev_err_probe(&pdev->dev, PTR_ERR(host->pinctrl),
				     "Cannot find pinctrl");

	host->pins_default = pinctrl_lookup_state(host->pinctrl,
			host->z1_direct_cid ? "z1-validated" : "default");
	if (IS_ERR(host->pins_default)) {
		dev_err(&pdev->dev, "Cannot find pinctrl default!\n");
		return PTR_ERR(host->pins_default);
	}

	if (!host->z1_direct_cid) {
		host->pins_uhs = pinctrl_lookup_state(host->pinctrl,
						      "state_uhs");
		if (IS_ERR(host->pins_uhs)) {
			dev_err(&pdev->dev, "Cannot find pinctrl uhs!\n");
			return PTR_ERR(host->pins_uhs);
		}
	}

	/* Support for SDIO eint irq ? */
	if ((mmc->pm_caps & MMC_PM_WAKE_SDIO_IRQ) && (mmc->pm_caps & MMC_PM_KEEP_POWER)) {
		host->eint_irq = platform_get_irq_byname_optional(pdev, "sdio_wakeup");
		if (host->eint_irq > 0) {
			host->pins_eint = pinctrl_lookup_state(host->pinctrl, "state_eint");
			if (IS_ERR(host->pins_eint)) {
				dev_err(&pdev->dev, "Cannot find pinctrl eint!\n");
				host->pins_eint = NULL;
			} else {
				device_init_wakeup(&pdev->dev, true);
			}
		}
	}

	msdc_of_property_parse(pdev, host);

	host->dev = &pdev->dev;
	host->src_clk_freq = clk_get_rate(host->src_clk);
	/* Set host parameters to mmc */
	mmc->ops = &mt_msdc_ops;
	if (host->dev_comp->clk_div_bits == 8)
		mmc->f_min = DIV_ROUND_UP(host->src_clk_freq, 4 * 255);
	else
		mmc->f_min = DIV_ROUND_UP(host->src_clk_freq, 4 * 4095);

	if (!(mmc->caps & MMC_CAP_NONREMOVABLE) &&
	    !mmc_host_can_gpio_cd(mmc) &&
	    host->dev_comp->use_internal_cd) {
		/*
		 * Is removable but no GPIO declared, so
		 * use internal functionality.
		 */
		host->internal_cd = true;
	}

	if (mmc->caps & MMC_CAP_SDIO_IRQ)
		mmc->caps2 |= MMC_CAP2_SDIO_IRQ_NOTHREAD;

	mmc->caps |= MMC_CAP_CMD23;
	if (host->cqhci)
		mmc->caps2 |= MMC_CAP2_CQE | MMC_CAP2_CQE_DCMD;
	/* MMC core transfer sizes tunable parameters */
	mmc->max_segs = MAX_BD_NUM;
	if (host->dev_comp->support_64g)
		mmc->max_seg_size = BDMA_DESC_BUFLEN_EXT;
	else
		mmc->max_seg_size = BDMA_DESC_BUFLEN;
	mmc->max_blk_size = 2048;
	mmc->max_req_size = 512 * 1024;
	mmc->max_blk_count = mmc->max_req_size / 512;
	if (host->dev_comp->support_64g)
		host->dma_mask = DMA_BIT_MASK(36);
	else
		host->dma_mask = DMA_BIT_MASK(32);
	mmc_dev(mmc)->dma_mask = &host->dma_mask;

	if (host->z1_direct_cid) {
		/*
		 * Z1 eMMC bring-up: the LK handoff leaves the eMMC card
		 * stuck-busy (CMD/DAT0 low); only a BOOTRST pulse clears it.  The
		 * release + vendor PATCH_BIT1 baseline are applied after the clock
		 * ungate and controller init below, immediately before mmc_add_host().
		 * No diagnostic gate: the normal mmc core enumeration now drives the
		 * full CMD0/CMD1/CMD2/CMD3/CMD8/CMD6 sequence and registers mmcblk.
		 */
		host->sys_clk_cg =
			devm_clk_get_optional_enabled(&pdev->dev, "sys_cg");
		if (IS_ERR(host->sys_clk_cg))
			return PTR_ERR(host->sys_clk_cg);
	}

	host->timeout_clks = 3 * 1048576;
	host->dma.gpd = dma_alloc_coherent(&pdev->dev,
				2 * sizeof(struct mt_gpdma_desc),
				&host->dma.gpd_addr, GFP_KERNEL);
	host->dma.bd = dma_alloc_coherent(&pdev->dev,
				MAX_BD_NUM * sizeof(struct mt_bdma_desc),
				&host->dma.bd_addr, GFP_KERNEL);
	if (!host->dma.gpd || !host->dma.bd) {
		ret = -ENOMEM;
		goto release_mem;
	}
	msdc_init_gpd_bd(host, &host->dma);
	INIT_DELAYED_WORK(&host->req_timeout, msdc_request_timeout);
	spin_lock_init(&host->lock);

	platform_set_drvdata(pdev, mmc);
	ret = msdc_ungate_clock(host);
	if (ret) {
		dev_err(&pdev->dev, "Cannot ungate clocks!\n");
		goto release_mem;
	}
	msdc_init_hw(host);

	if (host->z1_direct_cid) {
		/*
		 * Release the eMMC card from the LK-handoff stuck-busy state
		 * (BOOTRST assert+release) and restore the vendor PATCH_BIT1
		 * baseline.  Both are controller/card-state only; no media access.
		 * Without the reset the card never answers CMD1 (the historical
		 * resp=0 bug); with it the mmc core below enumerates mmcblk.
		 */
		msdc_z1_emmc_hw_reset(host);
		writel(0xffff0009, host->base + MSDC_PATCH_BIT1);
		dev_info(host->dev, "Z1 eMMC released from stuck-busy; pb1=%08x\n",
			 readl(host->base + MSDC_PATCH_BIT1));
		/*
		 * msdc_ops_set_ios() is a no-op under z1_direct_cid, so the mmc core
		 * never lowers the bus to 1-bit/400 kHz for identification.  A card
		 * released by BOOTRST is back at power-on state and expects <=400 kHz
		 * for CMD0/CMD1/CMD2 (the CID-only diagnostic always used this exact
		 * state).  Set it explicitly before mmc_add_host().
		 */
		msdc_set_buswidth(host, MMC_BUS_WIDTH_1);
		msdc_set_mclk(host, MMC_TIMING_LEGACY, Z1_DIAG_MAX_CLOCK_HZ);
	}

	if (mmc->caps2 & MMC_CAP2_CQE) {
		host->cq_host = devm_kzalloc(mmc->parent,
					     sizeof(*host->cq_host),
					     GFP_KERNEL);
		if (!host->cq_host) {
			ret = -ENOMEM;
			goto release;
		}
		host->cq_host->caps |= CQHCI_TASK_DESC_SZ_128;
		host->cq_host->mmio = host->base + 0x800;
		host->cq_host->ops = &msdc_cmdq_ops;
		ret = cqhci_init(host->cq_host, mmc, true);
		if (ret)
			goto release;
		mmc->max_segs = 128;
		/* cqhci 16bit length */
		/* 0 size, means 65536 so we don't have to -1 here */
		mmc->max_seg_size = 64 * 1024;
		/* Reduce CIT to 0x40 that corresponds to 2.35us */
		msdc_cqe_cit_cal(host, 2350);
	} else if (mmc->caps2 & MMC_CAP2_NO_SDIO) {
		/* Use HSQ on eMMC/SD (but not on SDIO) if HW CQE not supported */
		struct mmc_hsq *hsq = devm_kzalloc(&pdev->dev, sizeof(*hsq), GFP_KERNEL);
		if (!hsq) {
			ret = -ENOMEM;
			goto release;
		}

		ret = mmc_hsq_init(hsq, mmc);
		if (ret)
			goto release;

		host->hsq_en = true;
	}

	ret = devm_request_irq(&pdev->dev, host->irq, msdc_irq,
			       IRQF_TRIGGER_NONE, pdev->name, host);
	if (ret)
		goto release;

	pm_runtime_set_active(host->dev);
	pm_runtime_set_autosuspend_delay(host->dev, MTK_MMC_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(host->dev);
	pm_runtime_enable(host->dev);
	ret = mmc_add_host(mmc);

	if (ret)
		goto end;

	return 0;
end:
	pm_runtime_disable(host->dev);
release:
	msdc_deinit_hw(host);
	msdc_gate_clock(host);
	if (host->z1_direct_cid)
		msdc_z1_disable_sys_clk(host);
	platform_set_drvdata(pdev, NULL);
release_mem:
	platform_set_drvdata(pdev, NULL);
	device_init_wakeup(&pdev->dev, false);
	if (host->dma.gpd)
		dma_free_coherent(&pdev->dev,
			2 * sizeof(struct mt_gpdma_desc),
			host->dma.gpd, host->dma.gpd_addr);
	if (host->dma.bd)
		dma_free_coherent(&pdev->dev,
				  MAX_BD_NUM * sizeof(struct mt_bdma_desc),
				  host->dma.bd, host->dma.bd_addr);
	return ret;
}

static void msdc_drv_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct msdc_host *host;

	mmc = platform_get_drvdata(pdev);
	if (!mmc)
		return;
	host = mmc_priv(mmc);

	if (host->z1_direct_cid) {
		platform_set_drvdata(pdev, NULL);
		if (!host->z1_frozen)
			msdc_z1_freeze(host, false);
		return;
	}

	pm_runtime_get_sync(host->dev);

	platform_set_drvdata(pdev, NULL);
	mmc_remove_host(mmc);
	msdc_deinit_hw(host);
	msdc_gate_clock(host);

	pm_runtime_disable(host->dev);
	pm_runtime_put_noidle(host->dev);
	dma_free_coherent(&pdev->dev,
			2 * sizeof(struct mt_gpdma_desc),
			host->dma.gpd, host->dma.gpd_addr);
	dma_free_coherent(&pdev->dev, MAX_BD_NUM * sizeof(struct mt_bdma_desc),
			  host->dma.bd, host->dma.bd_addr);
	device_init_wakeup(&pdev->dev, false);
}

static void msdc_save_reg(struct msdc_host *host)
{
	u32 tune_reg = host->dev_comp->pad_tune_reg;

	host->save_para.msdc_cfg = readl(host->base + MSDC_CFG);
	host->save_para.iocon = readl(host->base + MSDC_IOCON);
	host->save_para.sdc_cfg = readl(host->base + SDC_CFG);
	host->save_para.patch_bit0 = readl(host->base + MSDC_PATCH_BIT);
	host->save_para.patch_bit1 = readl(host->base + MSDC_PATCH_BIT1);
	host->save_para.patch_bit2 = readl(host->base + MSDC_PATCH_BIT2);
	host->save_para.pad_ds_tune = readl(host->base + PAD_DS_TUNE);
	host->save_para.pad_cmd_tune = readl(host->base + PAD_CMD_TUNE);
	host->save_para.emmc50_cfg0 = readl(host->base + EMMC50_CFG0);
	host->save_para.emmc50_cfg3 = readl(host->base + EMMC50_CFG3);
	host->save_para.sdc_fifo_cfg = readl(host->base + SDC_FIFO_CFG);
	if (host->top_base) {
		host->save_para.emmc_top_control =
			readl(host->top_base + EMMC_TOP_CONTROL);
		host->save_para.emmc_top_cmd =
			readl(host->top_base + EMMC_TOP_CMD);
		host->save_para.emmc50_pad_ds_tune =
			readl(host->top_base + EMMC50_PAD_DS_TUNE);
		host->save_para.loop_test_control =
			readl(host->top_base + LOOP_TEST_CONTROL);
	} else {
		host->save_para.pad_tune = readl(host->base + tune_reg);
	}
}

static void msdc_restore_reg(struct msdc_host *host)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	u32 tune_reg = host->dev_comp->pad_tune_reg;

	if (host->dev_comp->support_new_tx) {
		sdr_clr_bits(host->base + SDC_ADV_CFG0, SDC_NEW_TX_EN);
		sdr_set_bits(host->base + SDC_ADV_CFG0, SDC_NEW_TX_EN);
	}
	if (host->dev_comp->support_new_rx) {
		sdr_clr_bits(host->base + MSDC_NEW_RX_CFG, MSDC_NEW_RX_PATH_SEL);
		sdr_set_bits(host->base + MSDC_NEW_RX_CFG, MSDC_NEW_RX_PATH_SEL);
	}

	writel(host->save_para.msdc_cfg, host->base + MSDC_CFG);
	writel(host->save_para.iocon, host->base + MSDC_IOCON);
	writel(host->save_para.sdc_cfg, host->base + SDC_CFG);
	writel(host->save_para.patch_bit0, host->base + MSDC_PATCH_BIT);
	writel(host->save_para.patch_bit1, host->base + MSDC_PATCH_BIT1);
	writel(host->save_para.patch_bit2, host->base + MSDC_PATCH_BIT2);
	writel(host->save_para.pad_ds_tune, host->base + PAD_DS_TUNE);
	writel(host->save_para.pad_cmd_tune, host->base + PAD_CMD_TUNE);
	writel(host->save_para.emmc50_cfg0, host->base + EMMC50_CFG0);
	writel(host->save_para.emmc50_cfg3, host->base + EMMC50_CFG3);
	writel(host->save_para.sdc_fifo_cfg, host->base + SDC_FIFO_CFG);
	if (host->top_base) {
		writel(host->save_para.emmc_top_control,
		       host->top_base + EMMC_TOP_CONTROL);
		writel(host->save_para.emmc_top_cmd,
		       host->top_base + EMMC_TOP_CMD);
		writel(host->save_para.emmc50_pad_ds_tune,
		       host->top_base + EMMC50_PAD_DS_TUNE);
		writel(host->save_para.loop_test_control,
		       host->top_base + LOOP_TEST_CONTROL);
	} else {
		writel(host->save_para.pad_tune, host->base + tune_reg);
	}

	if (sdio_irq_claimed(mmc))
		__msdc_enable_sdio_irq(host, 1);
}

static int msdc_runtime_suspend(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct msdc_host *host = mmc_priv(mmc);

	if (host->hsq_en)
		mmc_hsq_suspend(mmc);

	msdc_save_reg(host);

	if (sdio_irq_claimed(mmc)) {
		if (host->pins_eint) {
			disable_irq(host->irq);
			pinctrl_select_state(host->pinctrl, host->pins_eint);
		}

		__msdc_enable_sdio_irq(host, 0);
	}
	msdc_gate_clock(host);
	return 0;
}

static int msdc_runtime_resume(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct msdc_host *host = mmc_priv(mmc);
	int ret;

	ret = msdc_ungate_clock(host);
	if (ret)
		return ret;

	msdc_restore_reg(host);

	if (sdio_irq_claimed(mmc) && host->pins_eint) {
		pinctrl_select_state(host->pinctrl, host->pins_uhs);
		enable_irq(host->irq);
	}

	if (host->hsq_en)
		mmc_hsq_resume(mmc);

	return 0;
}

static int msdc_suspend(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct msdc_host *host = mmc_priv(mmc);
	int ret;
	u32 val;

	if (mmc->caps2 & MMC_CAP2_CQE) {
		ret = cqhci_suspend(mmc);
		if (ret)
			return ret;
		val = readl(host->base + MSDC_INT);
		writel(val, host->base + MSDC_INT);
	}

	/*
	 * Bump up runtime PM usage counter otherwise dev->power.needs_force_resume will
	 * not be marked as 1, pm_runtime_force_resume() will go out directly.
	 */
	if (sdio_irq_claimed(mmc) && host->pins_eint)
		pm_runtime_get_noresume(dev);

	return pm_runtime_force_suspend(dev);
}

static int msdc_resume(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct msdc_host *host = mmc_priv(mmc);

	if (sdio_irq_claimed(mmc) && host->pins_eint)
		pm_runtime_put_noidle(dev);

	return pm_runtime_force_resume(dev);
}

static const struct dev_pm_ops msdc_dev_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(msdc_suspend, msdc_resume)
	RUNTIME_PM_OPS(msdc_runtime_suspend, msdc_runtime_resume, NULL)
};

static struct platform_driver mt_msdc_driver = {
	.probe = msdc_drv_probe,
	.remove = msdc_drv_remove,
	.driver = {
		.name = "mtk-msdc",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = msdc_of_ids,
		.pm = pm_ptr(&msdc_dev_pm_ops),
	},
};

module_platform_driver(mt_msdc_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MediaTek SD/MMC Card Driver");
