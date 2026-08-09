// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MT6572 integrated CONNSYS platform-layer driver.
 *
 * The JBO Z1 (MT6572) uses the SoC-integrated CONNSYS connectivity block
 * (WiFi + BT), not the external MT6620 SDIO combo chip.  This driver is the
 * mainline platform layer that implements the vendor CONNSYS power-on
 * sequence through mainline APIs:
 *
 *   (a) MT6323 VCN18 / VCN28 regulators
 *   (b) the MT6572 SCPSYS "conn" power domain (genpd via pm_runtime)
 *   (c) the CONN MCU config base @0x18070000, polling the chip ID at +0x8
 *       (expects 0x6572 / 0x6582)
 *   (d) the CONNSYS shared EMI carveout @0x80080000 (343 KiB), zeroed
 *       before any firmware is loaded
 *
 * This is intentionally a minimal, compile-correct skeleton.  It does NOT yet
 * implement the WMT/STP transport, BTIF, the WLAN AHB HIF @0x180f0000 (GIC
 * SPI 123), IRQ 122/124, firmware download, or the CONN TOP CR @0x180b0000
 * clock gating.
 *
 * TODO(board bring-up, in this order):
 *   - Add/verify the DT node that binds this driver, e.g.:
 *       connsys@18070000 {
 *           compatible = "mediatek,mt6572-connsys";
 *           reg = <0x18070000 0x1000>, <0x80080000 0x55c00>;
 *           power-domains = <&spm MT6572_POWER_DOMAIN_CONN>;
 *           vcn18-supply = <&mt6323_vcn18_reg>;
 *           vcn28-supply = <&mt6323_vcn28_reg>;
 *           status = "disabled";   // enable only after gated board test
 *       };
 *   - After the chip-ID poll passes, add the WLAN AHB HIF @0x180f0000 +
 *     IRQ 123 in a later WLAN patch.  (The TOPCKGEN bit26 @0x10000084 CONN
 *     clock gating TODO below is implemented in this file: see
 *     mt6572_connsys_clk_gate_enable().)
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/sizes.h>

#define MT6572_CONN_MCU_CONFIG_BASE	0x18070000
#define MT6572_CONN_MCU_CONFIG_SIZE	SZ_4K	/* 0x1000 */

#define MT6572_CONNSYS_CHIP_ID_OFF	0x8
#define MT6572_CONNSYS_ID_MT6572	0x6572
#define MT6572_CONNSYS_ID_MT6582	0x6582
#define MT6572_CONNSYS_ID_POLL_RETRIES	100
#define MT6572_CONNSYS_ID_POLL_US	100

/* CONNSYS firmware shared EMI, inside the 1 MiB carveout @0x80000000. */
#define MT6572_CONN_SHARED_EMI_BASE	0x80080000
#define MT6572_CONN_SHARED_EMI_SIZE	0x55c00	/* 343 KiB */

/*
 * CONN clock gating.  The vendor MT6572 WMT flow (mtk_wcn_consys_hw_reg_ctrl)
 * opens the CONN clocks before conn_power_on() with these two writes:
 *
 *   TOPCKGEN+0x84 |= BIT(26)      (CONSYS_TOP_CLKCG_CLR_REG)
 *   SPM+0x00        = 0x0b160001  (CONSYS_PWRON_CONFG_EN_REG)
 *
 * TOPCKGEN+0x84 is the TOP1 clock-gate "clear" register.  Bit 26 is the SPM
 * clock gate: mainline clk-mt6572-topckgen.c models it as CLK_TOP_SPM
 * (GATE_TOP1, sta 0x24 / set 0x54 / clr 0x84, mtk_clk_gate_ops_setclr), so
 * un-gating is a single write of BIT(26) to the clr offset.  Without it the
 * CONN SCPSYS power-on and the CONN MCU (chip-ID poll @0x18070008) have no
 * clock and the poll times out.
 *
 * The second write enables SPM "power-on config" control (project key 0x0b16
 * | BIT(0) == 0x0b160001) -- the vendor precedes every MTCMOS power-on with
 * it so the CONN power is software-controllable.
 */
#define MT6572_CONN_TOP1_CG_CLR		0x84
#define MT6572_CONN_TOP1_CG_SPM_BIT	BIT(26)

#define MT6572_CONN_SPM_PWRON_CONFIG	0x00
#define MT6572_CONN_SPM_PWRON_CONFIG_VAL	(0x0b16 << 16 | BIT(0))

struct mt6572_connsys {
	struct device *dev;
	void __iomem *mcu_config;
	void __iomem *shared_emi;
	struct regulator *vcn18;
	struct regulator *vcn28;
};

/*
 * Vendor flow polls CONN MCU config +0x8 for the chip ID after the power
 * rails and the SPM power-on are asserted.  Poll with a bounded timeout so
 * a dead CONNSYS cannot wedge the boot.
 */
static int mt6572_connsys_wait_chip_id(struct mt6572_connsys *cs, u32 *chip_id)
{
	u32 id;
	unsigned int i;

	for (i = 0; i < MT6572_CONNSYS_ID_POLL_RETRIES; i++) {
		id = readl_relaxed(cs->mcu_config + MT6572_CONNSYS_CHIP_ID_OFF);
		if (id == MT6572_CONNSYS_ID_MT6572 ||
		    id == MT6572_CONNSYS_ID_MT6582) {
			*chip_id = id;
			return 0;
		}
		usleep_range(MT6572_CONNSYS_ID_POLL_US,
			     2 * MT6572_CONNSYS_ID_POLL_US);
	}

	dev_err(cs->dev, "CONNSYS chip ID poll timed out (last %#x)\n", id);
	return -ETIMEDOUT;
}

/*
 * Open the CONN clock gates (see the defines above).  Both TOPCKGEN and the
 * SPM block are "syscon" nodes in the MT6572 dtsi, so this uses the regmap
 * API instead of raw ioremap.
 *
 * Failures are non-fatal: LK/preloader configures TOPCKGEN + SPM before
 * jumping to the kernel, so the clocks may already be open.  The chip-ID
 * poll is the real test -- a timeout after a clock-gate warning confirms the
 * gate was the missing step.
 */
static void mt6572_connsys_clk_gate_enable(struct mt6572_connsys *cs)
{
	struct regmap *topckgen;
	struct regmap *spm;
	int ret;

	topckgen = syscon_regmap_lookup_by_compatible("mediatek,mt6572-topckgen");
	if (IS_ERR(topckgen)) {
		dev_warn(cs->dev, "no topckgen syscon (%ld), CONN clock may stay gated\n",
			 PTR_ERR(topckgen));
		return;
	}

	/* Un-gate the SPM clock: clear TOP1 CG bit 26 via the clr register. */
	ret = regmap_write(topckgen, MT6572_CONN_TOP1_CG_CLR,
			   MT6572_CONN_TOP1_CG_SPM_BIT);
	if (ret)
		dev_warn(cs->dev, "failed to clear TOPCKGEN CG bit 26: %d\n", ret);

	spm = syscon_regmap_lookup_by_compatible("mediatek,mt6572-scpsys");
	if (IS_ERR(spm)) {
		dev_warn(cs->dev, "no scpsys syscon (%ld), SPM power-on config not set\n",
			 PTR_ERR(spm));
		return;
	}

	ret = regmap_write(spm, MT6572_CONN_SPM_PWRON_CONFIG,
			   MT6572_CONN_SPM_PWRON_CONFIG_VAL);
	if (ret)
		dev_warn(cs->dev, "failed to set SPM power-on config: %d\n", ret);
}

static int mt6572_connsys_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mt6572_connsys *cs;
	u32 chip_id;
	int ret;

	cs = devm_kzalloc(dev, sizeof(*cs), GFP_KERNEL);
	if (!cs)
		return -ENOMEM;
	cs->dev = dev;

	/* (a) VCN18 / VCN28 regulators (MT6323 PMIC LDOs). */
	cs->vcn18 = devm_regulator_get(dev, "vcn18");
	if (IS_ERR(cs->vcn18))
		return dev_err_probe(dev, PTR_ERR(cs->vcn18),
				     "missing VCN18 supply\n");

	cs->vcn28 = devm_regulator_get(dev, "vcn28");
	if (IS_ERR(cs->vcn28))
		return dev_err_probe(dev, PTR_ERR(cs->vcn28),
				     "missing VCN28 supply\n");

	ret = regulator_enable(cs->vcn18);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable VCN18\n");

	ret = regulator_enable(cs->vcn28);
	if (ret) {
		dev_err_probe(dev, ret, "failed to enable VCN28\n");
		goto err_vcn18;
	}

	/*
	 * (a2) CONN clock gates.  Must be open before the CONN genpd is
	 * powered on (the SPM clock bit) and before the chip-ID poll; see
	 * mt6572_connsys_clk_gate_enable().  Non-fatal: LK may have opened
	 * them already.
	 */
	mt6572_connsys_clk_gate_enable(cs);

	/*
	 * (b) CONN SCPSYS power domain.
	 *
	 * platform_probe() already calls dev_pm_domain_attach() with
	 * PD_FLAG_ATTACH_POWER_ON for devices that carry a DT
	 * power-domains phandle, so this is normally a no-op.  It is kept
	 * explicit so the vendor sequence reads "regulators -> genpd" and
	 * so the driver stays self-contained if the bus attachment policy
	 * ever changes.
	 */
	ret = dev_pm_domain_attach(dev, PD_FLAG_ATTACH_POWER_ON |
				   PD_FLAG_DETACH_POWER_OFF);
	if (ret)
		goto err_vcn28;

	/*
	 * Runtime PM is not enabled automatically for platform devices in
	 * this kernel, so enable it here (same pattern as mtk-sd).  The
	 * pm_runtime_get_sync() below is what actually powers on the "conn"
	 * genpd for the rest of the probe.
	 */
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err_probe(dev, ret, "failed to power on CONN PM domain\n");
		pm_runtime_put_sync(dev);	/* balance RPM_GET_PUT ref */
		goto err_pm_runtime;
	}

	/* (c) CONN MCU config: read and poll the chip ID. */
	cs->mcu_config = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(cs->mcu_config)) {
		ret = PTR_ERR(cs->mcu_config);
		goto err_pm_put;
	}

	ret = mt6572_connsys_wait_chip_id(cs, &chip_id);
	if (ret < 0)
		goto err_pm_put;
	dev_info(dev, "CONNSYS up: chip ID %#x (expected 0x6572/0x6582)\n",
		 chip_id);

	/* (d) Clear the CONNSYS firmware shared EMI (343 KiB). */
	cs->shared_emi = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(cs->shared_emi)) {
		ret = PTR_ERR(cs->shared_emi);
		goto err_pm_put;
	}

	memset_io(cs->shared_emi, 0, MT6572_CONN_SHARED_EMI_SIZE);
	dev_info(dev, "CONNSYS shared EMI cleared (%#x bytes @%px)\n",
		 MT6572_CONN_SHARED_EMI_SIZE, cs->shared_emi);

	/*
	 * Keep the regulators and the CONN genpd powered while this driver is
	 * bound: it is the platform owner of the connectivity block that the
	 * WMT/BTIF/WLAN layers are expected to attach to later.
	 */
	platform_set_drvdata(pdev, cs);

	return 0;

err_pm_put:
	pm_runtime_put_sync(dev);
err_pm_runtime:
	pm_runtime_disable(dev);
err_vcn28:
	regulator_disable(cs->vcn28);
err_vcn18:
	regulator_disable(cs->vcn18);
	return ret;
}

static void mt6572_connsys_remove(struct platform_device *pdev)
{
	struct mt6572_connsys *cs = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	regulator_disable(cs->vcn28);
	regulator_disable(cs->vcn18);

	/*
	 * The platform bus core detaches the PM domain on unbind using the
	 * PD_FLAG_DETACH_POWER_OFF flag it used at attach, so no explicit
	 * dev_pm_domain_detach() is needed here.
	 */
}

static const struct of_device_id mt6572_connsys_of_match[] = {
	{ .compatible = "mediatek,mt6572-connsys" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6572_connsys_of_match);

static struct platform_driver mt6572_connsys_driver = {
	.probe = mt6572_connsys_probe,
	.remove = mt6572_connsys_remove,
	.driver = {
		.name = "mt6572-connsys",
		.of_match_table = mt6572_connsys_of_match,
	},
};
module_platform_driver(mt6572_connsys_driver);

MODULE_DESCRIPTION("MediaTek MT6572 integrated CONNSYS platform driver");
MODULE_LICENSE("GPL");
