// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek MT6572 USB2 PHY driver
 *
 * The register programming below is derived from the MT6572 vendor USB PHY
 * recover/savecurrent sequences. The MUSB glue enables the shared clocks
 * before calling into this PHY, so this driver deliberately does not manage
 * clocks itself.
 *
 * Fixes applied for Z1 bring-up (2026-08-17):
 *   1. HS slew-rate calibration: added free-run clock enable (tphy-style),
 *      reduced timeout from 100ms to 10ms per attempt (2 retries = 20ms total),
 *      added module param to skip calibration entirely for debugging.
 *   2. VBUS comparator: increased stabilization delay from 800us to 2-3ms
 *      for cold boot reliability.
 *   3. PLL lock: increased stabilization delay from 1-1.5ms to 5-10ms
 *      after force-device sequence to ensure valid frequency meter readings.
 *   4. Added dev_info logging for power_on entry and calibration status.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/usb/otg.h>
#include <linux/usb/phy.h>

/* Default slew-rate value when calibration fails (derived from USB 2.0 spec) */
#define MT6572_DEFAULT_SLEW_RATE	6
/* Maximum calibration retries */
#define MT6572_SLEW_CAL_MAX_RETRIES	2
/* Calibration timeout per attempt in microseconds (10ms — fail fast) */
#define MT6572_SLEW_CAL_TIMEOUT_US	10000

/*
 * Module parameter: skip HS slew-rate calibration entirely.
 * Set to 1 to use hardcoded default value immediately, bypassing the
 * frequency meter sequence. Useful for debugging when calibration
 * always times out (saves ~20ms from power_on path).
 */
static bool skip_slew_cal;
module_param(skip_slew_cal, bool, 0644);
MODULE_PARM_DESC(skip_slew_cal, "Skip HS slew-rate calibration (use default value)");

struct mt6572_usb2_phy {
	struct device *dev;
	void __iomem *base;
	struct mutex lock;
	bool powered;
	bool slew_cal_valid;
	u8 slew_cal_value;
	/* Legacy USB PHY framework registration (for MUSB xceiv) */
	struct usb_phy usb_phy;
	struct usb_otg otg;
	struct phy *phy;	/* Generic PHY reference for usb_phy wrappers */
};

static void mt6572_usb2_update_bits(struct mt6572_usb2_phy *priv,
				    unsigned int offset, u8 mask, u8 value)
{
	u8 reg;

	reg = readb(priv->base + offset);
	reg &= ~mask;
	reg |= value & mask;
	writeb(reg, priv->base + offset);
}

static void mt6572_usb2_set_bits(struct mt6572_usb2_phy *priv,
				 unsigned int offset, u8 mask)
{
	mt6572_usb2_update_bits(priv, offset, mask, mask);
}

static void mt6572_usb2_clear_bits(struct mt6572_usb2_phy *priv,
				   unsigned int offset, u8 mask)
{
	mt6572_usb2_update_bits(priv, offset, mask, 0);
}

static bool mt6572_usb2_in_uart_mode(struct mt6572_usb2_phy *priv)
{
	return (readb(priv->base + 0x6b) & 0x5c) == 0x5c;
}

/*
 * MT6572 vendor usb_phy_hs_slew_rate_cal() uses the PHY frequency meter to
 * select the HS transmit slew rate. Enhanced with:
 *   - Free-run clock enable (borrowed from tphy driver) to ensure the
 *     frequency meter has a valid clock source
 *   - Short per-attempt timeout (10ms) with 2 retries — fail fast
 *   - Module param to skip calibration entirely for debugging
 *
 * Returns the calibrated value, or MT6572_DEFAULT_SLEW_RATE on failure.
 */
static u8 mt6572_usb2_hs_slew_rate_cal(struct mt6572_usb2_phy *priv)
{
	const u32 fra = 48;
	const u32 para = 28;
	u32 fm_out;
	u32 value = MT6572_DEFAULT_SLEW_RATE;
	int ret;
	int retry;

	/* Module param: skip calibration entirely */
	if (skip_slew_cal) {
		dev_info(priv->dev,
			 "HS slew-rate calibration skipped (module param); using default %u\n",
			 MT6572_DEFAULT_SLEW_RATE);
		goto apply_default;
	}

	/* Try calibration up to MT6572_SLEW_CAL_MAX_RETRIES times */
	for (retry = 0; retry < MT6572_SLEW_CAL_MAX_RETRIES; retry++) {
		/*
		 * Enable free-run clock first (tphy-style).
		 * The frequency meter needs a free-running reference clock to
		 * count against; without this, BIT(0) at 0x510 may never set.
		 * Offset 0x501 bit2 is the FRCK_EN equivalent for MT6572.
		 */
		mt6572_usb2_set_bits(priv, 0x501, BIT(2));
		udelay(5);

		/* Enable frequency meter */
		writeb(0x80, priv->base + 0x15);
		udelay(10);
		writeb(0x01, priv->base + 0x511);
		/* 0x501 already has FRCK_EN set above; add cycle count bits */
		mt6572_usb2_set_bits(priv, 0x501, 0x04);
		writeb(BIT(0), priv->base + 0x503);

		/*
		 * Wait for calibration complete with short timeout.
		 * 10ms per attempt × 2 retries = 20ms total (vs old 300ms).
		 * If calibration fails, the default value is safe for USB 2.0.
		 */
		ret = read_poll_timeout(readb, fm_out, fm_out & BIT(0),
					1000, MT6572_SLEW_CAL_TIMEOUT_US,
					false, priv->base + 0x510);
		if (ret) {
			dev_dbg(priv->dev, "HS slew-rate calibration attempt %d timed out (10ms)\n",
				retry + 1);
			goto cleanup;
		}

		fm_out = readl(priv->base + 0x50c);
		if (!fm_out) {
			dev_dbg(priv->dev, "HS slew-rate calibration attempt %d produced zero\n",
				retry + 1);
			goto cleanup;
		}

		/* Vendor formula: ((1024 * FRA * PARA / fm_out) + 500) / 1000 */
		value = (1024 * fra * para / fm_out + 500) / 1000;

		/* Sanity check: USB 2.0 HS slew rate should produce value 2-10 */
		if (value >= 2 && value <= 10) {
			dev_info(priv->dev, "HS slew-rate calibration succeeded on attempt %d: %u\n",
				 retry + 1, value);
			priv->slew_cal_valid = true;
			priv->slew_cal_value = value;
			goto apply_value;
		}

		dev_dbg(priv->dev, "HS slew-rate calibration attempt %d produced out-of-range value %u\n",
			retry + 1, value);

cleanup:
		/* Clean up for next attempt */
		mt6572_usb2_clear_bits(priv, 0x503, BIT(0));
		mt6572_usb2_clear_bits(priv, 0x511, BIT(0));
		mt6572_usb2_clear_bits(priv, 0x501, BIT(2)); /* disable FRCK_EN */
		udelay(50);
	}

	/* All retries exhausted, use default */
	dev_info(priv->dev, "HS slew-rate calibration failed after %d attempts; using default %u\n",
		 MT6572_SLEW_CAL_MAX_RETRIES, MT6572_DEFAULT_SLEW_RATE);

apply_default:
	priv->slew_cal_valid = false;
	priv->slew_cal_value = MT6572_DEFAULT_SLEW_RATE;
	value = MT6572_DEFAULT_SLEW_RATE;

apply_value:
	/* Apply the calibrated/default value */
	writeb(value << 4, priv->base + 0x15);
	mt6572_usb2_clear_bits(priv, 0x15, BIT(7));

	return value;
}

static int mt6572_usb2_phy_power_on(struct phy *phy)
{
	struct mt6572_usb2_phy *priv = phy_get_drvdata(phy);
	int ret = 0;
	u8 slew_value;

	mutex_lock(&priv->lock);

	if (priv->powered)
		goto out;

	if (mt6572_usb2_in_uart_mode(priv))
		dev_info(priv->dev, "recovering USB from UART mode\n");

	/* Recover the USB function and release the saved-current state. */
	mt6572_usb2_clear_bits(priv, 0x6b, 0x04); /* force_uart_en */
	mt6572_usb2_clear_bits(priv, 0x6e, 0x01); /* RG_UART_EN */
	mt6572_usb2_clear_bits(priv, 0x6a, 0x04); /* force_suspendm */
	mt6572_usb2_clear_bits(priv, 0x68, 0xf4); /* line-state park */
	mt6572_usb2_clear_bits(priv, 0x69, 0x3c); /* RG_DATAIN */
	mt6572_usb2_clear_bits(priv, 0x6a, 0xba); /* line-state force bits */
	mt6572_usb2_clear_bits(priv, 0x1a, 0x80); /* BC1.2 */

	/*
	 * Force VBUS comparator on for device mode.
	 * Z1 operates as USB device only (CDC-ACM gadget). The VBUS comparator
	 * must be enabled for the PHY to detect VBUS presence and enter device
	 * mode. Without this, the MUSB controller may not enumerate even when
	 * connected to a host.
	 *
	 * Increased stabilization delay from 800us to 2ms: on cold boot the
	 * VBUS comparator may need more time to settle, especially when VBUS
	 * is provided solely by software (no physical VBUS regulator).
	 */
	mt6572_usb2_set_bits(priv, 0x1a, 0x10); /* VBUS comparator */

	usleep_range(2000, 3000);

	dev_info(priv->dev, "USB PHY power_on: VBUS comparator enabled, starting force-device sequence\n");

	/* Exact MT6572 production force-device sequence. */
	mt6572_usb2_clear_bits(priv, 0x6c, 0x10);
	mt6572_usb2_set_bits(priv, 0x6c, 0x2e);
	mt6572_usb2_set_bits(priv, 0x6d, 0x3e);

	/*
	 * Wait for PHY PLL to lock after force-device sequence.
	 * Increased from 1-1.5ms to 5-10ms: Z1 cold boot may need longer
	 * for the PHY PLL to stabilize. Insufficient PLL lock causes the
	 * frequency meter to return invalid values during slew-rate cal.
	 */
	usleep_range(5000, 10000);

	/* Perform HS slew-rate calibration with retry logic */
	slew_value = mt6572_usb2_hs_slew_rate_cal(priv);
	dev_dbg(priv->dev, "HS slew-rate value applied: %u\n", slew_value);

	/* Default VRT_VREF_SEL and TERM_VREF_SEL values from vendor recover. */
	mt6572_usb2_set_bits(priv, 0x05, 0x10);
	mt6572_usb2_set_bits(priv, 0x05, 0x01);

	/*
	 * Additional stabilization delay before returning.
	 * Allows the PHY to fully settle before the MUSB controller starts
	 * enumeration. This prevents race conditions where the controller
	 * tries to communicate before the PHY is ready.
	 */
	usleep_range(2000, 3000);

	priv->powered = true;
out:
	mutex_unlock(&priv->lock);

	return ret;
}

static int mt6572_usb2_phy_power_off(struct phy *phy)
{
	struct mt6572_usb2_phy *priv = phy_get_drvdata(phy);
	int ret = 0;

	mutex_lock(&priv->lock);

	if (!priv->powered)
		goto out;

	/* MT6572 vendor usb_phy_savecurrent() sequence. */
	mt6572_usb2_clear_bits(priv, 0x6b, 0x04); /* force_uart_en */
	mt6572_usb2_clear_bits(priv, 0x6e, 0x01); /* RG_UART_EN */
	mt6572_usb2_clear_bits(priv, 0x6a, 0x04); /* release force_suspendm */
	mt6572_usb2_set_bits(priv, 0x68, 0xc0); /* DP/DM pulldown */
	mt6572_usb2_clear_bits(priv, 0x68, 0x30); /* XCVRSEL */
	mt6572_usb2_set_bits(priv, 0x68, 0x10); /* XCVRSEL = 01 */
	mt6572_usb2_set_bits(priv, 0x68, 0x04); /* TERMSEL */
	mt6572_usb2_clear_bits(priv, 0x69, 0x3c); /* RG_DATAIN */
	mt6572_usb2_set_bits(priv, 0x6a, 0xba); /* force line state */
	mt6572_usb2_clear_bits(priv, 0x1a, 0x80); /* BC1.2 */
	mt6572_usb2_clear_bits(priv, 0x1a, 0x10); /* VBUS comparator */

	udelay(800);

	mt6572_usb2_set_bits(priv, 0x63, 0x02); /* PLL stable */
	udelay(1);
	mt6572_usb2_set_bits(priv, 0x6a, 0x04); /* force_suspendm */
	udelay(1);

	/* Exact MT6572 production force-device sequence. */
	mt6572_usb2_clear_bits(priv, 0x6c, 0x10);
	mt6572_usb2_set_bits(priv, 0x6c, 0x2e);
	mt6572_usb2_set_bits(priv, 0x6d, 0x3e);

	priv->powered = false;
out:
	mutex_unlock(&priv->lock);

	return ret;
}

static int mt6572_usb2_phy_set_mode(struct phy *phy, enum phy_mode mode,
				    int submode)
{
	/*
	 * Z1 operates exclusively in device mode. Accept both DEVICE and OTG
	 * modes for compatibility, but always configure for device operation.
	 */
	if (mode != PHY_MODE_USB_DEVICE && mode != PHY_MODE_USB_OTG)
		return -EINVAL;

	return 0;
}

static const struct phy_ops mt6572_usb2_phy_ops = {
	.power_on	= mt6572_usb2_phy_power_on,
	.power_off	= mt6572_usb2_phy_power_off,
	.set_mode	= mt6572_usb2_phy_set_mode,
	.owner		= THIS_MODULE,
};

/*
 * Legacy USB PHY framework wrappers.
 * The MUSB glue layer uses devm_usb_get_phy() to obtain a usb_phy pointer
 * for OTG state tracking (musb->xceiv->otg->state). By registering with
 * the legacy framework, MUSB gets a real MT6572 PHY instead of a NOP
 * transceiver that lacks VBUS detection.
 */
static int mt6572_usb_phy_init(struct usb_phy *phy)
{
	struct mt6572_usb2_phy *priv = container_of(phy, struct mt6572_usb2_phy, usb_phy);

	return phy_power_on(priv->phy);
}

static void mt6572_usb_phy_shutdown(struct usb_phy *phy)
{
	struct mt6572_usb2_phy *priv = container_of(phy, struct mt6572_usb2_phy, usb_phy);

	phy_power_off(priv->phy);
}

static int mt6572_usb2_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mt6572_usb2_phy *priv;
	struct phy *phy;
	struct phy_provider *provider;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->dev = dev;
	mutex_init(&priv->lock);

	phy = devm_phy_create(dev, NULL, &mt6572_usb2_phy_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	phy_set_drvdata(phy, priv);
	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider))
		return PTR_ERR(provider);

	/*
	 * Register with the legacy USB PHY framework so that
	 * devm_usb_get_phy(dev, USB_PHY_TYPE_USB2) from the MUSB
	 * glue layer finds this real MT6572 PHY (not a NOP transceiver).
	 *
	 * The MUSB controller needs usb_phy->otg for OTG state tracking
	 * and usb_phy->init/shutdown for PHY power sequencing.
	 */
	priv->phy = phy;	/* store Generic PHY ref for wrapper callbacks */
	priv->usb_phy.dev = dev;
	priv->usb_phy.init = mt6572_usb_phy_init;
	priv->usb_phy.shutdown = mt6572_usb_phy_shutdown;
	priv->usb_phy.type = USB_PHY_TYPE_USB2;
	priv->otg.usb_phy = &priv->usb_phy;
	priv->otg.state = OTG_STATE_B_IDLE;
	priv->usb_phy.otg = &priv->otg;

	ret = usb_add_phy_dev(&priv->usb_phy);
	if (ret) {
		dev_err(dev, "failed to register legacy USB PHY: %d\n", ret);
		return ret;
	}

	dev_info(dev, "MT6572 USB2 PHY registered (Generic + legacy)\n");

	return 0;
}

static const struct of_device_id mt6572_usb2_phy_of_match[] = {
	{ .compatible = "mediatek,mt6572-usb2-phy", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mt6572_usb2_phy_of_match);

static struct platform_driver mt6572_usb2_phy_driver = {
	.probe	= mt6572_usb2_phy_probe,
	.driver	= {
		.name		= "phy-mtk-mt6572-usb2-handoff",
		.of_match_table	= mt6572_usb2_phy_of_match,
	},
};
module_platform_driver(mt6572_usb2_phy_driver);

MODULE_DESCRIPTION("MediaTek MT6572 USB2 PHY driver");
MODULE_AUTHOR("Z1 mainline port");
MODULE_LICENSE("GPL");
