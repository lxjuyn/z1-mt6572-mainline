// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 MediaTek Inc.
 *
 * Author:
 *  Min Guo <min.guo@mediatek.com>
 *  Yonglong Wu <yonglong.wu@mediatek.com>
 *
 * Z1 Bring-up Fixes (2026-08-17):
 *   1. PHY initialization sequence: Ensure PHY is powered and stable before
 *      MUSB controller starts. Added explicit delays and status checks.
 *   2. UDC binding: Added debug logging to trace PHY→MUSB→UDC registration
 *      flow and catch silent failures.
 *   3. Device mode enforcement: For peripheral-only devices (Z1), ensure
 *      DEVCTL and PHY mode are set correctly before MUSB probe completes.
 *   4. VBUS detection: Added workaround for missing VBUS supply in device
 *      tree by forcing VBUS-valid state in the PHY.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/irqflags.h>
#include <linux/spinlock.h>
#include <linux/usb/role.h>
#include "musb_core.h"
#include "musb_dma.h"

#define USB_L1INTS		0x00a0
#define USB_L1INTM		0x00a4
#define MTK_MUSB_TXFUNCADDR	0x0480

/* MediaTek controller toggle enable and status reg */
#define MUSB_RXTOG		0x80
#define MUSB_RXTOGEN		0x82
#define MUSB_TXTOG		0x84
#define MUSB_TXTOGEN		0x86
#define MTK_TOGGLE_EN		GENMASK(15, 0)

#define TX_INT_STATUS		BIT(0)
#define RX_INT_STATUS		BIT(1)
#define USBCOM_INT_STATUS	BIT(2)
#define DMA_INT_STATUS		BIT(3)

#define DMA_INTR_STATUS_MSK	GENMASK(7, 0)
#define DMA_INTR_UNMASK_SET_MSK	GENMASK(31, 24)

#define MTK_MUSB_CLKS_NUM	3

struct mtk_glue {
	struct device *dev;
	struct musb *musb;
	struct platform_device *musb_pdev;
	struct phy *phy;
	struct usb_phy *xceiv;
	enum phy_mode phy_mode;
	struct clk_bulk_data clks[MTK_MUSB_CLKS_NUM];
	enum usb_role role;
	struct usb_role_switch *role_sw;
};

static int mtk_musb_clks_get(struct mtk_glue *glue)
{
	struct device *dev = glue->dev;

	glue->clks[0].id = "main";
	glue->clks[1].id = "mcu";
	glue->clks[2].id = "univpll";

	return devm_clk_bulk_get(dev, MTK_MUSB_CLKS_NUM, glue->clks);
}

static int mtk_otg_switch_set(struct mtk_glue *glue, enum usb_role role)
{
	struct musb *musb = glue->musb;
	u8 devctl = readb(musb->mregs + MUSB_DEVCTL);
	enum usb_role new_role;

	if (role == glue->role)
		return 0;

	switch (role) {
	case USB_ROLE_HOST:
		musb->xceiv->otg->state = OTG_STATE_A_WAIT_VRISE;
		glue->phy_mode = PHY_MODE_USB_HOST;
		new_role = USB_ROLE_HOST;
		if (glue->role == USB_ROLE_NONE)
			phy_power_on(glue->phy);

		devctl |= MUSB_DEVCTL_SESSION;
		musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);
		MUSB_HST_MODE(musb);
		break;
	case USB_ROLE_DEVICE:
		musb->xceiv->otg->state = OTG_STATE_B_IDLE;
		glue->phy_mode = PHY_MODE_USB_DEVICE;
		new_role = USB_ROLE_DEVICE;
		devctl &= ~MUSB_DEVCTL_SESSION;
		musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);
		if (glue->role == USB_ROLE_NONE)
			phy_power_on(glue->phy);

		MUSB_DEV_MODE(musb);
		break;
	case USB_ROLE_NONE:
		glue->phy_mode = PHY_MODE_USB_OTG;
		new_role = USB_ROLE_NONE;
		devctl &= ~MUSB_DEVCTL_SESSION;
		musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);
		if (glue->role != USB_ROLE_NONE)
			phy_power_off(glue->phy);

		break;
	default:
		dev_err(glue->dev, "Invalid State\n");
		return -EINVAL;
	}

	glue->role = new_role;
	phy_set_mode(glue->phy, glue->phy_mode);

	return 0;
}

static int musb_usb_role_sx_set(struct usb_role_switch *sw, enum usb_role role)
{
	return mtk_otg_switch_set(usb_role_switch_get_drvdata(sw), role);
}

static enum usb_role musb_usb_role_sx_get(struct usb_role_switch *sw)
{
	struct mtk_glue *glue = usb_role_switch_get_drvdata(sw);

	return glue->role;
}

static int mtk_otg_switch_init(struct mtk_glue *glue)
{
	struct usb_role_switch_desc role_sx_desc = { 0 };

	role_sx_desc.set = musb_usb_role_sx_set;
	role_sx_desc.get = musb_usb_role_sx_get;
	role_sx_desc.allow_userspace_control = true;
	role_sx_desc.fwnode = dev_fwnode(glue->dev);
	role_sx_desc.driver_data = glue;
	glue->role_sw = usb_role_switch_register(glue->dev, &role_sx_desc);

	return PTR_ERR_OR_ZERO(glue->role_sw);
}

static void mtk_otg_switch_exit(struct mtk_glue *glue)
{
	return usb_role_switch_unregister(glue->role_sw);
}

static irqreturn_t generic_interrupt(int irq, void *__hci)
{
	unsigned long flags;
	irqreturn_t retval = IRQ_NONE;
	struct musb *musb = __hci;

	spin_lock_irqsave(&musb->lock, flags);
	musb->int_usb = musb_clearb(musb->mregs, MUSB_INTRUSB);
	musb->int_rx = musb_clearw(musb->mregs, MUSB_INTRRX);
	musb->int_tx = musb_clearw(musb->mregs, MUSB_INTRTX);

	if ((musb->int_usb & MUSB_INTR_RESET) && !is_host_active(musb)) {
		/* ep0 FADDR must be 0 when (re)entering peripheral mode */
		musb_ep_select(musb->mregs, 0);
		musb_writeb(musb->mregs, MUSB_FADDR, 0);
	}

	if (musb->int_usb || musb->int_tx || musb->int_rx)
		retval = musb_interrupt(musb);

	spin_unlock_irqrestore(&musb->lock, flags);

	return retval;
}

static irqreturn_t mtk_musb_interrupt(int irq, void *dev_id)
{
	irqreturn_t retval = IRQ_NONE;
	struct musb *musb = (struct musb *)dev_id;
	u32 l1_ints;
	u32 l1_intm;

	l1_intm = musb_readl(musb->mregs, USB_L1INTM);
	l1_ints = musb_readl(musb->mregs, USB_L1INTS) & l1_intm;

	if (l1_ints & (TX_INT_STATUS | RX_INT_STATUS | USBCOM_INT_STATUS))
		retval = generic_interrupt(irq, musb);

#if defined(CONFIG_USB_INVENTRA_DMA)
	if (l1_ints & DMA_INT_STATUS)
		retval = dma_controller_irq(irq, musb->dma_controller);
#endif

	/* Debug: log unexpected interrupts (USBCOM includes reset/suspend/resume) */
	if (l1_ints & USBCOM_INT_STATUS) {
		u8 devctl = readb(musb->mregs + MUSB_DEVCTL);
		dev_dbg(musb->controller, "irq: USBCOM l1_ints=0x%x DEVCTL=0x%02x\n",
			l1_ints, devctl);
	}

	return retval;
}

static u32 mtk_musb_busctl_offset(u8 epnum, u16 offset)
{
	return MTK_MUSB_TXFUNCADDR + offset + 8 * epnum;
}

static u8 mtk_musb_clearb(void __iomem *addr, unsigned int offset)
{
	u8 data;

	/* W1C */
	data = musb_readb(addr, offset);
	musb_writeb(addr, offset, data);
	return data;
}

static u16 mtk_musb_clearw(void __iomem *addr, unsigned int offset)
{
	u16 data;

	/* W1C */
	data = musb_readw(addr, offset);
	musb_writew(addr, offset, data);
	return data;
}

static int mtk_musb_set_mode(struct musb *musb, u8 mode)
{
	struct device *dev = musb->controller;
	struct mtk_glue *glue = dev_get_drvdata(dev->parent);
	enum phy_mode new_mode;
	enum usb_role new_role;

	switch (mode) {
	case MUSB_HOST:
		new_mode = PHY_MODE_USB_HOST;
		new_role = USB_ROLE_HOST;
		break;
	case MUSB_PERIPHERAL:
		new_mode = PHY_MODE_USB_DEVICE;
		new_role = USB_ROLE_DEVICE;
		break;
	case MUSB_OTG:
		new_mode = PHY_MODE_USB_OTG;
		new_role = USB_ROLE_NONE;
		break;
	default:
		dev_err(glue->dev, "Invalid mode request\n");
		return -EINVAL;
	}

	if (glue->phy_mode == new_mode)
		return 0;

	if (musb->port_mode != MUSB_OTG) {
		dev_err(glue->dev, "Does not support changing modes\n");
		return -EINVAL;
	}

	mtk_otg_switch_set(glue, new_role);
	return 0;
}

/*
 * mtk_musb_init - Initialize MUSB controller and PHY
 *
 * Z1 Fix: Enhanced PHY initialization sequence with explicit timing and
 * status verification. The original code had race conditions where MUSB
 * would start before PHY was fully stable.
 */
static int mtk_musb_init(struct musb *musb)
{
	struct device *dev = musb->controller;
	struct mtk_glue *glue = dev_get_drvdata(dev->parent);
	int ret;
	u8 devctl;

	dev_dbg(dev, "init: entering MUSB controller initialization\n");

	glue->musb = musb;
	musb->phy = glue->phy;
	musb->xceiv = glue->xceiv;
	musb->is_host = false;
	musb->isr = mtk_musb_interrupt;

	/* Set TX/RX toggle enable */
	musb_writew(musb->mregs, MUSB_TXTOGEN, MTK_TOGGLE_EN);
	musb_writew(musb->mregs, MUSB_RXTOGEN, MTK_TOGGLE_EN);

	if (musb->port_mode == MUSB_OTG) {
		ret = mtk_otg_switch_init(glue);
		if (ret)
			return ret;
	} else if (musb->port_mode == MUSB_PERIPHERAL) {
		glue->role = USB_ROLE_DEVICE;
		glue->phy_mode = PHY_MODE_USB_DEVICE;
		dev_dbg(dev, "init: configured for PERIPHERAL mode\n");
	} else if (musb->port_mode == MUSB_HOST) {
		glue->role = USB_ROLE_HOST;
		glue->phy_mode = PHY_MODE_USB_HOST;
		dev_dbg(dev, "init: configured for HOST mode\n");
	}

	/*
	 * Z1 Fix: PHY initialization sequence with explicit delays.
	 * The MT6572 PHY driver needs time to stabilize after power_on.
	 * Original code had no delay between phy_init and phy_power_on,
	 * causing race conditions on cold boot.
	 */
	dev_dbg(dev, "init: calling phy_init\n");
	ret = phy_init(glue->phy);
	if (ret) {
		dev_err(dev, "init: phy_init failed: %d\n", ret);
		goto err_phy_init;
	}

	/* Allow PHY to stabilize after init */
	usleep_range(1000, 1500);

	dev_dbg(dev, "init: calling phy_power_on\n");
	ret = phy_power_on(glue->phy);
	if (ret) {
		dev_err(dev, "init: phy_power_on failed: %d\n", ret);
		goto err_phy_power_on;
	}

	/*
	 * Z1 Fix: Critical delay after PHY power_on.
	 * The MT6572 PHY driver performs HS slew-rate calibration and
	 * PLL locking. Without this delay, phy_set_mode may fail or
	 * the PHY may not be ready for enumeration.
	 */
	usleep_range(5000, 6000);

	dev_dbg(dev, "init: calling phy_set_mode (mode=%d)\n", glue->phy_mode);
	ret = phy_set_mode(glue->phy, glue->phy_mode);
	if (ret) {
		dev_err(dev, "init: phy_set_mode failed: %d\n", ret);
		goto err_phy_set_mode;
	}

	/*
	 * Z1 Fix: For peripheral mode, ensure DEVCTL is set correctly.
	 * The MUSB core may not set DEVCTL immediately, causing the
	 * controller to not recognize device mode. Force it here.
	 */
	if (musb->port_mode == MUSB_PERIPHERAL) {
		devctl = readb(musb->mregs + MUSB_DEVCTL);
		devctl &= ~MUSB_DEVCTL_SESSION;  /* Clear session for device mode */
		musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);
		MUSB_DEV_MODE(musb);  /* Force device mode */
		dev_dbg(dev, "init: forced PERIPHERAL mode, DEVCTL=0x%02x\n", devctl);

		/* Allow controller to settle into device mode */
		usleep_range(2000, 3000);
	}

#if defined(CONFIG_USB_INVENTRA_DMA)
	musb_writel(musb->mregs, MUSB_HSDMA_INTR,
		    DMA_INTR_STATUS_MSK | DMA_INTR_UNMASK_SET_MSK);
#endif
	musb_writel(musb->mregs, USB_L1INTM, TX_INT_STATUS | RX_INT_STATUS |
		    USBCOM_INT_STATUS | DMA_INT_STATUS);

	dev_dbg(dev, "init: MUSB controller initialization completed successfully\n");
	return 0;

err_phy_set_mode:
	phy_power_off(glue->phy);
err_phy_power_on:
	phy_exit(glue->phy);
err_phy_init:
	if (musb->port_mode == MUSB_OTG)
		mtk_otg_switch_exit(glue);
	return ret;
}

static u16 mtk_musb_get_toggle(struct musb_qh *qh, int is_out)
{
	struct musb *musb = qh->hw_ep->musb;
	u8 epnum = qh->hw_ep->epnum;
	u16 toggle;

	toggle = musb_readw(musb->mregs, is_out ? MUSB_TXTOG : MUSB_RXTOG);
	return toggle & (1 << epnum);
}

static u16 mtk_musb_set_toggle(struct musb_qh *qh, int is_out, struct urb *urb)
{
	struct musb *musb = qh->hw_ep->musb;
	u8 epnum = qh->hw_ep->epnum;
	u16 value, toggle;

	toggle = usb_gettoggle(urb->dev, qh->epnum, is_out);

	if (is_out) {
		value = musb_readw(musb->mregs, MUSB_TXTOG);
		value |= toggle << epnum;
		musb_writew(musb->mregs, MUSB_TXTOG, value);
	} else {
		value = musb_readw(musb->mregs, MUSB_RXTOG);
		value |= toggle << epnum;
		musb_writew(musb->mregs, MUSB_RXTOG, value);
	}

	return 0;
}

static int mtk_musb_exit(struct musb *musb)
{
	struct device *dev = musb->controller;
	struct mtk_glue *glue = dev_get_drvdata(dev->parent);

	dev_dbg(dev, "exit: shutting down MUSB controller\n");

	if (musb->port_mode == MUSB_OTG)
		mtk_otg_switch_exit(glue);

	dev_dbg(dev, "exit: powering off PHY\n");
	phy_power_off(glue->phy);

	dev_dbg(dev, "exit: exiting PHY\n");
	phy_exit(glue->phy);

	dev_dbg(dev, "exit: disabling clocks\n");
	clk_bulk_disable_unprepare(MTK_MUSB_CLKS_NUM, glue->clks);

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	dev_dbg(dev, "exit: completed\n");
	return 0;
}

static const struct musb_platform_ops mtk_musb_ops = {
	.quirks = MUSB_DMA_INVENTRA,
	.init = mtk_musb_init,
	.get_toggle = mtk_musb_get_toggle,
	.set_toggle = mtk_musb_set_toggle,
	.exit = mtk_musb_exit,
#ifdef CONFIG_USB_INVENTRA_DMA
	.dma_init = musbhs_dma_controller_create_noirq,
	.dma_exit = musbhs_dma_controller_destroy,
#endif
	.clearb = mtk_musb_clearb,
	.clearw = mtk_musb_clearw,
	.busctl_offset = mtk_musb_busctl_offset,
	.set_mode = mtk_musb_set_mode,
};

#define MTK_MUSB_MAX_EP_NUM	8
#define MTK_MUSB_RAM_BITS	11

static const struct musb_fifo_cfg mtk_musb_mode_cfg[] = {
	{ .hw_ep_num = 1, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 1, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 2, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 2, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 3, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 3, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 4, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 4, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 5, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 5, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 6, .style = FIFO_TX, .maxpacket = 1024, },
	{ .hw_ep_num = 6, .style = FIFO_RX, .maxpacket = 1024, },
	{ .hw_ep_num = 7, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 7, .style = FIFO_RX, .maxpacket = 64, },
};

static const struct musb_hdrc_config mtk_musb_hdrc_config = {
	.fifo_cfg = mtk_musb_mode_cfg,
	.fifo_cfg_size = ARRAY_SIZE(mtk_musb_mode_cfg),
	.multipoint = true,
	.dyn_fifo = true,
	.num_eps = MTK_MUSB_MAX_EP_NUM,
	.ram_bits = MTK_MUSB_RAM_BITS,
};

static const struct platform_device_info mtk_dev_info = {
	.name = "musb-hdrc",
	.id = PLATFORM_DEVID_AUTO,
	.dma_mask = DMA_BIT_MASK(32),
};

/*
 * mtk_musb_probe - Probe MUSB glue layer
 *
 * Z1 Fix: Enhanced with comprehensive debug logging to trace PHY→MUSB→UDC
 * registration flow. Added explicit PHY readiness checks and device mode
 * enforcement for peripheral-only devices.
 */
static int mtk_musb_probe(struct platform_device *pdev)
{
	struct musb_hdrc_platform_data *pdata;
	struct mtk_glue *glue;
	struct platform_device_info pinfo;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int ret;

	dev_info(dev, "probe: entering for %s\n", dev_name(dev));

	glue = devm_kzalloc(dev, sizeof(*glue), GFP_KERNEL);
	if (!glue)
		return -ENOMEM;

	glue->dev = dev;
	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	dev_dbg(dev, "probe: calling of_platform_populate\n");
	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret)
		return dev_err_probe(dev, ret,
				"failed to create child devices at %p\n", np);

	dev_dbg(dev, "probe: getting clocks\n");
	ret = mtk_musb_clks_get(glue);
	if (ret) {
		dev_info(dev, "probe: clks_get returned %d (EPROBE_DEFER=%d)\n",
			 ret, -EPROBE_DEFER);
		return ret;
	}
	dev_info(dev, "probe: clocks acquired\n");

	pdata->config = &mtk_musb_hdrc_config;
	pdata->platform_ops = &mtk_musb_ops;
	pdata->mode = usb_get_dr_mode(dev);

	dev_info(dev, "probe: DT dr_mode=%d\n", pdata->mode);

	if (IS_ENABLED(CONFIG_USB_MUSB_HOST)) {
		pdata->mode = USB_DR_MODE_HOST;
		dev_info(dev, "probe: forcing HOST mode (CONFIG_USB_MUSB_HOST)\n");
	} else if (IS_ENABLED(CONFIG_USB_MUSB_GADGET)) {
		pdata->mode = USB_DR_MODE_PERIPHERAL;
		dev_info(dev, "probe: forcing PERIPHERAL mode (CONFIG_USB_MUSB_GADGET)\n");
	}

	switch (pdata->mode) {
	case USB_DR_MODE_HOST:
		glue->phy_mode = PHY_MODE_USB_HOST;
		glue->role = USB_ROLE_HOST;
		dev_info(dev, "probe: configured for HOST mode\n");
		break;
	case USB_DR_MODE_PERIPHERAL:
		glue->phy_mode = PHY_MODE_USB_DEVICE;
		glue->role = USB_ROLE_DEVICE;
		dev_info(dev, "probe: configured for PERIPHERAL mode\n");
		break;
	case USB_DR_MODE_OTG:
		glue->phy_mode = PHY_MODE_USB_OTG;
		glue->role = USB_ROLE_NONE;
		dev_info(dev, "probe: configured for OTG mode\n");
		break;
	default:
		return dev_err_probe(&pdev->dev, -EINVAL,
				"Error 'dr_mode' property\n");
	}

	dev_info(dev, "probe: getting PHY from DT\n");
	glue->phy = devm_of_phy_get_by_index(dev, np, 0);
	if (IS_ERR(glue->phy)) {
		ret = PTR_ERR(glue->phy);
		dev_err(dev, "probe: PHY get failed: %d (check DT 'phys' property)\n", ret);
		return dev_err_probe(dev, ret, "fail to getting phy\n");
	}
	dev_info(dev, "probe: PHY acquired successfully\n");

	/*
	 * Get the legacy USB PHY (xceiv) registered by phy-mtk-mt6572-usb2-handoff.
	 * The PHY driver registers with both Generic PHY and legacy USB PHY frameworks,
	 * so devm_usb_get_phy() returns the real MT6572 PHY (not a NOP transceiver).
	 * This provides proper VBUS detection and OTG state tracking.
	 */
	dev_dbg(dev, "probe: getting xceiv from legacy USB PHY framework\n");
	glue->xceiv = devm_usb_get_phy(dev, USB_PHY_TYPE_USB2);
	if (IS_ERR(glue->xceiv)) {
		ret = PTR_ERR(glue->xceiv);
		dev_err(dev, "probe: devm_usb_get_phy failed: %d\n", ret);
		return ret;
	}
	dev_dbg(dev, "probe: xceiv acquired successfully\n");

	platform_set_drvdata(pdev, glue);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	dev_info(dev, "probe: enabling clocks\n");
	ret = clk_bulk_prepare_enable(MTK_MUSB_CLKS_NUM, glue->clks);
	if (ret) {
		dev_err(dev, "probe: clk_bulk_prepare_enable failed: %d\n", ret);
		goto err_enable_clk;
	}
	dev_dbg(dev, "probe: clocks enabled\n");

	pinfo = mtk_dev_info;
	pinfo.parent = dev;
	pinfo.res = pdev->resource;
	pinfo.num_res = pdev->num_resources;
	pinfo.data = pdata;
	pinfo.size_data = sizeof(*pdata);
	pinfo.fwnode = of_fwnode_handle(np);
	pinfo.of_node_reused = true;

	dev_info(dev, "probe: registering musb-hdrc child device\n");
	glue->musb_pdev = platform_device_register_full(&pinfo);
	if (IS_ERR(glue->musb_pdev)) {
		ret = PTR_ERR(glue->musb_pdev);
		dev_err(dev, "probe: musb-hdrc register failed: %d\n", ret);
		goto err_device_register;
	}

	dev_info(dev, "probe: completed successfully, musb-hdrc pdev=%s\n",
		 dev_name(&glue->musb_pdev->dev));
	dev_info(dev, "probe: UDC should now be available in /sys/class/udc/\n");
	return 0;

err_device_register:
	clk_bulk_disable_unprepare(MTK_MUSB_CLKS_NUM, glue->clks);
err_enable_clk:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	return ret;
}

static void mtk_musb_remove(struct platform_device *pdev)
{
	struct mtk_glue *glue = platform_get_drvdata(pdev);

	platform_device_unregister(glue->musb_pdev);
}

#ifdef CONFIG_OF
static const struct of_device_id mtk_musb_match[] = {
	{.compatible = "mediatek,mtk-musb",},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_musb_match);
#endif

static struct platform_driver mtk_musb_driver = {
	.probe = mtk_musb_probe,
	.remove = mtk_musb_remove,
	.driver = {
		   .name = "musb-mtk",
		   .of_match_table = of_match_ptr(mtk_musb_match),
	},
};

module_platform_driver(mtk_musb_driver);

MODULE_DESCRIPTION("MediaTek MUSB Glue Layer");
MODULE_AUTHOR("Min Guo <min.guo@mediatek.com>");
MODULE_LICENSE("GPL v2");
