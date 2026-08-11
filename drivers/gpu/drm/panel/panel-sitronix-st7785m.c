// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sitronix ST7785M 240x320 QVGA MIPI-DSI panel driver
 *
 * Driver skeleton for the Z1 (MediaTek MT6572) 2.4" 240x320 1-lane MIPI DSI
 * panel based on the Sitronix ST7785M TFT controller.
 *
 * The ST7785M belongs to the same controller family as the ST7789V (command
 * set compatible), but is wired through MIPI DSI instead of SPI. The init
 * sequence below is the exact table extracted from the Z1 factory LK binary
 * (see mainline_recon/ST7785M_INIT_LK_20260810.h), with the LK 16-bit
 * 0x00 prefix stripped so that only the effective low bytes are sent.
 *
 * Reference timing measured from the Z1 LK boot log
 * (mainline_recon/z1_boot_mainline_20260731_230233.log):
 *   - 240x320, DSI video mode (mode = 2), 1 data lane
 *   - data_rate = 140MHz, txdiv = 4, fbk_div = 5
 *   - VSA = 3 / VBP = 6 / VFP = 8 / VACT = 320  =>  vtotal = 337
 *   - RGB565 (16bpp, i.e. two bytes per pixel)
 *
 * H-timing was not printed by LK and uses ST7789V-family defaults, see the
 * TODO comment on st7785m_mode.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_connector.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

/* ST7789V/ST7785M family extended commands (same command set). */
#define ST7789V_PORCTRL_CMD	0xb2	/* Porch control */
#define ST7789V_GCTRL_CMD	0xb7	/* Gate control */
#define ST7789V_VCOMS_CMD	0xbb	/* VCOM setting */
#define ST7789V_LCMCTRL_CMD	0xc0	/* LCM control */
#define ST7789V_VDVVRHEN_CMD	0xc2	/* VDV and VRH command enable */
#define ST7789V_VRHS_CMD	0xc3	/* VRH set */
#define ST7789V_VDVS_CMD	0xc4	/* VDV set */
#define ST7789V_FRCTRL2_CMD	0xc6	/* Frame rate control */
#define ST7789V_PWCTRL1_CMD	0xd0	/* Power control 1 */
#define ST7789V_PVGAMCTRL_CMD	0xe0	/* Positive voltage gamma control */
#define ST7789V_NVGAMCTRL_CMD	0xe1	/* Negative voltage gamma control */
#define ST7785M_RAMCTRL_CMD	0xb0	/* RAM control */
#define ST7785M_D6_CMD		0xd6	/* Register 0xd6 */

struct st7785m_panel_desc {
	const struct drm_display_mode *mode;
	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	unsigned long mode_flags;
	/* Extra delay (ms) to wait after MIPI_DCS_EXIT_SLEEP_MODE. */
	unsigned int sleep_delay;
};

struct st7785m {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct st7785m_panel_desc *desc;

	struct regulator *power;
	struct gpio_desc *reset;
	enum drm_panel_orientation orientation;
};

static inline struct st7785m *panel_to_st7785m(struct drm_panel *panel)
{
	return container_of(panel, struct st7785m, panel);
}

static int st7785m_dcs_write(struct st7785m *ctx, u8 cmd, const void *data,
			     size_t len)
{
	return mipi_dsi_dcs_write(ctx->dsi, cmd, data, len);
}

#define ST7785M_DCS_WRITE(ctx, cmd, data...)				\
	st7785m_dcs_write(ctx, cmd, (const u8[]){ data },		\
			  sizeof((const u8[]){ data }))

/*
 * ST7785M init sequence (22 commands) extracted from the Z1 factory LK
 * binary (mainline_recon/ST7785M_INIT_LK_20260810.h, init table at
 * lk.bin file 0x396d0). LK stores every register as (0x00, value) on the
 * DSI link; the effective low bytes are sent here. 0xFE entries are LK
 * delay markers (ms), 0xFF marks the end of the table.
 */
static int st7785m_init_sequence(struct st7785m *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	int ret;

	/* Sleep Out, then wait for the panel to stabilize (LK delay 120ms). */
	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret)
		return ret;
	msleep(120 + ctx->desc->sleep_delay);

	/* MADCTL: RGB order, portrait, scan from top-left (LK effective 0x00). */
	ret = ST7785M_DCS_WRITE(ctx, MIPI_DCS_SET_ADDRESS_MODE, 0x00);
	if (ret)
		return ret;

	/* COLMOD: pixel format 0x06 as programmed by the Z1 LK. */
	ret = ST7785M_DCS_WRITE(ctx, MIPI_DCS_SET_PIXEL_FORMAT, 0x06);
	if (ret)
		return ret;

	/* RAMCTRL. */
	ret = ST7785M_DCS_WRITE(ctx, ST7785M_RAMCTRL_CMD, 0x10);
	if (ret)
		return ret;

	/* PORCTRL: porch control (LK effective 0x0c 0x0c 0x00 0x33 0x33). */
	ret = ST7785M_DCS_WRITE(ctx, ST7789V_PORCTRL_CMD,
				0x0c, 0x0c, 0x00, 0x33, 0x33);
	if (ret)
		return ret;

	/* GCTRL: gate control VGLS(5) | VGHS(3). */
	ret = ST7785M_DCS_WRITE(ctx, ST7789V_GCTRL_CMD, 0x35);
	if (ret)
		return ret;

	/* VCOMS: VCOM setting. */
	ret = ST7785M_DCS_WRITE(ctx, ST7789V_VCOMS_CMD, 0x29);
	if (ret)
		return ret;

	/* LCMCTRL. */
	ret = ST7785M_DCS_WRITE(ctx, ST7789V_LCMCTRL_CMD, 0x2c);
	if (ret)
		return ret;

	/* VDV and VRH command enable. */
	ret = ST7785M_DCS_WRITE(ctx, ST7789V_VDVVRHEN_CMD, 0x01);
	if (ret)
		return ret;

	/* VRH set. */
	ret = ST7785M_DCS_WRITE(ctx, ST7789V_VRHS_CMD, 0x15);
	if (ret)
		return ret;

	/* VDV set. */
	ret = ST7785M_DCS_WRITE(ctx, ST7789V_VDVS_CMD, 0x20);
	if (ret)
		return ret;

	/* FRCTRL2: frame rate. */
	ret = ST7785M_DCS_WRITE(ctx, ST7789V_FRCTRL2_CMD, 0x0f);
	if (ret)
		return ret;

	/* PWCTRL1: power control. */
	ret = ST7785M_DCS_WRITE(ctx, ST7789V_PWCTRL1_CMD, 0xa4, 0xa1);
	if (ret)
		return ret;

	/* Register 0xd6. */
	ret = ST7785M_DCS_WRITE(ctx, ST7785M_D6_CMD, 0xa1);
	if (ret)
		return ret;

	/* PVGAMCTRL: positive gamma (LK effective values). */
	ret = ST7785M_DCS_WRITE(ctx, ST7789V_PVGAMCTRL_CMD,
				0xd0, 0x09, 0x0f, 0x09, 0x09, 0x16, 0x35,
				0x44, 0x4d, 0x28, 0x13, 0x13, 0x2d, 0x30);
	if (ret)
		return ret;

	/* NVGAMCTRL: negative gamma (LK effective values). */
	ret = ST7785M_DCS_WRITE(ctx, ST7789V_NVGAMCTRL_CMD,
				0xd0, 0x06, 0x0c, 0x0b, 0x09, 0x05, 0x35,
				0x33, 0x4d, 0x3a, 0x18, 0x17, 0x2d, 0x2f);
	if (ret)
		return ret;

	/* Display Inversion ON. */
	ret = st7785m_dcs_write(ctx, MIPI_DCS_ENTER_INVERT_MODE, NULL, 0);
	if (ret)
		return ret;

	/* Display ON, then a dummy Memory Write as LK does. */
	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_WRITE_MEMORY_START, NULL, 0);
	if (ret)
		return ret;
	msleep(20);

	return 0;
}

static int st7785m_prepare(struct drm_panel *panel)
{
	struct st7785m *ctx = panel_to_st7785m(panel);
	int ret;

	if (ctx->power) {
		ret = regulator_enable(ctx->power);
		if (ret)
			return ret;
	}

	if (ctx->reset) {
		/* Reset pulse (active low). */
		gpiod_set_value(ctx->reset, 0);	/* assert reset */
		msleep(10);
		gpiod_set_value(ctx->reset, 1);	/* release reset */
		msleep(120);
	}

	return st7785m_init_sequence(ctx);
}

static int st7785m_enable(struct drm_panel *panel)
{
	struct st7785m *ctx = panel_to_st7785m(panel);

	return mipi_dsi_dcs_set_display_on(ctx->dsi);
}

static int st7785m_disable(struct drm_panel *panel)
{
	struct st7785m *ctx = panel_to_st7785m(panel);

	return mipi_dsi_dcs_set_display_off(ctx->dsi);
}

static int st7785m_unprepare(struct drm_panel *panel)
{
	struct st7785m *ctx = panel_to_st7785m(panel);
	int ret;

	/* LK suspend table: Display Off, delay 120ms, Sleep In, delay 120ms. */
	ret = mipi_dsi_dcs_set_display_off(ctx->dsi);
	if (ret)
		return ret;
	msleep(120);

	ret = mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);
	if (ret)
		return ret;
	msleep(120 + ctx->desc->sleep_delay);

	if (ctx->reset)
		gpiod_set_value(ctx->reset, 0);	/* assert reset */

	if (ctx->power)
		return regulator_disable(ctx->power);

	return 0;
}

static int st7785m_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct st7785m *ctx = panel_to_st7785m(panel);
	const struct drm_display_mode *desc_mode = ctx->desc->mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, desc_mode);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			desc_mode->hdisplay, desc_mode->vdisplay,
			drm_mode_vrefresh(desc_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = desc_mode->width_mm;
	connector->display_info.height_mm = desc_mode->height_mm;
	connector->display_info.bpc = 6; /* RGB565 */

	drm_connector_set_panel_orientation(connector, ctx->orientation);

	return 1;
}

static enum drm_panel_orientation st7785m_get_orientation(struct drm_panel *panel)
{
	struct st7785m *ctx = panel_to_st7785m(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs st7785m_funcs = {
	.disable	= st7785m_disable,
	.unprepare	= st7785m_unprepare,
	.prepare	= st7785m_prepare,
	.enable		= st7785m_enable,
	.get_modes	= st7785m_get_modes,
	.get_orientation = st7785m_get_orientation,
};

static const struct drm_display_mode st7785m_mode = {
	/*
	 * V-timing matches the LK measured values: VSA = 3, VBP = 6, VFP = 8,
	 * VACT = 320  =>  vtotal = 337.
	 *
	 * TODO:
	 *  - Confirm H-timing (HFP/HSA/HBP) against the ST7785M datasheet;
	 *    LK did not print it. Values below are ST7789V-family defaults
	 *    for a 240x320 QVGA panel.
	 *  - Derive the pixel clock from the measured DSI data_rate = 140MHz.
	 *    The value below (298 * 337 * 60Hz) is a 60Hz placeholder.
	 *  - Confirm width_mm / height_mm on the actual 2.4" panel.
	 */
	.clock		= 6026,

	.hdisplay	= 240,
	.hsync_start	= 240 + 38,
	.hsync_end	= 240 + 38 + 10,
	.htotal		= 240 + 38 + 10 + 10,

	.vdisplay	= 320,
	.vsync_start	= 320 + 8,		/* + VFP = 8 */
	.vsync_end	= 320 + 8 + 3,		/* + VSA = 3 */
	.vtotal		= 320 + 8 + 3 + 6,	/* + VBP = 6 => 337 */

	.width_mm	= 43,
	.height_mm	= 57,

	.flags		= DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
	.type		= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct st7785m_panel_desc st7785m_desc = {
	.mode		= &st7785m_mode,
	.lanes		= 1,
	.format		= MIPI_DSI_FMT_RGB565,
	/*
	 * LK programmed DSI video mode (mode = 2). On mtk_dsi this maps to
	 * burst video mode; DCS commands are issued in low-power mode.
	 */
	.mode_flags	= MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.sleep_delay	= 0,
};

static void st7785m_cleanup(void *data)
{
	struct st7785m *ctx = data;

	drm_panel_remove(&ctx->panel);
	drm_panel_disable(&ctx->panel);
	drm_panel_unprepare(&ctx->panel);
}

static int st7785m_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct st7785m_panel_desc *desc;
	struct st7785m *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct st7785m, panel, &st7785m_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -ENODEV;

	ctx->desc = desc;
	ctx->dsi = dsi;

	/*
	 * Power and reset are optional so that a bare DT panel node (without
	 * vcc-supply / reset-gpios) still probes for bring-up logging.
	 */
	ctx->power = devm_regulator_get_optional(dev, "vcc");
	if (IS_ERR(ctx->power)) {
		if (PTR_ERR(ctx->power) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		ctx->power = NULL;
		dev_dbg(dev, "no vcc-supply regulator found\n");
	}

	ctx->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset))
		return dev_err_probe(dev, PTR_ERR(ctx->reset),
				     "Failed to get reset GPIO\n");

	ret = of_drm_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get panel orientation\n");

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	/* DSI bus configuration: 1 lane, RGB565 (2 bytes/pixel) video mode. */
	dsi->lanes = desc->lanes;
	dsi->format = desc->format;
	dsi->mode_flags = desc->mode_flags;

	dev_info(dev, "Sitronix ST7785M %dx%d %u-lane DSI panel probed\n",
		 desc->mode->hdisplay, desc->mode->vdisplay, desc->lanes);

	ret = mipi_dsi_attach(dsi);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to attach to MIPI DSI host\n");

	return devm_add_action_or_reset(dev, st7785m_cleanup, ctx);
}

static void st7785m_remove(struct mipi_dsi_device *dsi)
{
	mipi_dsi_detach(dsi);
}

static const struct of_device_id st7785m_of_match[] = {
	{ .compatible = "sitronix,st7785m", .data = &st7785m_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, st7785m_of_match);

static struct mipi_dsi_driver st7785m_driver = {
	.probe	= st7785m_probe,
	.remove	= st7785m_remove,
	.driver = {
		.name		= "st7785m",
		.of_match_table	= st7785m_of_match,
	},
};
module_mipi_dsi_driver(st7785m_driver);

MODULE_DESCRIPTION("Sitronix ST7785M MIPI-DSI panel driver");
MODULE_LICENSE("GPL");
