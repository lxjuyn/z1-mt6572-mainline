// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/units.h>

#include <video/mipi_display.h>
#include <video/videomode.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "mtk_ddp_comp.h"
#include "mtk_disp_drv.h"
#include "mtk_drm_drv.h"

#define DSI_START		0x00

#define DSI_INTEN		0x08

#define DSI_INTSTA		0x0c
#define LPRX_RD_RDY_INT_FLAG		BIT(0)
#define CMD_DONE_INT_FLAG		BIT(1)
#define TE_RDY_INT_FLAG			BIT(2)
#define VM_DONE_INT_FLAG		BIT(3)
#define EXT_TE_RDY_INT_FLAG		BIT(4)
#define DSI_BUSY			BIT(31)

#define DSI_CON_CTRL		0x10
#define DSI_RESET			BIT(0)
#define DSI_EN				BIT(1)
#define DPHY_RESET			BIT(2)

#define DSI_MODE_CTRL		0x14
#define MODE				(3)
#define CMD_MODE			0
#define SYNC_PULSE_MODE			1
#define SYNC_EVENT_MODE			2
#define BURST_MODE			3
#define FRM_MODE			BIT(16)
#define MIX_MODE			BIT(17)

#define DSI_TXRX_CTRL		0x18
#define VC_NUM				BIT(1)
#define LANE_NUM			GENMASK(5, 2)
#define DIS_EOT				BIT(6)
#define NULL_EN				BIT(7)
#define TE_FREERUN			BIT(8)
#define EXT_TE_EN			BIT(9)
#define EXT_TE_EDGE			BIT(10)
#define MAX_RTN_SIZE			GENMASK(15, 12)
#define HSTX_CKLP_EN			BIT(16)

#define DSI_PSCTRL		0x1c
#define DSI_MEM_CONTI		0x90
#define DSI_PS_WC			GENMASK(13, 0)
#define DSI_PS_SEL			GENMASK(17, 16)
#define PACKED_PS_16BIT_RGB565		0
#define PACKED_PS_18BIT_RGB666		1
#define LOOSELY_PS_24BIT_RGB666		2
#define PACKED_PS_24BIT_RGB888		3

#define DSI_VSA_NL		0x20
#define DSI_VBP_NL		0x24
#define DSI_VFP_NL		0x28
#define DSI_VACT_NL		0x2C
#define VACT_NL				GENMASK(14, 0)
#define DSI_SIZE_CON		0x38
#define DSI_HEIGHT				GENMASK(30, 16)
#define DSI_WIDTH				GENMASK(14, 0)
#define DSI_HSA_WC		0x50
#define DSI_HBP_WC		0x54
#define DSI_HFP_WC		0x58
#define HFP_HS_VB_PS_WC		GENMASK(30, 16)
#define HFP_HS_EN			BIT(31)

#define DSI_CMDQ_SIZE		0x60
#define CMDQ_SIZE			0x3f
#define CMDQ_SIZE_SEL		BIT(15)

#define DSI_HSTX_CKL_WC		0x64
#define HSTX_CKL_WC			GENMASK(15, 2)

#define DSI_RX_DATA0		0x74
#define DSI_RX_DATA1		0x78
#define DSI_RX_DATA2		0x7c
#define DSI_RX_DATA3		0x80

#define DSI_RACK		0x84
#define RACK				BIT(0)

#define DSI_PHY_LCCON		0x104
#define LC_HS_TX_EN			BIT(0)
#define LC_ULPM_EN			BIT(1)
#define LC_WAKEUP_EN			BIT(2)

#define DSI_PHY_LD0CON		0x108
#define LD0_HS_TX_EN			BIT(0)
#define LD0_ULPM_EN			BIT(1)
#define LD0_WAKEUP_EN			BIT(2)

#define DSI_PHY_TIMECON0	0x110
#define LPX				GENMASK(7, 0)
#define HS_PREP				GENMASK(15, 8)
#define HS_ZERO				GENMASK(23, 16)
#define HS_TRAIL			GENMASK(31, 24)

#define DSI_PHY_TIMECON1	0x114
#define TA_GO				GENMASK(7, 0)
#define TA_SURE				GENMASK(15, 8)
#define TA_GET				GENMASK(23, 16)
#define DA_HS_EXIT			GENMASK(31, 24)

#define DSI_PHY_TIMECON2	0x118
#define CONT_DET			GENMASK(7, 0)
#define DA_HS_SYNC			GENMASK(15, 8)
#define CLK_ZERO			GENMASK(23, 16)
#define CLK_TRAIL			GENMASK(31, 24)

#define DSI_PHY_TIMECON3	0x11c
#define CLK_HS_PREP			GENMASK(7, 0)
#define CLK_HS_POST			GENMASK(15, 8)
#define CLK_HS_EXIT			GENMASK(23, 16)

/* DSI_VM_CMD_CON (vendor dsi_reg.h: bit0 EN, bit1 LONG_PKT, bit2 TIME_SEL,
 * bit3..5 TS_*_EN, bit8..15 CM_DATA_ID, bit16..23 CM_DATA_0, bit24..31 CM_DATA_1) */
#define VM_CMD_EN			BIT(0)
#define VM_CMD_LONG_PKT			BIT(1)
#define TS_VFP_EN			BIT(5)
#define VM_CMD_CM_DATA_ID_SHIFT		8
#define VM_CMD_CM_DATA_0_SHIFT		16
#define VM_CMD_CM_DATA_1_SHIFT		24
#define DSI_VM_CMD_DATA_OFF(off)	((off) + 4)	/* 0x134 */
#define DSI_START_VM_CMD_START		BIT(16)
#define VM_CMD_DONE_INT_FLAG		BIT(5)

/* DSI_SHADOW_DEBUG */
#define FORCE_COMMIT			BIT(0)
#define BYPASS_SHADOW			BIT(1)

/* CMDQ related bits */
#define CONFIG				GENMASK(7, 0)
#define SHORT_PACKET			0
#define LONG_PACKET			2
#define BTA				BIT(2)
#define HSTX				BIT(3)
#define DATA_ID				GENMASK(15, 8)
#define DATA_0				GENMASK(23, 16)
#define DATA_1				GENMASK(31, 24)

#define NS_TO_CYCLE(n, c)    ((n) / (c) + (((n) % (c)) ? 1 : 0))

#define MTK_DSI_HOST_IS_READ(type) \
	((type == MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM) || \
	(type == MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM) || \
	(type == MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM) || \
	(type == MIPI_DSI_DCS_READ))

struct mtk_phy_timing {
	u32 lpx;
	u32 da_hs_prepare;
	u32 da_hs_zero;
	u32 da_hs_trail;

	u32 ta_go;
	u32 ta_sure;
	u32 ta_get;
	u32 da_hs_exit;

	u32 clk_hs_zero;
	u32 clk_hs_trail;

	u32 clk_hs_prepare;
	u32 clk_hs_post;
	u32 clk_hs_exit;
};

struct phy;

struct mtk_dsi_driver_data {
	const u32 reg_cmdq_off;
	const u32 reg_vm_cmd_off;
	const u32 reg_shadow_dbg_off;
	bool has_shadow_ctl;
	bool has_size_ctl;
	bool cmdq_long_packet_ctl;
	bool support_per_frame_lp;
	bool mt6572_mipi_tx;	/* Z1: use the vendor MIPI-TX register map */
};

struct mtk_dsi {
	struct device *dev;
	struct mipi_dsi_host host;
	struct drm_encoder encoder;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_connector *connector;
	struct phy *phy;

	void __iomem *regs;

	struct clk *engine_clk;
	struct clk *digital_clk;
	struct clk *hs_clk;

	u32 data_rate;

	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	unsigned int lanes;
	struct videomode vm;
	struct mtk_phy_timing phy_timing;
	int refcount;
	bool enabled;
	bool lanes_ready;
	u32 irq_data;
	wait_queue_head_t irq_wait_queue;
	const struct mtk_dsi_driver_data *driver_data;
};

static inline struct mtk_dsi *bridge_to_dsi(struct drm_bridge *b)
{
	return container_of(b, struct mtk_dsi, bridge);
}

static inline struct mtk_dsi *host_to_dsi(struct mipi_dsi_host *h)
{
	return container_of(h, struct mtk_dsi, host);
}

static void mtk_dsi_mask(struct mtk_dsi *dsi, u32 offset, u32 mask, u32 data)
{
	u32 temp = readl(dsi->regs + offset);

	writel((temp & ~mask) | (data & mask), dsi->regs + offset);
}

static void mtk_dsi_phy_timconfig(struct mtk_dsi *dsi)
{
	u32 timcon0, timcon1, timcon2, timcon3;
	u32 data_rate_mhz = DIV_ROUND_UP(dsi->data_rate, HZ_PER_MHZ);
	struct mtk_phy_timing *timing = &dsi->phy_timing;

	timing->lpx = (60 * data_rate_mhz / (8 * 1000)) + 1;
	timing->da_hs_prepare = (80 * data_rate_mhz + 4 * 1000) / 8000;
	timing->da_hs_zero = (170 * data_rate_mhz + 10 * 1000) / 8000 + 1 -
			     timing->da_hs_prepare;
	timing->da_hs_trail = timing->da_hs_prepare + 1;

	timing->ta_go = 4 * timing->lpx - 2;
	timing->ta_sure = timing->lpx + 2;
	timing->ta_get = 4 * timing->lpx;
	timing->da_hs_exit = 2 * timing->lpx + 1;

	timing->clk_hs_prepare = 70 * data_rate_mhz / (8 * 1000);
	timing->clk_hs_post = timing->clk_hs_prepare + 8;
	timing->clk_hs_trail = timing->clk_hs_prepare;
	timing->clk_hs_zero = timing->clk_hs_trail * 4;
	timing->clk_hs_exit = 2 * timing->clk_hs_trail;

	timcon0 = FIELD_PREP(LPX, timing->lpx) |
		  FIELD_PREP(HS_PREP, timing->da_hs_prepare) |
		  FIELD_PREP(HS_ZERO, timing->da_hs_zero) |
		  FIELD_PREP(HS_TRAIL, timing->da_hs_trail);

	timcon1 = FIELD_PREP(TA_GO, timing->ta_go) |
		  FIELD_PREP(TA_SURE, timing->ta_sure) |
		  FIELD_PREP(TA_GET, timing->ta_get) |
		  FIELD_PREP(DA_HS_EXIT, timing->da_hs_exit);

	timcon2 = FIELD_PREP(DA_HS_SYNC, 1) |
		  FIELD_PREP(CLK_ZERO, timing->clk_hs_zero) |
		  FIELD_PREP(CLK_TRAIL, timing->clk_hs_trail);

	timcon3 = FIELD_PREP(CLK_HS_PREP, timing->clk_hs_prepare) |
		  FIELD_PREP(CLK_HS_POST, timing->clk_hs_post) |
		  FIELD_PREP(CLK_HS_EXIT, timing->clk_hs_exit);

	writel(timcon0, dsi->regs + DSI_PHY_TIMECON0);
	writel(timcon1, dsi->regs + DSI_PHY_TIMECON1);
	writel(timcon2, dsi->regs + DSI_PHY_TIMECON2);
	writel(timcon3, dsi->regs + DSI_PHY_TIMECON3);
}

static void mtk_dsi_enable(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_CON_CTRL, DSI_EN, DSI_EN);
}

static void mtk_dsi_disable(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_CON_CTRL, DSI_EN, 0);
}

static void mtk_dsi_reset_engine(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_CON_CTRL, DSI_RESET, DSI_RESET);
	mtk_dsi_mask(dsi, DSI_CON_CTRL, DSI_RESET, 0);
}

static void mtk_dsi_reset_dphy(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_CON_CTRL, DPHY_RESET, DPHY_RESET);
	mtk_dsi_mask(dsi, DSI_CON_CTRL, DPHY_RESET, 0);
}

static void mtk_dsi_clk_ulp_mode_enter(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_PHY_LCCON, LC_HS_TX_EN, 0);
	mtk_dsi_mask(dsi, DSI_PHY_LCCON, LC_ULPM_EN, 0);
}

static void mtk_dsi_clk_ulp_mode_leave(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_PHY_LCCON, LC_ULPM_EN, 0);
	mtk_dsi_mask(dsi, DSI_PHY_LCCON, LC_WAKEUP_EN, LC_WAKEUP_EN);
	mtk_dsi_mask(dsi, DSI_PHY_LCCON, LC_WAKEUP_EN, 0);
}

static void mtk_dsi_lane0_ulp_mode_enter(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_PHY_LD0CON, LD0_HS_TX_EN, 0);
	mtk_dsi_mask(dsi, DSI_PHY_LD0CON, LD0_ULPM_EN, 0);
}

static void mtk_dsi_lane0_ulp_mode_leave(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_PHY_LD0CON, LD0_ULPM_EN, 0);
	mtk_dsi_mask(dsi, DSI_PHY_LD0CON, LD0_WAKEUP_EN, LD0_WAKEUP_EN);
	mtk_dsi_mask(dsi, DSI_PHY_LD0CON, LD0_WAKEUP_EN, 0);
}

static bool mtk_dsi_clk_hs_state(struct mtk_dsi *dsi)
{
	return readl(dsi->regs + DSI_PHY_LCCON) & LC_HS_TX_EN;
}

static void mtk_dsi_clk_hs_mode(struct mtk_dsi *dsi, bool enter)
{
	if (enter && !mtk_dsi_clk_hs_state(dsi))
		mtk_dsi_mask(dsi, DSI_PHY_LCCON, LC_HS_TX_EN, LC_HS_TX_EN);
	else if (!enter && mtk_dsi_clk_hs_state(dsi))
		mtk_dsi_mask(dsi, DSI_PHY_LCCON, LC_HS_TX_EN, 0);
}

static void mtk_dsi_set_mode(struct mtk_dsi *dsi)
{
	u32 vid_mode = CMD_MODE;

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO) {
		if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
			vid_mode = BURST_MODE;
		else if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
			vid_mode = SYNC_PULSE_MODE;
		else
			vid_mode = SYNC_EVENT_MODE;
	}

	writel(vid_mode, dsi->regs + DSI_MODE_CTRL);
}

static void mtk_dsi_set_vm_cmd(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, dsi->driver_data->reg_vm_cmd_off, VM_CMD_EN, VM_CMD_EN);
	mtk_dsi_mask(dsi, dsi->driver_data->reg_vm_cmd_off, TS_VFP_EN, TS_VFP_EN);
}

static void mtk_dsi_rxtx_control(struct mtk_dsi *dsi)
{
	u32 regval, tmp_reg = 0;
	u8 i;

	/* Number of DSI lanes (max 4 lanes), each bit enables one DSI lane. */
	for (i = 0; i < dsi->lanes; i++)
		tmp_reg |= BIT(i);

	regval = FIELD_PREP(LANE_NUM, tmp_reg);

	if (dsi->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS)
		regval |= HSTX_CKLP_EN;

	if (dsi->mode_flags & MIPI_DSI_MODE_NO_EOT_PACKET)
		regval |= DIS_EOT;

	writel(regval, dsi->regs + DSI_TXRX_CTRL);
}

static void mtk_dsi_ps_control(struct mtk_dsi *dsi, bool config_vact)
{
	u32 dsi_buf_bpp, ps_val, ps_wc, vact_nl;

	if (dsi->format == MIPI_DSI_FMT_RGB565)
		dsi_buf_bpp = 2;
	else
		dsi_buf_bpp = 3;

	/* Word count */
	ps_wc = FIELD_PREP(DSI_PS_WC, dsi->vm.hactive * dsi_buf_bpp);
	ps_val = ps_wc;

	/* Pixel Stream type */
	switch (dsi->format) {
	default:
		fallthrough;
	case MIPI_DSI_FMT_RGB888:
		ps_val |= FIELD_PREP(DSI_PS_SEL, PACKED_PS_24BIT_RGB888);
		break;
	case MIPI_DSI_FMT_RGB666:
		ps_val |= FIELD_PREP(DSI_PS_SEL, LOOSELY_PS_24BIT_RGB666);
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		ps_val |= FIELD_PREP(DSI_PS_SEL, PACKED_PS_18BIT_RGB666);
		break;
	case MIPI_DSI_FMT_RGB565:
		ps_val |= FIELD_PREP(DSI_PS_SEL, PACKED_PS_16BIT_RGB565);
		break;
	}

	if (config_vact) {
		vact_nl = FIELD_PREP(VACT_NL, dsi->vm.vactive);
		writel(vact_nl, dsi->regs + DSI_VACT_NL);
		writel(ps_wc, dsi->regs + DSI_HSTX_CKL_WC);
	}
	writel(ps_val, dsi->regs + DSI_PSCTRL);
}

static void mtk_dsi_config_vdo_timing_per_frame_lp(struct mtk_dsi *dsi)
{
	u32 horizontal_sync_active_byte;
	u32 horizontal_backporch_byte;
	u32 horizontal_frontporch_byte;
	u32 hfp_byte_adjust, v_active_adjust;
	u32 cklp_wc_min_adjust, cklp_wc_max_adjust;
	u32 dsi_tmp_buf_bpp;
	unsigned int da_hs_trail;
	unsigned int ps_wc, hs_vb_ps_wc;
	u32 v_active_roundup, hstx_cklp_wc;
	u32 hstx_cklp_wc_max, hstx_cklp_wc_min;
	struct videomode *vm = &dsi->vm;

	if (dsi->format == MIPI_DSI_FMT_RGB565)
		dsi_tmp_buf_bpp = 2;
	else
		dsi_tmp_buf_bpp = 3;

	da_hs_trail = dsi->phy_timing.da_hs_trail;
	ps_wc = vm->hactive * dsi_tmp_buf_bpp;

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE) {
		horizontal_sync_active_byte =
			vm->hsync_len * dsi_tmp_buf_bpp - 10;
		horizontal_backporch_byte =
			vm->hback_porch * dsi_tmp_buf_bpp - 10;
		hfp_byte_adjust = 12;
		v_active_adjust = 32 + horizontal_sync_active_byte;
		cklp_wc_min_adjust = 12 + 2 + 4 + horizontal_sync_active_byte;
		cklp_wc_max_adjust = 20 + 6 + 4 + horizontal_sync_active_byte;
	} else {
		horizontal_sync_active_byte = vm->hsync_len * dsi_tmp_buf_bpp - 4;
		horizontal_backporch_byte = (vm->hback_porch + vm->hsync_len) *
			dsi_tmp_buf_bpp - 10;
		cklp_wc_min_adjust = 4;
		cklp_wc_max_adjust = 12 + 4 + 4;
		if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST) {
			hfp_byte_adjust = 18;
			v_active_adjust = 28;
		} else {
			hfp_byte_adjust = 12;
			v_active_adjust = 22;
		}
	}
	horizontal_frontporch_byte = vm->hfront_porch * dsi_tmp_buf_bpp - hfp_byte_adjust;
	v_active_roundup = (v_active_adjust + horizontal_backporch_byte + ps_wc +
			   horizontal_frontporch_byte) % dsi->lanes;
	if (v_active_roundup)
		horizontal_backporch_byte += dsi->lanes - v_active_roundup;
	hstx_cklp_wc_min = (DIV_ROUND_UP(cklp_wc_min_adjust, dsi->lanes) + da_hs_trail + 1)
			   * dsi->lanes / 6 - 1;
	hstx_cklp_wc_max = (DIV_ROUND_UP((cklp_wc_max_adjust + horizontal_backporch_byte +
			   ps_wc), dsi->lanes) + da_hs_trail + 1) * dsi->lanes / 6 - 1;

	hstx_cklp_wc = FIELD_PREP(HSTX_CKL_WC, (hstx_cklp_wc_min + hstx_cklp_wc_max) / 2);
	writel(hstx_cklp_wc, dsi->regs + DSI_HSTX_CKL_WC);

	hs_vb_ps_wc = ps_wc - (dsi->phy_timing.lpx + dsi->phy_timing.da_hs_exit +
		      dsi->phy_timing.da_hs_prepare + dsi->phy_timing.da_hs_zero + 2) * dsi->lanes;
	horizontal_frontporch_byte |= FIELD_PREP(HFP_HS_EN, 1) |
				      FIELD_PREP(HFP_HS_VB_PS_WC, hs_vb_ps_wc);

	writel(horizontal_sync_active_byte, dsi->regs + DSI_HSA_WC);
	writel(horizontal_backporch_byte, dsi->regs + DSI_HBP_WC);
	writel(horizontal_frontporch_byte, dsi->regs + DSI_HFP_WC);
}

static void mtk_dsi_config_vdo_timing_per_line_lp(struct mtk_dsi *dsi)
{
	u32 horizontal_sync_active_byte;
	u32 horizontal_backporch_byte;
	u32 horizontal_frontporch_byte;
	u32 horizontal_front_back_byte;
	u32 data_phy_cycles_byte;
	u32 dsi_tmp_buf_bpp, data_phy_cycles;
	u32 delta;
	struct mtk_phy_timing *timing = &dsi->phy_timing;
	struct videomode *vm = &dsi->vm;

	if (dsi->format == MIPI_DSI_FMT_RGB565)
		dsi_tmp_buf_bpp = 2;
	else
		dsi_tmp_buf_bpp = 3;

	horizontal_sync_active_byte = (vm->hsync_len * dsi_tmp_buf_bpp - 10);

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
		horizontal_backporch_byte = vm->hback_porch * dsi_tmp_buf_bpp - 10;
	else
		horizontal_backporch_byte = (vm->hback_porch + vm->hsync_len) *
					    dsi_tmp_buf_bpp - 10;

	data_phy_cycles = timing->lpx + timing->da_hs_prepare +
			  timing->da_hs_zero + timing->da_hs_exit + 3;

	delta = dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST ? 18 : 12;
	delta += dsi->mode_flags & MIPI_DSI_MODE_NO_EOT_PACKET ? 0 : 2;

	horizontal_frontporch_byte = vm->hfront_porch * dsi_tmp_buf_bpp;
	horizontal_front_back_byte = horizontal_frontporch_byte + horizontal_backporch_byte;
	data_phy_cycles_byte = data_phy_cycles * dsi->lanes + delta;

	if (horizontal_front_back_byte > data_phy_cycles_byte) {
		horizontal_frontporch_byte -= data_phy_cycles_byte *
					      horizontal_frontporch_byte /
					      horizontal_front_back_byte;

		horizontal_backporch_byte -= data_phy_cycles_byte *
					     horizontal_backporch_byte /
					     horizontal_front_back_byte;
	} else {
		DRM_WARN("HFP + HBP less than d-phy, FPS will under 60Hz\n");
	}

	if ((dsi->mode_flags & MIPI_DSI_HS_PKT_END_ALIGNED) &&
	    (dsi->lanes == 4)) {
		horizontal_sync_active_byte =
			roundup(horizontal_sync_active_byte, dsi->lanes) - 2;
		horizontal_frontporch_byte =
			roundup(horizontal_frontporch_byte, dsi->lanes) - 2;
		horizontal_backporch_byte =
			roundup(horizontal_backporch_byte, dsi->lanes) - 2;
		horizontal_backporch_byte -=
			(vm->hactive * dsi_tmp_buf_bpp + 2) % dsi->lanes;
	}

	writel(horizontal_sync_active_byte, dsi->regs + DSI_HSA_WC);
	writel(horizontal_backporch_byte, dsi->regs + DSI_HBP_WC);
	writel(horizontal_frontporch_byte, dsi->regs + DSI_HFP_WC);
}

static void mtk_dsi_config_vdo_timing(struct mtk_dsi *dsi)
{
	struct videomode *vm = &dsi->vm;

	writel(vm->vsync_len, dsi->regs + DSI_VSA_NL);
	writel(vm->vback_porch, dsi->regs + DSI_VBP_NL);
	writel(vm->vfront_porch, dsi->regs + DSI_VFP_NL);
	writel(vm->vactive, dsi->regs + DSI_VACT_NL);

	if (dsi->driver_data->has_size_ctl)
		writel(FIELD_PREP(DSI_HEIGHT, vm->vactive) |
			FIELD_PREP(DSI_WIDTH, vm->hactive),
			dsi->regs + DSI_SIZE_CON);

	if (dsi->driver_data->support_per_frame_lp)
		mtk_dsi_config_vdo_timing_per_frame_lp(dsi);
	else
		mtk_dsi_config_vdo_timing_per_line_lp(dsi);

	mtk_dsi_ps_control(dsi, false);
}

static void mtk_dsi_start(struct mtk_dsi *dsi)
{
	writel(0, dsi->regs + DSI_START);
	writel(1, dsi->regs + DSI_START);
}

static void mtk_dsi_stop(struct mtk_dsi *dsi)
{
	writel(0, dsi->regs + DSI_START);
}

static void mtk_dsi_set_cmd_mode(struct mtk_dsi *dsi)
{
	writel(CMD_MODE, dsi->regs + DSI_MODE_CTRL);
}

static void mtk_dsi_set_interrupt_enable(struct mtk_dsi *dsi)
{
	u32 inten = LPRX_RD_RDY_INT_FLAG | CMD_DONE_INT_FLAG | VM_DONE_INT_FLAG;

	writel(inten, dsi->regs + DSI_INTEN);
}

static void mtk_dsi_irq_data_set(struct mtk_dsi *dsi, u32 irq_bit)
{
	dsi->irq_data |= irq_bit;
}

static void mtk_dsi_irq_data_clear(struct mtk_dsi *dsi, u32 irq_bit)
{
	dsi->irq_data &= ~irq_bit;
}

static s32 mtk_dsi_wait_for_irq_done(struct mtk_dsi *dsi, u32 irq_flag,
				     unsigned int timeout)
{
	s32 ret = 0;
	unsigned long jiffies = msecs_to_jiffies(timeout);

	ret = wait_event_interruptible_timeout(dsi->irq_wait_queue,
					       dsi->irq_data & irq_flag,
					       jiffies);
	if (ret == 0) {
		DRM_WARN("Wait DSI IRQ(0x%08x) Timeout\n", irq_flag);

		mtk_dsi_enable(dsi);
		mtk_dsi_reset_engine(dsi);
	}

	return ret;
}

static irqreturn_t mtk_dsi_irq(int irq, void *dev_id)
{
	struct mtk_dsi *dsi = dev_id;
	u32 status, tmp;
	u32 flag = LPRX_RD_RDY_INT_FLAG | CMD_DONE_INT_FLAG | VM_DONE_INT_FLAG;

	status = readl(dsi->regs + DSI_INTSTA) & flag;

	if (status) {
		/*
		 * Z1: this used to be an unbounded do/while on DSI_BUSY inside a hard
		 * IRQ.  With the engine left in the bootloader's video mode BUSY can
		 * stay latched forever, which would hang the CPU with interrupts off.
		 * Bound it.
		 */
		int i;

		for (i = 0; i < 200; i++) {
			mtk_dsi_mask(dsi, DSI_RACK, RACK, RACK);
			tmp = readl(dsi->regs + DSI_INTSTA);
			if (!(tmp & DSI_BUSY))
				break;
			udelay(10);
		}
		if (i == 200)
			dev_warn_ratelimited(dsi->host.dev,
					     "Z1DSI: RACK stuck, INTSTA=%08x\n", tmp);

		mtk_dsi_mask(dsi, DSI_INTSTA, status, 0);
		mtk_dsi_irq_data_set(dsi, status);
		wake_up_interruptible(&dsi->irq_wait_queue);
	}

	return IRQ_HANDLED;
}

static s32 mtk_dsi_switch_to_cmd_mode(struct mtk_dsi *dsi, u8 irq_flag, u32 t)
{
	struct device *dev = dsi->host.dev;
	u32 before, after;

	before = readl(dsi->regs + DSI_MODE_CTRL);
	mtk_dsi_irq_data_clear(dsi, irq_flag);
	mtk_dsi_set_cmd_mode(dsi);
	/*
	 * Z1: LK hands us a DSI that has been streaming BURST video mode, so
	 * DSI_BUSY (INTSTA bit31) stays latched and no DCS command ever
	 * completes.  Cycle the engine through reset (EN off -> RESET -> EN on)
	 * before using the command channel, which is what the vendor
	 * dsi_drv.c does before switching modes.
	 */
	mtk_dsi_disable(dsi);
	mtk_dsi_mask(dsi, DSI_CON_CTRL, DSI_RESET, DSI_RESET);
	udelay(20);
	mtk_dsi_mask(dsi, DSI_CON_CTRL, DSI_RESET, 0);
	mtk_dsi_enable(dsi);
	after = readl(dsi->regs + DSI_MODE_CTRL);

	/*
	 * Z1 (route A): the vendor dsi_drv.c does not wait for VM_DONE here.
	 * It simply polls DSI_INTSTA.BUSY (bit31) until the engine is idle and
	 * resets it if it stays busy.  Waiting for the VM_DONE interrupt is
	 * what made every DCS transfer fail with -ETIME on this IP.
	 */
	{
		int i;
		for (i = 0; i < 200; i++) {
			if (!(readl(dsi->regs + DSI_INTSTA) & DSI_BUSY)) {
				writel(0, dsi->regs + DSI_INTSTA);
				return 0;
			}
			usleep_range(1000, 2000);
		}
	}

	/* Z1: if the write to MODE_CTRL does not stick, this DSI IP does not
	 * use the MT2701 register map and nothing we program will ever reach
	 * the hardware. */

	if (!mtk_dsi_wait_for_irq_done(dsi, irq_flag, t)) {
		DRM_ERROR("failed to switch cmd mode\n");
		return -ETIME;
	} else {
		return 0;
	}
}

/* ------------------------------------------------------------------ *
 * Z1: MT6572 MIPI-TX (D-PHY + PLL)
 *
 * The generic phy-mtk-mipi-dsi driver programs the MT2701 register map,
 * whose PLL block sits at different offsets than this SoC's.  Measured on
 * the board: lane registers (0x00..0x10) get programmed, everything from
 * 0x14 up stays 0 -- and the PLL never produces a clock, so the DSI engine
 * stays BUSY forever and no DCS command completes.
 *
 * The sequence below is transcribed from the MediaTek vendor kernel for the
 * same DSI IP (anthropic: mt6582 dsi_drv.c:DSI_PHY_clk_setting()), with the
 * register map from dsi_reg.h: lanes 0x00..0x14, TOP_CON 0x40, BG_CON 0x44,
 * PLL_CON0..3 0x50..0x5c, PLL_CHG 0x60, PLL_TOP 0x64, PLL_PWR 0x68.
 * ------------------------------------------------------------------ */
#define MT6572_MIPI_TX_BASE	0x10011000

#define MTX_DSI0_CON		0x00
#define MTX_CLOCK_LANE		0x04
#define MTX_DATA_LANE0		0x08
#define MTX_TOP_CON		0x40
#define MTX_BG_CON		0x44
#define MTX_PLL_CON0		0x50
#define MTX_PLL_CON1		0x54
#define MTX_PLL_CON2		0x58
#define MTX_PLL_CON3		0x5c
#define MTX_PLL_TOP		0x64
#define MTX_PLL_PWR		0x68

static void mtx_set(void __iomem *tx, u32 off, u32 mask, u32 val)
{
	u32 v = readl(tx + off);

	v = (v & ~mask) | ((val << __ffs(mask)) & mask);
	writel(v, tx + off);
}

static void mtk_dsi_mt6572_mipi_tx_init(struct mtk_dsi *dsi)
{
	void __iomem *tx = ioremap(MT6572_MIPI_TX_BASE, 0x100);
	u64 data_rate_mbps = div_u64(dsi->data_rate, 1000000);
	u32 txdiv, pcw, frac;

	if (!tx) {
		dev_err(dsi->host.dev, "Z1DSI: cannot map MIPI TX\n");
		return;
	}

	/* bias / impedance */
	mtx_set(tx, MTX_TOP_CON, GENMASK(7, 4), 8);	/* LNT_IMP_CAL_CODE */
	mtx_set(tx, MTX_TOP_CON, BIT(1), 1);		/* LNT_HS_BIAS_EN */

	/* bandgap */
	mtx_set(tx, MTX_BG_CON, GENMASK(19, 17), 4);	/* V032_SEL */
	mtx_set(tx, MTX_BG_CON, GENMASK(16, 14), 4);	/* V04_SEL */
	mtx_set(tx, MTX_BG_CON, GENMASK(13, 11), 4);	/* V072_SEL */
	mtx_set(tx, MTX_BG_CON, GENMASK(10, 8), 4);	/* V10_SEL */
	mtx_set(tx, MTX_BG_CON, GENMASK(7, 5), 4);	/* V12_SEL */
	mtx_set(tx, MTX_BG_CON, BIT(1), 1);		/* BG_CKEN */
	mtx_set(tx, MTX_BG_CON, BIT(0), 1);		/* BG_CORE_EN */
	mdelay(10);

	/* LDO */
	mtx_set(tx, MTX_DSI0_CON, BIT(1), 1);		/* CKG_LDOOUT_EN */
	mtx_set(tx, MTX_DSI0_CON, BIT(0), 1);		/* LDOCORE_EN */

	/* PLL power */
	mtx_set(tx, MTX_PLL_PWR, BIT(0), 1);		/* SDM_PWR_ON */
	mtx_set(tx, MTX_PLL_PWR, BIT(1), 1);		/* SDM_ISO_EN */
	mdelay(10);
	mtx_set(tx, MTX_PLL_PWR, BIT(1), 0);

	mtx_set(tx, MTX_PLL_CON0, GENMASK(2, 1), 0);	/* PREDIV */
	mtx_set(tx, MTX_PLL_CON0, GENMASK(9, 7), 0);	/* POSDIV */

	/* txdiv: same table as the vendor driver (data rate in Mbps) */
	if (data_rate_mbps >= 500)
		txdiv = 1;
	else if (data_rate_mbps >= 250)
		txdiv = 2;
	else if (data_rate_mbps >= 125)
		txdiv = 4;
	else if (data_rate_mbps > 62)
		txdiv = 8;
	else if (data_rate_mbps >= 50)
		txdiv = 16;
	else
		txdiv = 16;

	switch (txdiv) {
	case 1: mtx_set(tx, MTX_PLL_CON0, GENMASK(6, 3), 0); break;
	case 2: mtx_set(tx, MTX_PLL_CON0, GENMASK(6, 3), 0x8); break;
	case 4: mtx_set(tx, MTX_PLL_CON0, GENMASK(6, 3), 0x10); break;
	case 8: mtx_set(tx, MTX_PLL_CON0, GENMASK(6, 3), 0x14); break;
	case 16: mtx_set(tx, MTX_PLL_CON0, GENMASK(6, 3), 0x18); break;
	}

	/* PCW = data_rate * txdiv / 13 */
	{
		u32 prod = (u32)data_rate_mbps * txdiv;
		pcw = prod / 13;
		frac = prod % 13;
		mtx_set(tx, MTX_PLL_CON2, GENMASK(30, 24), pcw & 0x7f);
		mtx_set(tx, MTX_PLL_CON2, GENMASK(23, 16), (256 * frac / 13) & 0xff);
		mtx_set(tx, MTX_PLL_CON2, GENMASK(15, 8),
			(256 * ((256 * frac) % 13) / 13) & 0xff);
		mtx_set(tx, MTX_PLL_CON2, GENMASK(7, 0),
			(256 * ((256 * ((256 * frac) % 13)) % 13) / 13) & 0xff);
	}

	mtx_set(tx, MTX_PLL_CON1, BIT(0), 1);		/* FRA_EN */

	/* clock lane + data lane 0 */
	mtx_set(tx, MTX_CLOCK_LANE, GENMASK(11, 8), 8);	/* RT_CODE */
	mtx_set(tx, MTX_CLOCK_LANE, BIT(5), 1);		/* PHI_SEL */
	mtx_set(tx, MTX_CLOCK_LANE, BIT(0), 1);		/* LDOOUT_EN */
	mtx_set(tx, MTX_DATA_LANE0, GENMASK(10, 7), 8);	/* RT_CODE */
	mtx_set(tx, MTX_DATA_LANE0, BIT(0), 1);		/* LDOOUT_EN */

	/* enable PLL */
	mtx_set(tx, MTX_PLL_CON0, BIT(0), 1);
	mdelay(1);
	mtx_set(tx, MTX_PLL_CON1, BIT(2), 0);		/* SSC_EN = 0 */
	mtx_set(tx, MTX_PLL_TOP, GENMASK(9, 8), 3);	/* PRESERVE_L */
	mtx_set(tx, MTX_TOP_CON, BIT(11), 0);		/* PAD_TIE_LOW_EN = 0 */
	mdelay(5);


	iounmap(tx);
}

/*
 * Z1 (v65) "route B": the command-queue engine on this MT6572 never retires a
 * DCS packet.  The v64 probe measured it directly -- LP and HS both time out
 * after 20 ms with DBG6 = 0x0001, which the vendor decoder table calls
 * "Idle (wait for command)": the engine accepts START (INTSTA.BUSY latches)
 * but never picks the command up.  LK has already initialised the panel and
 * is streaming video when we take over, so the only thing Linux has to do is
 * feed pixels.  With this enabled the whole DSI block -- MODE, START, PHY,
 * timing -- is left exactly as handed over, and all panel DCS traffic is
 * skipped (see panel-sitronix-st7785m.c:keep_lk_panel).
 */
static bool z1_keep_lk_dsi = true;
module_param_named(keep_lk_dsi, z1_keep_lk_dsi, bool, 0644);

static void mtk_dsi_lane_ready(struct mtk_dsi *dsi);

static int mtk_dsi_poweron(struct mtk_dsi *dsi)
{
	struct device *dev = dsi->host.dev;
	int ret;
	u32 bit_per_pixel;

	if (++dsi->refcount != 1)
		return 0;

	ret = mipi_dsi_pixel_format_to_bpp(dsi->format);
	if (ret < 0) {
		dev_err(dev, "Unknown MIPI DSI format %d\n", dsi->format);
		return ret;
	}
	bit_per_pixel = ret;

	dsi->data_rate = DIV_ROUND_UP_ULL(dsi->vm.pixelclock * bit_per_pixel,
					  dsi->lanes);

	if (!dsi->driver_data->mt6572_mipi_tx) {
		ret = clk_set_rate(dsi->hs_clk, dsi->data_rate);
		if (ret < 0) {
			dev_err(dev, "Failed to set data rate: %d\n", ret);
			goto err_refcount;
		}
	}

	ret = clk_prepare_enable(dsi->engine_clk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable engine clock: %d\n", ret);
		goto err_phy_power_off;
	}

	ret = clk_prepare_enable(dsi->digital_clk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable digital clock: %d\n", ret);
		goto err_disable_engine_clk;
	}

	/*
	 * Z1 (v55): keep whatever LK programmed into the MIPI-TX block.  LK is
	 * demonstrably driving this panel (it is streaming video at boot), and
	 * our DT only describes 0x90 bytes of the block while LK also writes
	 * 0x10011848/4c/50 -- i.e. this SoC's PHY spans a page our map does not
	 * cover.  Touching it from Linux has only ever left the PHY without a
	 * usable clock.  Dump it instead, so the next iteration has the values.
	 */
	if (dsi->driver_data->mt6572_mipi_tx) {
		void __iomem *tx = ioremap(MT6572_MIPI_TX_BASE, 0x1000);

		if (tx) {
			iounmap(tx);
		}
	}

	/*
	 * Z1 (v62): the bootloader hands us a DSI that is *already scanning
	 * video* with DSI_START bit0 = 1 and MODE_CTRL = BURST (3).  The vendor
	 * boot flow is the opposite: DSI_Init() leaves the engine in command
	 * mode (the IP resets MODE_CTRL to 0), the LCM init sequence goes out
	 * through the command queue, and only afterwards does the LCM layer call
	 * DSI_SetMode(video) + DSI_Start().  Vendor DSI_SetMode() is a bare
	 * read-modify-write of the MODE field -- it does not stop a running scan
	 * -- so simply writing CMD_MODE over LK's video stream leaves the engine
	 * walking frames, INTSTA.BUSY (bit31) latched, and every command-queue
	 * transaction times out.  Stop the engine first, then switch, then
	 * re-enable -- the same order vendor DSI_handle_int_TE() uses for
	 * VDO -> CMD (clk_hs_mode(0), COM_CTRL.RESET, DPI_DisableClk, SetMode).
	 */

	/*
	 * Z1 (v63): vendor VDO -> CMD sequence (dsi_drv.c:2410-2432) starts
	 * with DSI_clk_HS_mode(0) -- the clock lane must leave continuous HS
	 * before the engine can emit an HS command burst.  LK left us with
	 * LC_HS_TX_EN = 1 because it was streaming video, which is why the
	 * first DCS write latched INTSTA.BUSY forever instead of completing.
	 */
	if (z1_keep_lk_dsi) {
		dev_info(dev, "Z1DSI: route B -- keeping LK video stream; no DSI reconfiguration from Linux\n");
		return 0;
	}

	mtk_dsi_clk_hs_mode(dsi, 0);
	mtk_dsi_stop(dsi);
	mtk_dsi_set_cmd_mode(dsi);
	mtk_dsi_enable(dsi);

	if (dsi->driver_data->has_shadow_ctl)
		writel(FORCE_COMMIT | BYPASS_SHADOW,
		       dsi->regs + dsi->driver_data->reg_shadow_dbg_off);

	mtk_dsi_reset_engine(dsi);
	/* vendor DSI_Init(): DSI_MEM_CONTI = 0x3C (write continuous) */
	writel(0x3C, dsi->regs + DSI_MEM_CONTI);

	/* Z1 (v63): clear any latched status (incl. BUSY bit31) left by LK's
	 * video stream, then drive the D-PHY lanes to LP-11 so the command
	 * queue can emit an HS/LP burst at all.
	 */
	writel(0, dsi->regs + DSI_INTSTA);
	mtk_dsi_lane_ready(dsi);
	mtk_dsi_phy_timconfig(dsi);

	mtk_dsi_ps_control(dsi, true);
	mtk_dsi_set_vm_cmd(dsi);
	mtk_dsi_config_vdo_timing(dsi);
	mtk_dsi_set_interrupt_enable(dsi);


	return 0;
err_disable_engine_clk:
	clk_disable_unprepare(dsi->engine_clk);
err_phy_power_off:
	phy_power_off(dsi->phy);
err_refcount:
	dsi->refcount--;
	return ret;
}

static void mtk_dsi_poweroff(struct mtk_dsi *dsi)
{
	if (WARN_ON(dsi->refcount == 0))
		return;

	if (--dsi->refcount != 0)
		return;

	if (z1_keep_lk_dsi)
		return;

	/*
	 * mtk_dsi_stop() and mtk_dsi_start() is asymmetric, since
	 * mtk_dsi_stop() should be called after mtk_crtc_atomic_disable(),
	 * which needs irq for vblank, and mtk_dsi_stop() will disable irq.
	 * mtk_dsi_start() needs to be called in mtk_output_dsi_enable(),
	 * after dsi is fully set.
	 */
	mtk_dsi_stop(dsi);

	mtk_dsi_switch_to_cmd_mode(dsi, VM_DONE_INT_FLAG, 500);
	mtk_dsi_reset_engine(dsi);
	mtk_dsi_lane0_ulp_mode_enter(dsi);
	mtk_dsi_clk_ulp_mode_enter(dsi);
	/* set the lane number as 0 to pull down mipi */
	writel(0, dsi->regs + DSI_TXRX_CTRL);

	mtk_dsi_disable(dsi);

	clk_disable_unprepare(dsi->engine_clk);
	clk_disable_unprepare(dsi->digital_clk);

	phy_power_off(dsi->phy);

	dsi->lanes_ready = false;
}

static void mtk_dsi_lane_ready(struct mtk_dsi *dsi)
{
	if (!dsi->lanes_ready) {
		dsi->lanes_ready = true;
		mtk_dsi_rxtx_control(dsi);
		usleep_range(30, 100);
		mtk_dsi_reset_dphy(dsi);
		mtk_dsi_clk_ulp_mode_leave(dsi);
		mtk_dsi_lane0_ulp_mode_leave(dsi);
		mtk_dsi_clk_hs_mode(dsi, 0);
		usleep_range(1000, 3000);
		/* The reaction time after pulling up the mipi signal for dsi_rx */
	}
}

static void mtk_output_dsi_enable(struct mtk_dsi *dsi)
{
	if (dsi->enabled)
		return;

	if (z1_keep_lk_dsi) {
		dev_info(dsi->host.dev,
			 "Z1DSI: route B -- DSI/PHY/timing left as LK configured them (MODE=%08x START=%08x)\n",
			 readl(dsi->regs + DSI_MODE_CTRL), readl(dsi->regs + DSI_START));
		dsi->enabled = true;
		return;
	}

	mtk_dsi_lane_ready(dsi);
	mtk_dsi_set_mode(dsi);
	mtk_dsi_clk_hs_mode(dsi, 1);

	mtk_dsi_start(dsi);

	dsi->enabled = true;
}

static void mtk_output_dsi_disable(struct mtk_dsi *dsi)
{
	if (!dsi->enabled)
		return;

	if (z1_keep_lk_dsi) {
		dsi->enabled = false;
		return;
	}

	dsi->enabled = false;
}

static int mtk_dsi_bridge_attach(struct drm_bridge *bridge,
				 struct drm_encoder *encoder,
				 enum drm_bridge_attach_flags flags)
{
	struct mtk_dsi *dsi = bridge_to_dsi(bridge);

	/* Attach the panel or bridge to the dsi bridge */
	return drm_bridge_attach(encoder, dsi->next_bridge,
				 &dsi->bridge, flags);
}

static void mtk_dsi_bridge_mode_set(struct drm_bridge *bridge,
				    const struct drm_display_mode *mode,
				    const struct drm_display_mode *adjusted)
{
	struct mtk_dsi *dsi = bridge_to_dsi(bridge);

	drm_display_mode_to_videomode(adjusted, &dsi->vm);
}

static void mtk_dsi_bridge_atomic_disable(struct drm_bridge *bridge,
					  struct drm_atomic_state *state)
{
	struct mtk_dsi *dsi = bridge_to_dsi(bridge);

	mtk_output_dsi_disable(dsi);
}

static void mtk_dsi_bridge_atomic_enable(struct drm_bridge *bridge,
					 struct drm_atomic_state *state)
{
	struct mtk_dsi *dsi = bridge_to_dsi(bridge);

	if (dsi->refcount == 0)
		return;

	mtk_output_dsi_enable(dsi);
}

static void mtk_dsi_bridge_atomic_pre_enable(struct drm_bridge *bridge,
					     struct drm_atomic_state *state)
{
	struct mtk_dsi *dsi = bridge_to_dsi(bridge);
	int ret;

	ret = mtk_dsi_poweron(dsi);
	if (ret < 0)
		DRM_ERROR("failed to power on dsi\n");
}

static void mtk_dsi_bridge_atomic_post_disable(struct drm_bridge *bridge,
					       struct drm_atomic_state *state)
{
	struct mtk_dsi *dsi = bridge_to_dsi(bridge);

	mtk_dsi_poweroff(dsi);
}

static enum drm_mode_status
mtk_dsi_bridge_mode_valid(struct drm_bridge *bridge,
			  const struct drm_display_info *info,
			  const struct drm_display_mode *mode)
{
	struct mtk_dsi *dsi = bridge_to_dsi(bridge);
	int bpp;

	bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);
	if (bpp < 0)
		return MODE_ERROR;

	if (mode->clock * bpp / dsi->lanes > 1500000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static const struct drm_bridge_funcs mtk_dsi_bridge_funcs = {
	.attach = mtk_dsi_bridge_attach,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_disable = mtk_dsi_bridge_atomic_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_enable = mtk_dsi_bridge_atomic_enable,
	.atomic_pre_enable = mtk_dsi_bridge_atomic_pre_enable,
	.atomic_post_disable = mtk_dsi_bridge_atomic_post_disable,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.mode_valid = mtk_dsi_bridge_mode_valid,
	.mode_set = mtk_dsi_bridge_mode_set,
};

void mtk_dsi_ddp_start(struct device *dev)
{
	struct mtk_dsi *dsi = dev_get_drvdata(dev);

	mtk_dsi_poweron(dsi);
}

void mtk_dsi_ddp_stop(struct device *dev)
{
	struct mtk_dsi *dsi = dev_get_drvdata(dev);

	mtk_dsi_poweroff(dsi);
}

static int mtk_dsi_encoder_init(struct drm_device *drm, struct mtk_dsi *dsi)
{
	int ret;

	ret = drm_simple_encoder_init(drm, &dsi->encoder,
				      DRM_MODE_ENCODER_DSI);
	if (ret) {
		DRM_ERROR("Failed to encoder init to drm\n");
		return ret;
	}

	ret = mtk_find_possible_crtcs(drm, dsi->host.dev);
	if (ret < 0)
		goto err_cleanup_encoder;
	dsi->encoder.possible_crtcs = ret;

	ret = drm_bridge_attach(&dsi->encoder, &dsi->bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret)
		goto err_cleanup_encoder;

	dsi->connector = drm_bridge_connector_init(drm, &dsi->encoder);
	if (IS_ERR(dsi->connector)) {
		DRM_ERROR("Unable to create bridge connector\n");
		ret = PTR_ERR(dsi->connector);
		goto err_cleanup_encoder;
	}
	drm_connector_attach_encoder(dsi->connector, &dsi->encoder);

	return 0;

err_cleanup_encoder:
	drm_encoder_cleanup(&dsi->encoder);
	return ret;
}

unsigned int mtk_dsi_encoder_index(struct device *dev)
{
	struct mtk_dsi *dsi = dev_get_drvdata(dev);
	unsigned int encoder_index = drm_encoder_index(&dsi->encoder);

	dev_dbg(dev, "encoder index:%d\n", encoder_index);
	return encoder_index;
}

static int mtk_dsi_bind(struct device *dev, struct device *master, void *data)
{
	int ret;
	struct drm_device *drm = data;
	struct mtk_dsi *dsi = dev_get_drvdata(dev);

	ret = mtk_dsi_encoder_init(drm, dsi);
	if (ret)
		return ret;

	return device_reset_optional(dev);
}

static void mtk_dsi_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct mtk_dsi *dsi = dev_get_drvdata(dev);

	drm_encoder_cleanup(&dsi->encoder);
}

static const struct component_ops mtk_dsi_component_ops = {
	.bind = mtk_dsi_bind,
	.unbind = mtk_dsi_unbind,
};

static int mtk_dsi_host_attach(struct mipi_dsi_host *host,
			       struct mipi_dsi_device *device)
{
	struct mtk_dsi *dsi = host_to_dsi(host);
	struct device *dev = host->dev;
	int ret;

	dsi->lanes = device->lanes;
	dsi->format = device->format;
	dsi->mode_flags = device->mode_flags;
	dsi->next_bridge = devm_drm_of_get_bridge(dev, dev->of_node, 1, 0);
	if (IS_ERR(dsi->next_bridge)) {
		ret = PTR_ERR(dsi->next_bridge);
		/* Z1 debug: which lookup failed and with what */
		if (ret == -EPROBE_DEFER)
			return ret;

		/* Old devicetree has only one endpoint */
		dsi->next_bridge = devm_drm_of_get_bridge(dev, dev->of_node, 0, 0);
		if (IS_ERR(dsi->next_bridge)) {
			ret = PTR_ERR(dsi->next_bridge);
			return ret;
		}
	}

	drm_bridge_add(&dsi->bridge);

	ret = component_add(host->dev, &mtk_dsi_component_ops);
	if (ret) {
		DRM_ERROR("failed to add dsi_host component: %d\n", ret);
		drm_bridge_remove(&dsi->bridge);
		return ret;
	}

	return 0;
}

static int mtk_dsi_host_detach(struct mipi_dsi_host *host,
			       struct mipi_dsi_device *device)
{
	struct mtk_dsi *dsi = host_to_dsi(host);

	component_del(host->dev, &mtk_dsi_component_ops);
	drm_bridge_remove(&dsi->bridge);
	return 0;
}

static void mtk_dsi_wait_for_idle(struct mtk_dsi *dsi)
{
	int ret;
	u32 val;
	u32 mode = readl(dsi->regs + DSI_MODE_CTRL);

	/*
	 * Z1 (v58): in video mode INTSTA.BUSY is *always* set, so polling it
	 * here can only ever time out.  The vendor driver and this board's own
	 * bootloader both skip the wait entirely when MODE != CMD_MODE
	 * (dsi_drv.c:475 "if (DSI_REG->DSI_MODE_CTRL.MODE) return;";
	 * lk.bin 0xFA0C branches straight out for video mode).
	 */
	if (mode & MODE)
		return;

	ret = readl_poll_timeout(dsi->regs + DSI_INTSTA, val, !(val & DSI_BUSY),
				 4, 2000000);
	if (ret) {
		DRM_WARN("polling dsi wait not busy timeout!\n");

		mtk_dsi_enable(dsi);
		mtk_dsi_reset_engine(dsi);
	}
}

static u32 mtk_dsi_recv_cnt(u8 type, u8 *read_data)
{
	switch (type) {
	case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_1BYTE:
	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_1BYTE:
		return 1;
	case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_2BYTE:
	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_2BYTE:
		return 2;
	case MIPI_DSI_RX_GENERIC_LONG_READ_RESPONSE:
	case MIPI_DSI_RX_DCS_LONG_READ_RESPONSE:
		return read_data[1] + read_data[2] * 16;
	case MIPI_DSI_RX_ACKNOWLEDGE_AND_ERROR_REPORT:
		DRM_INFO("type is 0x02, try again\n");
		break;
	default:
		DRM_INFO("type(0x%x) not recognized\n", type);
		break;
	}

	return 0;
}

static void mtk_dsi_cmdq(struct mtk_dsi *dsi, const struct mipi_dsi_msg *msg)
{
	const char *tx_buf = msg->tx_buf;
	u8 config, cmdq_size, cmdq_off, type = msg->type;
	u32 reg_val, cmdq_mask, i;
	u32 reg_cmdq_off = dsi->driver_data->reg_cmdq_off;

	if (MTK_DSI_HOST_IS_READ(type))
		config = BTA;
	else
		config = (msg->tx_len > 2) ? LONG_PACKET : SHORT_PACKET;

	if (!(msg->flags & MIPI_DSI_MSG_USE_LPM))
		config |= HSTX;

	if (msg->tx_len > 2) {
		cmdq_size = 1 + (msg->tx_len + 3) / 4;
		cmdq_off = 4;
		cmdq_mask = CONFIG | DATA_ID | DATA_0 | DATA_1;
		reg_val = (msg->tx_len << 16) | (type << 8) | config;
	} else {
		cmdq_size = 1;
		cmdq_off = 2;
		cmdq_mask = CONFIG | DATA_ID;
		reg_val = (type << 8) | config;
	}

	for (i = 0; i < msg->tx_len; i++)
		mtk_dsi_mask(dsi, (reg_cmdq_off + cmdq_off + i) & (~0x3U),
			     (0xffUL << (((i + cmdq_off) & 3U) * 8U)),
			     tx_buf[i] << (((i + cmdq_off) & 3U) * 8U));

	mtk_dsi_mask(dsi, reg_cmdq_off, cmdq_mask, reg_val);
	mtk_dsi_mask(dsi, DSI_CMDQ_SIZE, CMDQ_SIZE, cmdq_size);
	if (dsi->driver_data->cmdq_long_packet_ctl) {
		/* Disable setting cmdq_size automatically for long packets */
		mtk_dsi_mask(dsi, DSI_CMDQ_SIZE, CMDQ_SIZE_SEL, CMDQ_SIZE_SEL);
	}
}

/*
 * Z1: DCS command injection while the DSI is still in video mode.
 *
 * The engine is streaming video (INTSTA.BUSY is *permanently* set in video
 * mode -- the vendor source says so explicitly) and switching to command mode
 * only takes effect at a frame boundary that never comes once the bootloader's
 * pixel feed stops.  The vendor driver therefore does not switch modes at all
 * in this situation: it injects the DCS packet through the video-mode command
 * channel (DSI_VM_CMD_CON 0x130 + payload at 0x134, started with START.bit16)
 * and waits for INTSTA bit5 (VM_CMD_DONE).
 */
static int mtk_dsi_vm_cmd_write(struct mtk_dsi *dsi,
				const struct mipi_dsi_msg *msg)
{
	u32 off = (u32)dsi->driver_data->reg_vm_cmd_off;
	u32 con = readl(dsi->regs + off);
	const u8 *tx = msg->tx_buf;
	unsigned int tx_len = msg->tx_len;
	unsigned int i;

	/* payload window is 16 bytes (0x134..0x143) */
	if (!tx || tx_len < 1 || tx_len > 16)
		return -EINVAL;

	con &= ~(VM_CMD_LONG_PKT | GENMASK(15, 8) | GENMASK(23, 16) | GENMASK(31, 24));
	con |= VM_CMD_EN | TS_VFP_EN;

	if (tx_len > 2) {
		/* long write: WC counts the command byte itself */
		con |= VM_CMD_LONG_PKT;
		con |= (MIPI_DSI_DCS_LONG_WRITE << VM_CMD_CM_DATA_ID_SHIFT);
		con |= (tx_len << VM_CMD_CM_DATA_0_SHIFT);
		writel(con, dsi->regs + off);
		for (i = 0; i < tx_len; i++)
			writeb(tx[i], dsi->regs + DSI_VM_CMD_DATA_OFF(off) + i);
	} else if (tx_len == 2) {
		con |= (MIPI_DSI_DCS_SHORT_WRITE_PARAM << VM_CMD_CM_DATA_ID_SHIFT);
		con |= (tx[0] << VM_CMD_CM_DATA_0_SHIFT);
		con |= (tx[1] << VM_CMD_CM_DATA_1_SHIFT);
		writel(con, dsi->regs + off);
	} else {
		con |= (MIPI_DSI_DCS_SHORT_WRITE << VM_CMD_CM_DATA_ID_SHIFT);
		con |= (tx[0] << VM_CMD_CM_DATA_0_SHIFT);
		writel(con, dsi->regs + off);
	}

	/* vendor LK: clear VM_CMD_START then set it, so the hardware sees 0->1 */
	mtk_dsi_mask(dsi, DSI_START, DSI_START_VM_CMD_START, 0);
	mtk_dsi_mask(dsi, DSI_START, DSI_START_VM_CMD_START,
		     DSI_START_VM_CMD_START);

	/* neither the vendor driver nor this board's LK waits for completion */
	usleep_range(1000, 2000);

	return 0;
}

static ssize_t mtk_dsi_host_send_cmd(struct mtk_dsi *dsi,
				     const struct mipi_dsi_msg *msg, u8 flag)
{
	int i;

	mtk_dsi_wait_for_idle(dsi);
	mtk_dsi_irq_data_clear(dsi, flag);
	mtk_dsi_cmdq(dsi, msg);
	mtk_dsi_start(dsi);

	/*
	 * Z1 (route A): this DSI IP is the MT6572/MT6582 generation and does
	 * not raise CMD_DONE for us the way newer SoCs do (measured: INTSTA
	 * stays BUSY, the IRQ never arrives).  The vendor dsi_drv.c polls
	 * DSI_INTSTA instead of waiting for an interrupt, clears CMD_DONE by
	 * writing the status register to 0, and resets the engine when it
	 * stays busy.  Do the same here.
	 */
	/*
	 * Z1: the vendor dsi_drv.c does not wait for CMD_DONE when it sends a
	 * HS DCS write -- it polls DSI_INTSTA.BUSY until the engine goes idle
	 * and then clears the whole status register:
	 *
	 *     OUTREG32(&DSI_CMDQ_REG->data[0], cmd);
	 *     OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);
	 *     START 0 -> 1;
	 *     while (DSI_REG->DSI_INTSTA.BUSY) { if (>50ms) DSI_Reset(); }
	 *     OUTREG32(&DSI_REG->DSI_INTSTA, 0x0);
	 *
	 * Waiting for CMD_DONE is what kept timing out on this IP.
	 */
	for (i = 0; i < 100; i++) {
		if (!(readl(dsi->regs + DSI_INTSTA) & DSI_BUSY)) {
			writel(0, dsi->regs + DSI_INTSTA);
			return 0;
		}
		usleep_range(500, 1000);
	}

	mtk_dsi_mask(dsi, DSI_CON_CTRL, DSI_RESET, DSI_RESET);
	udelay(20);
	mtk_dsi_mask(dsi, DSI_CON_CTRL, DSI_RESET, 0);
	mtk_dsi_enable(dsi);

	return -ETIME;
}

static ssize_t mtk_dsi_host_transfer(struct mipi_dsi_host *host,
				     const struct mipi_dsi_msg *msg)
{
	struct mtk_dsi *dsi = host_to_dsi(host);
	ssize_t recv_cnt;
	u8 read_data[16];
	void *src_addr;
	u8 irq_flag = CMD_DONE_INT_FLAG;
	u32 dsi_mode;
	int ret, i;

	dsi_mode = readl(dsi->regs + DSI_MODE_CTRL);
	if (dsi_mode & MODE) {
		/*
		 * Z1: video mode is still streaming (bootloader handover).  Do not
		 * try to switch to command mode -- that stalls on a frame boundary
		 * that never arrives.  Inject through the VM_CMD channel instead.
		 */
		ret = mtk_dsi_vm_cmd_write(dsi, msg);
		if (!ret) {
			/* read requests still need the command path */
			if (!MTK_DSI_HOST_IS_READ(msg->type))
				return 0;
		} else {
			mtk_dsi_stop(dsi);
			ret = mtk_dsi_switch_to_cmd_mode(dsi, VM_DONE_INT_FLAG, 500);
			if (ret)
				goto restore_dsi_mode;
		}
	}

	if (MTK_DSI_HOST_IS_READ(msg->type))
		irq_flag |= LPRX_RD_RDY_INT_FLAG;

	mtk_dsi_lane_ready(dsi);

	ret = mtk_dsi_host_send_cmd(dsi, msg, irq_flag);
	if (ret)
		goto restore_dsi_mode;

	if (!MTK_DSI_HOST_IS_READ(msg->type)) {
		recv_cnt = 0;
		goto restore_dsi_mode;
	}

	if (!msg->rx_buf) {
		DRM_ERROR("dsi receive buffer size may be NULL\n");
		ret = -EINVAL;
		goto restore_dsi_mode;
	}

	for (i = 0; i < 16; i++)
		*(read_data + i) = readb(dsi->regs + DSI_RX_DATA0 + i);

	recv_cnt = mtk_dsi_recv_cnt(read_data[0], read_data);

	if (recv_cnt > 2)
		src_addr = &read_data[4];
	else
		src_addr = &read_data[1];

	if (recv_cnt > 10)
		recv_cnt = 10;

	if (recv_cnt > msg->rx_len)
		recv_cnt = msg->rx_len;

	if (recv_cnt)
		memcpy(msg->rx_buf, src_addr, recv_cnt);

	DRM_INFO("dsi get %zd byte data from the panel address(0x%x)\n",
		 recv_cnt, *((u8 *)(msg->tx_buf)));

restore_dsi_mode:
	if (dsi_mode & MODE) {
		mtk_dsi_set_mode(dsi);
		mtk_dsi_start(dsi);
	}

	return ret < 0 ? ret : recv_cnt;
}

static const struct mipi_dsi_host_ops mtk_dsi_ops = {
	.attach = mtk_dsi_host_attach,
	.detach = mtk_dsi_host_detach,
	.transfer = mtk_dsi_host_transfer,
};

static int mtk_dsi_probe(struct platform_device *pdev)
{
	struct mtk_dsi *dsi;
	struct device *dev = &pdev->dev;
	int irq_num;
	int ret;

	dsi = devm_drm_bridge_alloc(dev, struct mtk_dsi, bridge,
				    &mtk_dsi_bridge_funcs);
	if (IS_ERR(dsi))
		return PTR_ERR(dsi);

	dsi->driver_data = of_device_get_match_data(dev);

	dsi->engine_clk = devm_clk_get(dev, "engine");
	if (IS_ERR(dsi->engine_clk))
		return dev_err_probe(dev, PTR_ERR(dsi->engine_clk),
				     "Failed to get engine clock\n");


	dsi->digital_clk = devm_clk_get(dev, "digital");
	if (IS_ERR(dsi->digital_clk))
		return dev_err_probe(dev, PTR_ERR(dsi->digital_clk),
				     "Failed to get digital clock\n");

	dsi->hs_clk = devm_clk_get(dev, "hs");
	if (IS_ERR(dsi->hs_clk))
		return dev_err_probe(dev, PTR_ERR(dsi->hs_clk), "Failed to get hs clock\n");

	dsi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dsi->regs))
		return dev_err_probe(dev, PTR_ERR(dsi->regs), "Failed to ioremap memory\n");

	dsi->phy = devm_phy_get(dev, "dphy");
	if (IS_ERR(dsi->phy))
		return dev_err_probe(dev, PTR_ERR(dsi->phy), "Failed to get MIPI-DPHY\n");

	irq_num = platform_get_irq(pdev, 0);
	if (irq_num < 0)
		return irq_num;

	dsi->host.ops = &mtk_dsi_ops;
	dsi->host.dev = dev;

	init_waitqueue_head(&dsi->irq_wait_queue);

	platform_set_drvdata(pdev, dsi);

	ret = mipi_dsi_host_register(&dsi->host);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to register DSI host\n");

	writel(0, dsi->regs + DSI_INTSTA);
	ret = devm_request_irq(&pdev->dev, irq_num, mtk_dsi_irq,
			       IRQF_TRIGGER_NONE, dev_name(&pdev->dev), dsi);
	if (ret) {
		mipi_dsi_host_unregister(&dsi->host);
		return dev_err_probe(&pdev->dev, ret, "Failed to request DSI irq\n");
	}

	dsi->bridge.of_node = dev->of_node;
	dsi->bridge.type = DRM_MODE_CONNECTOR_DSI;

	return 0;
}

static void mtk_dsi_remove(struct platform_device *pdev)
{
	struct mtk_dsi *dsi = platform_get_drvdata(pdev);

	mtk_output_dsi_disable(dsi);
	mipi_dsi_host_unregister(&dsi->host);
}

static const struct mtk_dsi_driver_data mt8173_dsi_driver_data = {
	.reg_cmdq_off = 0x200,
	.reg_vm_cmd_off = 0x130,
	.reg_shadow_dbg_off = 0x190
};

static const struct mtk_dsi_driver_data mt2701_dsi_driver_data = {
	.reg_cmdq_off = 0x180,
	.reg_vm_cmd_off = 0x130,
	.reg_shadow_dbg_off = 0x190
};

static const struct mtk_dsi_driver_data mt6572_dsi_driver_data = {
	.reg_cmdq_off = 0x180,
	.reg_vm_cmd_off = 0x130,
	.has_shadow_ctl = false,
	.has_size_ctl = false,
	.mt6572_mipi_tx = true,
};

static const struct mtk_dsi_driver_data mt8183_dsi_driver_data = {
	.reg_cmdq_off = 0x200,
	.reg_vm_cmd_off = 0x130,
	.reg_shadow_dbg_off = 0x190,
	.has_shadow_ctl = true,
	.has_size_ctl = true,
};

static const struct mtk_dsi_driver_data mt8186_dsi_driver_data = {
	.reg_cmdq_off = 0xd00,
	.reg_vm_cmd_off = 0x200,
	.reg_shadow_dbg_off = 0xc00,
	.has_shadow_ctl = true,
	.has_size_ctl = true,
};

static const struct mtk_dsi_driver_data mt8188_dsi_driver_data = {
	.reg_cmdq_off = 0xd00,
	.reg_vm_cmd_off = 0x200,
	.reg_shadow_dbg_off = 0xc00,
	.has_shadow_ctl = true,
	.has_size_ctl = true,
	.cmdq_long_packet_ctl = true,
	.support_per_frame_lp = true,
};

static const struct of_device_id mtk_dsi_of_match[] = {
	{ .compatible = "mediatek,mt2701-dsi", .data = &mt2701_dsi_driver_data },
	{ .compatible = "mediatek,mt6572-dsi", .data = &mt6572_dsi_driver_data },
	{ .compatible = "mediatek,mt8173-dsi", .data = &mt8173_dsi_driver_data },
	{ .compatible = "mediatek,mt8183-dsi", .data = &mt8183_dsi_driver_data },
	{ .compatible = "mediatek,mt8186-dsi", .data = &mt8186_dsi_driver_data },
	{ .compatible = "mediatek,mt8188-dsi", .data = &mt8188_dsi_driver_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_dsi_of_match);

struct platform_driver mtk_dsi_driver = {
	.probe = mtk_dsi_probe,
	.remove = mtk_dsi_remove,
	.driver = {
		.name = "mtk-dsi",
		.of_match_table = mtk_dsi_of_match,
	},
};
