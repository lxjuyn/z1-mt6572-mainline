// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Device Tree support for Mediatek SoCs
 *
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Matthias Brugger <matthias.bgg@gmail.com>
 */
#include <linux/init.h>
#include <linux/io.h>
#include <asm/mach/arch.h>
#include <linux/of.h>
#include <linux/of_clk.h>
#include <linux/clocksource.h>


#define GPT6_CON_MT65xx 0x10008060
#define GPT_ENABLE      0x31

/*
 * MT6572 RGU watchdog: WDT_MODE @0x10007000+0x000.
 * bit0 = WDT_EN (enable). Writes to WDT_MODE must OR in WDT_MODE_KEY (0x22000000)
 * or the hardware silently drops the write (MTK anti-glitch protection).
 * LK/preloader enables the watchdog before jumping to the kernel and hands it
 * to us with an ~8s window. The mtk_wdt platform driver (drivers/watchdog/mtk_wdt.c)
 * only takes over at device_initcall time, which on this slow mainline bring-up can
 * land later than the 8s tick, causing a silent reboot loop ("0 output + WDT reset").
 * Stop the dog here, before of_clk_init, so the kernel has indefinite time to reach
 * the driver probe. When mtk_wdt probes it re-enables and pings per the dts
 * timeout-sec=15; this early stop is a pure safety net.
 */
#define MT6572_WDT_BASE   0x10007000
#define WDT_MODE_KEY      0x22000000
#define WDT_MODE_EN       0x00000001

static void __init mediatek_disable_wdt_early(void)
{
	void __iomem *wdt;
	u32 mode;

	/* Only the SoC that actually has this watchdog base needs this. */
	if (!of_machine_is_compatible("mediatek,mt6572"))
		return;

	wdt = ioremap(MT6572_WDT_BASE, 0x100);
	if (!wdt)
		return;

	mode = readl(wdt);                 /* WDT_MODE reg @ offset 0 */
	writel((mode & ~WDT_MODE_EN) | WDT_MODE_KEY, wdt);
	iounmap(wdt);
}

static void __init mediatek_timer_init(void)
{
	void __iomem *gpt_base;

	/* Stop the preloader/LK watchdog ASAP so it can't bite us before mtk_wdt probes. */
	mediatek_disable_wdt_early();

	if (of_machine_is_compatible("mediatek,mt6589") ||
	    of_machine_is_compatible("mediatek,mt7623") ||
	    of_machine_is_compatible("mediatek,mt8135") ||
	    of_machine_is_compatible("mediatek,mt8127")) {
		/* turn on GPT6 which ungates arch timer clocks */
		gpt_base = ioremap(GPT6_CON_MT65xx, 0x04);

		/* enable clock and set to free-run */
		writel(GPT_ENABLE, gpt_base);
		iounmap(gpt_base);
	}

	of_clk_init(NULL);
	timer_probe();
};

static const char * const mediatek_board_dt_compat[] = {
	"mediatek,mt2701",
	"mediatek,mt6572",
	"mediatek,mt6582",
	"mediatek,mt6589",
	"mediatek,mt6592",
	"mediatek,mt7623",
	"mediatek,mt7629",
	"mediatek,mt8127",
	"mediatek,mt8135",
	NULL,
};

DT_MACHINE_START(MEDIATEK_DT, "MediaTek Cortex-A7 (Device Tree)")
	.dt_compat	= mediatek_board_dt_compat,
	.init_time	= mediatek_timer_init,
MACHINE_END
