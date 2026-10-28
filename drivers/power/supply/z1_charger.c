// SPDX-License-Identifier: GPL-2.0-only
/*
 * z1_charger.c - Z1 (MT6572 + MT6323) USB charger presence via power_supply
 *
 * Problem this solves
 * -------------------
 * Android 9 health HAL (android.hardware.health@2.0-service, AOSP
 * healthd/BatteryMonitor) classifies each /sys/class/power_supply entry by its
 * "type" file: entries of type "USB" contribute their "online" to
 * BatteryProperties.chargerUsbOnline.  The Z1 tree only exposes the
 * generic-adc-battery node (type "Battery"), so the USB online link is
 * broken: the framework never sees the charger.  This driver registers a
 * small power_supply of type USB whose "online" mirrors the MT6323 hardware
 * charger-detect status bit.
 *
 * Detection source (reverse-engineered from the vendor 3.4.67 kernel,
 * drsami/mt6572_kernel, same PMIC as Z1):
 *   - The charger-present read-only status bit is RGS_CHRDET:
 *     CHR_CON0 (0x0000) bit 5.  Vendor code path:
 *       IRQ10 CHRDET -> chrdet_int_handler()          pmic_mt6323.c:956
 *         -> do_chrdet_int_task()                     battery_common.c:2019
 *           -> upmu_is_chr_det()                      battery_common.c:242
 *             -> charging_get_chrdet_status()         charging_hw_pmic.c:656
 *               -> upmu_get_rgs_chrdet()              upmu_common.c:77
 *                  = pmic_read_interface(CHR_CON0, PMIC_RGS_CHRDET)
 *     with PMIC_RGS_CHRDET_MASK=0x1 SHIFT=5           upmu_hw.h:414-415
 *   - CHRSTATUS (0x0142) is NOT the charger-detect register.  Its bit 4 is
 *     PCHR_CHRDET (precharger module view), bit 3 VBAT_OV, bit 5
 *     RO_BATON_UNDET, bits 1/2 power/home-key debounce (upmu_common.c
 *     CHRSTATUS block).  It is dumped at probe for forensics only.
 *   - VCDT (AUXADC channel 4 -> internal register AUXADC_ADC2) is the
 *     charger-input voltage tap; the mainline mt6323-auxadc driver exposes
 *     it as the iio channel with scale 1800 mV / 2^15 (mt6323-auxadc.c,
 *     IIO_CHAN_INFO_SCALE, mult=1 branch).  We expose that raw reading as
 *     voltage_now (millivolts * 1000 = uV as per power_supply ABI).  NOTE:
 *     this is the voltage at the VCDT tap (after the PMIC-internal
 *     divider), not a calibrated VBUS voltage; the VBUS:VCDT ratio is to be
 *     measured on hardware and documented in the stage notes.
 *
 * Event sources, in order of preference:
 *   1. PMIC IRQ 10 (MT6323_IRQ_STATUS_CHRDET) if the DT node carries an
 *      interrupts property; the mt6397 MFD irq domain is linear and its
 *      default xlate takes specifier[0] as hwirq, so <10 IRQ_TYPE_LEVEL_HIGH>
 *      resolves to hwirq 10.
 *   2. A 2 Hz polling work as a belt-and-braces fallback (the RGS bit is
 *      live hardware comparator output; polling alone still yields correct
 *      online state, just with up to ~2 s latency).
 */

#include <linux/workqueue.h>
#include <linux/devm-helpers.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/mfd/mt6323/registers.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>

/*
 * CHR_CON0 (MT6323_CHR_CON0 == 0x0000, registers.h:10) bit fields per the
 * vendor 3.4.67 kernel (upmu_hw.h:404-419):
 *   [0]    RG_VCDT_HV_EN
 *   [1]    RGS_CHR_LDO_DET
 *   [2]    RG_PCHR_AUTOMODE
 *   [3]    RG_CSDAC_EN
 *   [4]    RG_CHR_EN
 *   [5]    RGS_CHRDET     <- charger present, read-only, used here
 *   [6]    RGS_VCDT_LV_DET
 *   [7]    RGS_VCDT_HV_DET
 */
#define Z1_CHR_CON0_RGS_CHRDET_SHIFT	5

/* AUXADC VCDT: IIO_VOLTAGE processed value is millivolts (the auxadc
 * provider's scale is 1800 mV / 2^15 raw, mt6323-auxadc.c
 * IIO_CHAN_INFO_SCALE); power_supply voltage_now takes uV, same *1000
 * convention as generic-adc-battery. */

#define Z1_CHARGER_POLL_MS		2000

struct z1_charger {
	struct device *dev;
	struct regmap *regmap;
	struct power_supply *psy;
	struct iio_channel *chan_vcdt;
	struct delayed_work poll_work;
	bool online;
	bool have_vcdt;
	/* serialize regmap access from irq thread + poll work */
	struct mutex lock;
};

static const char *const z1_charger_psy_name = "z1_charger";

static int z1_charger_read_online(struct z1_charger *chg, bool *online)
{
	unsigned int val;
	int ret;

	ret = regmap_read(chg->regmap, MT6323_CHR_CON0, &val);
	if (ret)
		return ret;

	*online = !!(val & BIT(Z1_CHR_CON0_RGS_CHRDET_SHIFT));
	return 0;
}

static void z1_charger_update(struct z1_charger *chg)
{
	bool online;
	int ret;

	ret = z1_charger_read_online(chg, &online);
	if (ret) {
		dev_err(chg->dev, "RGS_CHRDET read failed: %d\n", ret);
		return;
	}

	if (online != chg->online) {
		dev_info(chg->dev, "charger %s (RGS_CHRDET CHR_CON0[5] -> %d)\n",
			 online ? "plugged" : "unplugged", online);
		chg->online = online;
		power_supply_changed(chg->psy);
	}
}

static int z1_charger_read_vcdt_mv(struct z1_charger *chg, int *mv)
{
	/* iio_read_channel_processed applies the provider scale: for the
	 * mt6323 auxadc VCDT channel this is raw * 1800 mV / 2^15. */
	return iio_read_channel_processed(chg->chan_vcdt, mv);
}

static int z1_charger_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct z1_charger *chg = power_supply_get_drvdata(psy);
	bool online = chg->online;
	int mv;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		/* Read the live bit, fall back to the cached state on error
		 * so the framework still gets a sane answer off the poll
		 * work. */
		mutex_lock(&chg->lock);
		ret = z1_charger_read_online(chg, &online);
		mutex_unlock(&chg->lock);
		if (ret) {
			dev_err_once(chg->dev,
				     "RGS_CHRDET read failed: %d (cached %d)\n",
				     ret, chg->online);
			online = chg->online;
		}
		val->intval = online ? 1 : 0;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (!chg->have_vcdt)
			return -ENODATA;
		mutex_lock(&chg->lock);
		ret = z1_charger_read_vcdt_mv(chg, &mv);
		mutex_unlock(&chg->lock);
		if (ret)
			return ret;
		/* power_supply ABI: uV. Value is the VCDT tap voltage. */
		val->intval = (s64)mv * 1000;
		return 0;
	default:
		return -EINVAL;
	}
}

static const enum power_supply_property z1_charger_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static const struct power_supply_desc z1_charger_desc = {
	.name		= z1_charger_psy_name,
	.type		= POWER_SUPPLY_TYPE_USB,
	.properties	= z1_charger_props,
	.num_properties	= ARRAY_SIZE(z1_charger_props),
	.get_property	= z1_charger_get_property,
};

static void z1_charger_poll_work(struct work_struct *work)
{
	struct z1_charger *chg = container_of(to_delayed_work(work),
					      struct z1_charger, poll_work);

	mutex_lock(&chg->lock);
	z1_charger_update(chg);
	mutex_unlock(&chg->lock);
	schedule_delayed_work(&chg->poll_work,
			      msecs_to_jiffies(Z1_CHARGER_POLL_MS));
}

static irqreturn_t z1_charger_irq_thread(int irq, void *data)
{
	struct z1_charger *chg = data;

	mutex_lock(&chg->lock);
	z1_charger_update(chg);
	mutex_unlock(&chg->lock);

	return IRQ_HANDLED;
}

static int z1_charger_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct z1_charger *chg;
	struct power_supply_config cfg = {};
	unsigned int dump0, dump142;
	unsigned int irq_flags;
	int irq, ret;

	chg = devm_kzalloc(dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;

	chg->dev = dev;
	mutex_init(&chg->lock);
	platform_set_drvdata(pdev, chg);

	/* Same regmap chain as mt6323-auxadc: MFD child -> parent is the
	 * mt6397-pmic platform device whose regmap is the pwrap map. */
	chg->regmap = dev_get_regmap(dev->parent->parent, NULL);
	if (!chg->regmap)
		return dev_err_probe(dev, -ENODEV, "failed to get PMIC regmap\n");

	/* VCDT AUXADC channel is optional: online works without it and only
	 * POWER_SUPPLY_PROP_VOLTAGE_NOW is lost.  Defer properly if the
	 * auxadc provider is not registered yet. */
	chg->chan_vcdt = devm_iio_channel_get(dev, "vcdt");
	if (IS_ERR(chg->chan_vcdt)) {
		ret = PTR_ERR(chg->chan_vcdt);
		if (ret == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		chg->chan_vcdt = NULL;
		dev_info(dev, "no VCDT iio channel (%d), online-only\n", ret);
	} else {
		chg->have_vcdt = true;
	}

	cfg.drv_data = chg;
	chg->psy = devm_power_supply_register(dev, &z1_charger_desc, &cfg);
	if (IS_ERR(chg->psy))
		return dev_err_probe(dev, PTR_ERR(chg->psy),
				     "failed to register USB power_supply\n");

	/* Initial state + one-shot forensics dump (CHRSTATUS bit map: see
	 * driver header comment; bit4 PCHR_CHRDET, bit3 VBAT_OV,
	 * bit5 RO_BATON_UNDET). */
	ret = regmap_read(chg->regmap, MT6323_CHR_CON0, &dump0);
	if (!ret)
		ret = regmap_read(chg->regmap, MT6323_CHRSTATUS, &dump142);
	if (ret)
		return dev_err_probe(dev, ret,
				     "PMIC charger regs unreadable\n");

	chg->online = !!(dump0 & BIT(Z1_CHR_CON0_RGS_CHRDET_SHIFT));
	dev_info(dev,
		 "MT6323 charger detect: CHR_CON0(0x%04x)=0x%04x -> RGS_CHRDET[5]=%d; CHRSTATUS(0x%04x)=0x%04x (PCHR_CHRDET[4]=%d)\n",
		 MT6323_CHR_CON0, dump0, chg->online,
		 MT6323_CHRSTATUS, dump142,
		 !!(dump142 & BIT(4)));

	/* PMIC CHRDET IRQ (hwirq 10) is optional: the mt6397 irq domain has
	 * no explicit xlate so the default takes specifier[0] as hwirq
	 * (kernel/irq/irqdomain.c, irq_domain_xlate fallback). */
	irq = of_irq_get(dev->of_node, 0);
	if (irq > 0) {
		ret = of_property_read_u32_index(dev->of_node, "interrupts",
						 1, &irq_flags);
		if (ret || irq_flags == IRQ_TYPE_NONE)
			irq_flags = IRQ_TYPE_LEVEL_HIGH;

		ret = devm_request_threaded_irq(dev, irq, NULL,
						z1_charger_irq_thread,
						IRQF_ONESHOT | irq_flags,
						"z1-charger", chg);
		if (ret) {
			dev_warn(dev,
				 "CHRDET irq %d request failed: %d, polling only\n",
				 irq, ret);
		} else {
			dev_info(dev, "CHRDET irq %d armed\n", irq);
		}
	} else {
		dev_warn(dev, "no CHRDET irq in DT, polling only\n");
	}

	ret = devm_delayed_work_autocancel(dev, &chg->poll_work,
					   z1_charger_poll_work);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to alloc poll work\n");

	schedule_delayed_work(&chg->poll_work,
			      msecs_to_jiffies(Z1_CHARGER_POLL_MS));

	return 0;
}

static const struct of_device_id z1_charger_of_match[] = {
	{ .compatible = "z1,mt6323-charger" },
	{ }
};
MODULE_DEVICE_TABLE(of, z1_charger_of_match);

static struct platform_driver z1_charger_driver = {
	.driver = {
		.name = "z1-charger",
		.of_match_table = z1_charger_of_match,
	},
	.probe = z1_charger_probe,
};
module_platform_driver(z1_charger_driver);

MODULE_AUTHOR("Z1 mainline bring-up");
MODULE_DESCRIPTION("MT6323 USB charger detection (RGS_CHRDET) for Z1");
MODULE_LICENSE("GPL");
