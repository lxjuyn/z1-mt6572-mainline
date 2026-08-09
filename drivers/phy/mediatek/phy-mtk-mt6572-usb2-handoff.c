// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek MT6572 USB2 PHY driver
 *
 * The register programming below is derived from the MT6572 vendor USB PHY
 * recover/savecurrent sequences. The MUSB glue enables the shared clocks
 * before calling into this PHY, so this driver deliberately does not manage
 * clocks itself.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

struct mt6572_usb2_phy {
	struct device *dev;
	void __iomem *base;
	struct mutex lock;
	bool powered;
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
 * select the HS transmit slew rate. Keep its FRA/PARA arithmetic intact.
 */
static void mt6572_usb2_hs_slew_rate_cal(struct mt6572_usb2_phy *priv)
{
	const u32 fra = 48;
	const u32 para = 28;
	u32 fm_out;
	u32 value = 4;
	int ret;

	writeb(0x80, priv->base + 0x15);
	udelay(1);
	writeb(0x01, priv->base + 0x511);
	writeb(0x04, priv->base + 0x501);
	writeb(BIT(0), priv->base + 0x503);

	/* A probe-time calibration must not inherit the vendor's 3 s wait. */
	ret = read_poll_timeout(readb, fm_out, fm_out & BIT(0), 1, 10000,
				false, priv->base + 0x510);
	if (ret) {
		dev_warn(priv->dev, "HS slew-rate calibration timed out; using %u\n",
			 value);
		goto out;
	}

	fm_out = readl(priv->base + 0x50c);
	if (!fm_out) {
		dev_warn(priv->dev, "HS slew-rate calibration produced zero; using %u\n",
			 value);
		goto out;
	}

	/* Vendor formula: ((1024 * FRA * PARA / fm_out) + 500) / 1000. */
	value = (1024 * fra * para / fm_out + 500) / 1000;
out:
	mt6572_usb2_clear_bits(priv, 0x503, BIT(0));
	mt6572_usb2_clear_bits(priv, 0x511, BIT(0));
	writeb(value << 4, priv->base + 0x15);
	mt6572_usb2_clear_bits(priv, 0x15, BIT(7));
	dev_dbg(priv->dev, "HS slew-rate calibration result %u\n", value);
}

static int mt6572_usb2_phy_power_on(struct phy *phy)
{
	struct mt6572_usb2_phy *priv = phy_get_drvdata(phy);
	int ret = 0;

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
	mt6572_usb2_set_bits(priv, 0x1a, 0x10); /* VBUS comparator */

	udelay(800);

	/* Exact MT6572 production force-device sequence. */
	mt6572_usb2_clear_bits(priv, 0x6c, 0x10);
	mt6572_usb2_set_bits(priv, 0x6c, 0x2e);
	mt6572_usb2_set_bits(priv, 0x6d, 0x3e);

	mt6572_usb2_hs_slew_rate_cal(priv);

	/* Default VRT_VREF_SEL and TERM_VREF_SEL values from vendor recover. */
	mt6572_usb2_set_bits(priv, 0x05, 0x10);
	mt6572_usb2_set_bits(priv, 0x05, 0x01);

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
	if (mode != PHY_MODE_USB_DEVICE)
		return -EINVAL;

	return 0;
}

static const struct phy_ops mt6572_usb2_phy_ops = {
	.power_on	= mt6572_usb2_phy_power_on,
	.power_off	= mt6572_usb2_phy_power_off,
	.set_mode	= mt6572_usb2_phy_set_mode,
	.owner		= THIS_MODULE,
};

static int mt6572_usb2_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mt6572_usb2_phy *priv;
	struct phy *phy;
	struct phy_provider *provider;

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

	return PTR_ERR_OR_ZERO(provider);
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
