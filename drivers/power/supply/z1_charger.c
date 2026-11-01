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
 *
 * Charging control (2026-11-01, vendor-extracted bit map, no guessed bits)
 * -------------------------------------------------------------------
 * The Z1 charger is the MT6323 built-in linear charger; charging only ever
 * happened in LK power-off mode before this.  The enable sequence and every
 * register/bit below are copied from the vendor 3.4.67 MT6572 kernel
 * charging_hw_pmic.c (archived: mainline_recon/vendor_ref_mt6323_charger/,
 * source Intelinsidecom/kernel_ckpt72_w_kk-mt6572 branch main):
 *
 *   charging_enable(TRUE):  CSDAC_DLY=4, CSDAC_STP=1 (CHR_CON21),
 *     CSDAC_STP_INC=1, CSDAC_STP_DEC=2 (CHR_CON20), CS_EN (CHR_CON2.3),
 *     HWCV_EN (CHR_CON23.6), VBAT_CV_EN (CHR_CON2.1), CSDAC_EN (CHR_CON0.3),
 *     CHR_EN (CHR_CON0.4, master), CV=4.2V (CHR_CON3 VBAT_CV_VTH=0),
 *     CC=450mA (CHR_CON4 CS_VTH=12; vendor table index 12).
 *   CHRWDT: CHR_CON13 TD[3:0]=0 (4 s), EN.4, WR.8; CHR_CON15 INT_EN.0,
 *     FLAG_WR.1.  The watchdog must be re-fed faster than 4 s or the
 *     hardware cuts charging by itself -- the 2 s poll work is the feed
 *     heartbeat (2x margin), so driver death also stops charging (safe).
 *   HW init (vendor charging_hw_init): VBAT_OV_EN + VBAT_OV_VTH=1 (4.3 V)
 *     CHR_CON6, VCDT_HV_EN CHR_CON1.0, USBDL force-exit CHR_CON16.2/.3,
 *     BC11 reset CHR_CON18.0/.1, CSDAC_MODE CHR_CON23.2, BATON_EN CHR_CON7.0,
 *     ULC_DET_EN CHR_CON23.7, LOW_ICH_DB CHR_CON22[5:0]=1.
 *   disable (vendor FALSE branch): CHR_CON15 INT_EN/FLAG_WR=0,
 *     CHR_CON13 EN=0, CHR_CON0 CSDAC_EN/CHR_EN=0, CHR_CON23 HWCV_EN=0.
 *
 * Safety rails: CV 4.2 V + HW VBAT_OV 4.3 V + WDT kept armed + 450 mA
 * start current (USB-port conservative) + software temperature gate: the
 * battery psy ("battery", generic-adc-battery) must report 0..50 degC,
 * and a missing/unreadable temperature means NOT charging.  Suspend will
 * stop the feed and thus charging after 4 s -- acceptable for now, revisit
 * if autosuspend ever bites on this device.
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

/* Charger control bits, vendor upmu_hw.h (see header comment).  Register
 * addresses are MT6323_CHR_CONn from <linux/mfd/mt6323/registers.h>. */
#define Z1_CHR_CON0_VCDT_HV_EN		BIT(0)
#define Z1_CHR_CON0_CSDAC_EN		BIT(3)
#define Z1_CHR_CON0_CHR_EN		BIT(4)
#define Z1_CHR_CON1_VCDT_HV_EN		BIT(0)
#define Z1_CHR_CON2_VBAT_CV_EN		BIT(1)
#define Z1_CHR_CON2_CS_EN		BIT(3)
#define Z1_CHR_CON3_VBAT_CV_VTH_MASK	0x1f	/* [4:0], idx0 = 4.2 V */
#define Z1_CHR_CON4_CS_VTH_MASK		0xf	/* [3:0], idx12 = 450 mA */
#define Z1_CHR_CON6_VBAT_OV_EN		BIT(0)
#define Z1_CHR_CON6_VBAT_OV_VTH_MASK	0x7	/* [3:1], 1 = 4.3 V */
#define Z1_CHR_CON6_VBAT_OV_VTH_SHIFT	1
#define Z1_CHR_CON7_BATON_EN		BIT(0)
#define Z1_CHR_CON10_PCHR_FLAG_EN	BIT(4)
#define Z1_CHR_CON13_CHRWDT_TD_MASK	0xf	/* [3:0], 0 = 4 s */
#define Z1_CHR_CON13_CHRWDT_EN		BIT(4)
#define Z1_CHR_CON13_CHRWDT_WR		BIT(8)
#define Z1_CHR_CON15_CHRWDT_INT_EN	BIT(0)
#define Z1_CHR_CON15_CHRWDT_FLAG_WR	BIT(1)
#define Z1_CHR_CON16_USBDL_RST		BIT(2)
#define Z1_CHR_CON16_USBDL_SET		BIT(3)
#define Z1_CHR_CON18_BC11_BB_CTRL	BIT(0)
#define Z1_CHR_CON18_BC11_RST		BIT(1)
#define Z1_CHR_CON20_CSDAC_STP_INC_MASK	0x7	/* [2:0] */
#define Z1_CHR_CON20_CSDAC_STP_DEC_MASK	0x7	/* [6:4] */
#define Z1_CHR_CON20_CSDAC_STP_DEC_SHIFT 4
#define Z1_CHR_CON21_CSDAC_DLY_MASK	0x7	/* [2:0] */
#define Z1_CHR_CON21_CSDAC_STP_MASK	0x7	/* [6:4] */
#define Z1_CHR_CON21_CSDAC_STP_SHIFT	4
#define Z1_CHR_CON22_LOW_ICH_DB_MASK	0x3f	/* [5:0] */
#define Z1_CHR_CON23_CSDAC_MODE		BIT(2)
#define Z1_CHR_CON23_HWCV_EN		BIT(6)
#define Z1_CHR_CON23_ULC_DET_EN		BIT(7)

/* Charger setpoints (vendor tables, conservative picks). */
#define Z1_CV_VTH_4V2			0	/* VBAT_CV_VTH idx0 = 4.2 V */
#define Z1_CS_VTH_450MA			12	/* CS_VTH idx12 = 450 mA */
#define Z1_VBAT_OV_VTH_4V3		1	/* vendor: 4.3 V */

/* Battery temperature gate (psy TEMP unit: 0.1 degC).  Outside -> no
 * charge; unreadable temperature also means no charge. */
#define Z1_TEMP_MIN			0	/* 0 degC */
#define Z1_TEMP_MAX			500	/* 50.0 degC */

/* Name of the generic-adc-battery psy on this device (board-verified:
 * /sys/class/power_supply/battery/). */
#define Z1_BATTERY_PSY_NAME		"battery"

#define Z1_CHARGER_POLL_MS		2000

/* AUXADC VCDT: IIO_VOLTAGE processed value is millivolts (the auxadc
 * provider's scale is 1800 mV / 2^15 raw, mt6323-auxadc.c
 * IIO_CHAN_INFO_SCALE); power_supply voltage_now takes uV, same *1000
 * convention as generic-adc-battery. */

struct z1_charger {
	struct device *dev;
	struct regmap *regmap;
	struct power_supply *psy;
	struct iio_channel *chan_vcdt;
	struct power_supply *bat_psy;	/* "battery", for the temp gate */
	struct delayed_work poll_work;
	bool online;
	bool have_vcdt;
	bool charging;			/* charge enable bits applied */
	/* serialize regmap access from irq thread + poll work */
	struct mutex lock;
};

static const char *const z1_charger_psy_name = "usb"; /* std name so the A9 health HAL isPowered() sees the charger */

static void z1_charger_apply(struct z1_charger *chg, bool online);

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

	/* Charge policy + watchdog heartbeat ride on the same tick as the
	 * detection update (2 s poll, or immediately on the CHRDET IRQ). */
	z1_charger_apply(chg, online);

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

/* Vendor charging_hw_init() (charging_hw_pmic.c:532): watchdog armed,
 * HW overvoltage 4.3 V, charger-input (VCDT) HV detect on, USBDL forced
 * off, BC11 block reset, CSDAC/baton/low-current-debounce defaults. */
static int z1_charger_hw_init(struct z1_charger *chg)
{
	struct regmap *map = chg->regmap;
	int ret;

	ret = regmap_update_bits(map, MT6323_CHR_CON13,
				 Z1_CHR_CON13_CHRWDT_TD_MASK, 0);
	ret |= regmap_update_bits(map, MT6323_CHR_CON13,
				  Z1_CHR_CON13_CHRWDT_EN | Z1_CHR_CON13_CHRWDT_WR,
				  Z1_CHR_CON13_CHRWDT_EN | Z1_CHR_CON13_CHRWDT_WR);
	ret |= regmap_update_bits(map, MT6323_CHR_CON15,
				  Z1_CHR_CON15_CHRWDT_INT_EN,
				  Z1_CHR_CON15_CHRWDT_INT_EN);
	ret |= regmap_update_bits(map, MT6323_CHR_CON6,
				  Z1_CHR_CON6_VBAT_OV_EN |
				  (Z1_CHR_CON6_VBAT_OV_VTH_MASK <<
				   Z1_CHR_CON6_VBAT_OV_VTH_SHIFT),
				  Z1_CHR_CON6_VBAT_OV_EN |
				  (Z1_VBAT_OV_VTH_4V3 <<
				   Z1_CHR_CON6_VBAT_OV_VTH_SHIFT));
	ret |= regmap_update_bits(map, MT6323_CHR_CON1,
				  Z1_CHR_CON1_VCDT_HV_EN,
				  Z1_CHR_CON1_VCDT_HV_EN);
	ret |= regmap_update_bits(map, MT6323_CHR_CON16,
				  Z1_CHR_CON16_USBDL_SET |
				  Z1_CHR_CON16_USBDL_RST,
				  Z1_CHR_CON16_USBDL_RST);
	ret |= regmap_update_bits(map, MT6323_CHR_CON18,
				  Z1_CHR_CON18_BC11_BB_CTRL |
				  Z1_CHR_CON18_BC11_RST,
				  Z1_CHR_CON18_BC11_BB_CTRL |
				  Z1_CHR_CON18_BC11_RST);
	ret |= regmap_update_bits(map, MT6323_CHR_CON23,
				  Z1_CHR_CON23_CSDAC_MODE |
				  Z1_CHR_CON23_ULC_DET_EN,
				  Z1_CHR_CON23_CSDAC_MODE |
				  Z1_CHR_CON23_ULC_DET_EN);
	ret |= regmap_update_bits(map, MT6323_CHR_CON7,
				  Z1_CHR_CON7_BATON_EN,
				  Z1_CHR_CON7_BATON_EN);
	ret |= regmap_update_bits(map, MT6323_CHR_CON22,
				  Z1_CHR_CON22_LOW_ICH_DB_MASK, 1);
	if (ret)
		dev_err(chg->dev, "charger hw_init regmap error: %d\n", ret);
	return ret;
}

/* Vendor charging_enable() TRUE branch + CV/CC setpoints, bit for bit. */
static int z1_charger_enable(struct z1_charger *chg)
{
	struct regmap *map = chg->regmap;
	int ret;

	ret = z1_charger_hw_init(chg);

	ret |= regmap_update_bits(map, MT6323_CHR_CON21,
				  Z1_CHR_CON21_CSDAC_DLY_MASK, 0x4);
	ret |= regmap_update_bits(map, MT6323_CHR_CON21,
				  Z1_CHR_CON21_CSDAC_STP_MASK <<
				  Z1_CHR_CON21_CSDAC_STP_SHIFT,
				  0x1 << Z1_CHR_CON21_CSDAC_STP_SHIFT);
	ret |= regmap_update_bits(map, MT6323_CHR_CON20,
				  Z1_CHR_CON20_CSDAC_STP_INC_MASK, 0x1);
	ret |= regmap_update_bits(map, MT6323_CHR_CON20,
				  Z1_CHR_CON20_CSDAC_STP_DEC_MASK <<
				  Z1_CHR_CON20_CSDAC_STP_DEC_SHIFT,
				  0x2 << Z1_CHR_CON20_CSDAC_STP_DEC_SHIFT);
	ret |= regmap_update_bits(map, MT6323_CHR_CON2,
				  Z1_CHR_CON2_CS_EN, Z1_CHR_CON2_CS_EN);
	ret |= regmap_update_bits(map, MT6323_CHR_CON23,
				  Z1_CHR_CON23_HWCV_EN,
				  Z1_CHR_CON23_HWCV_EN);
	ret |= regmap_update_bits(map, MT6323_CHR_CON2,
				  Z1_CHR_CON2_VBAT_CV_EN,
				  Z1_CHR_CON2_VBAT_CV_EN);
	ret |= regmap_update_bits(map, MT6323_CHR_CON3,
				  Z1_CHR_CON3_VBAT_CV_VTH_MASK,
				  Z1_CV_VTH_4V2);
	ret |= regmap_update_bits(map, MT6323_CHR_CON4,
				  Z1_CHR_CON4_CS_VTH_MASK,
				  Z1_CS_VTH_450MA);
	ret |= regmap_update_bits(map, MT6323_CHR_CON10,
				  Z1_CHR_CON10_PCHR_FLAG_EN,
				  Z1_CHR_CON10_PCHR_FLAG_EN);
	ret |= regmap_update_bits(map, MT6323_CHR_CON0,
				  Z1_CHR_CON0_CSDAC_EN | Z1_CHR_CON0_CHR_EN,
				  Z1_CHR_CON0_CSDAC_EN | Z1_CHR_CON0_CHR_EN);

	if (ret) {
		dev_err(chg->dev, "charge-enable sequence failed: %d\n", ret);
		return ret;
	}

	/* Forensics: dump the state the hardware reports right after the
	 * enable sequence (CHR_CON0 RGS bits + CSDAC DAC value). */
	{
		unsigned int c0, c8;

		if (!regmap_read(map, MT6323_CHR_CON0, &c0) &&
		    !regmap_read(map, MT6323_CHR_CON8, &c8))
			dev_info(chg->dev,
				 "charging ENABLED (CV 4.2V CC 450mA): CHR_CON0=0x%04x CHR_CON8(CSDAC)=0x%04x\n",
				 c0, c8);
	}
	return 0;
}

/* Vendor charging_enable() FALSE branch.  Watchdog off too, so a stray
 * partial enable cannot outlive this call. */
static int z1_charger_disable(struct z1_charger *chg)
{
	struct regmap *map = chg->regmap;
	int ret;

	ret  = regmap_update_bits(map, MT6323_CHR_CON15,
				  Z1_CHR_CON15_CHRWDT_INT_EN |
				  Z1_CHR_CON15_CHRWDT_FLAG_WR, 0);
	ret |= regmap_update_bits(map, MT6323_CHR_CON13,
				  Z1_CHR_CON13_CHRWDT_EN, 0);
	ret |= regmap_update_bits(map, MT6323_CHR_CON0,
				  Z1_CHR_CON0_CSDAC_EN | Z1_CHR_CON0_CHR_EN, 0);
	ret |= regmap_update_bits(map, MT6323_CHR_CON23,
				  Z1_CHR_CON23_HWCV_EN, 0);
	if (ret)
		dev_err(chg->dev, "charge-disable sequence failed: %d\n", ret);
	else
		dev_info(chg->dev, "charging DISABLED\n");
	return ret;
}

/* Vendor charging_reset_watch_dog_timer(): re-arm the 4 s watchdog.  The
 * poll work calls this every 2 s while charging. */
static int z1_charger_feed_wdt(struct z1_charger *chg)
{
	struct regmap *map = chg->regmap;
	int ret;

	ret  = regmap_update_bits(map, MT6323_CHR_CON13,
				  Z1_CHR_CON13_CHRWDT_TD_MASK, 0);
	ret |= regmap_update_bits(map, MT6323_CHR_CON13,
				  Z1_CHR_CON13_CHRWDT_WR |
				  Z1_CHR_CON13_CHRWDT_EN,
				  Z1_CHR_CON13_CHRWDT_WR |
				  Z1_CHR_CON13_CHRWDT_EN);
	ret |= regmap_update_bits(map, MT6323_CHR_CON15,
				  Z1_CHR_CON15_CHRWDT_INT_EN,
				  Z1_CHR_CON15_CHRWDT_INT_EN);
	ret |= regmap_update_bits(map, MT6323_CHR_CON15,
				  Z1_CHR_CON15_CHRWDT_FLAG_WR,
				  Z1_CHR_CON15_CHRWDT_FLAG_WR);
	return ret;
}

/* Temperature gate against the battery psy.  Returns 1 if charging is
 * allowed, 0 otherwise (missing psy, missing TEMP property, or out of
 * 0..50 degC).  Missing/unreadable temperature is fail-safe: no charge. */
static bool z1_charger_temp_ok(struct z1_charger *chg, int *temp)
{
	union power_supply_propval val;
	int ret;

	if (!chg->bat_psy)
		return false;
	ret = power_supply_get_property(chg->bat_psy,
					POWER_SUPPLY_PROP_TEMP, &val);
	if (ret)
		return false;
	*temp = val.intval;
	return *temp >= Z1_TEMP_MIN && *temp <= Z1_TEMP_MAX;
}

/* Called with chg->lock held, once per poll tick (2 s).  Applies the
 * charge policy: enable only with charger present AND battery temperature
 * in window; re-feed the watchdog while charging; take the vendor disable
 * path when the charger leaves or the gate says no. */
static void z1_charger_apply(struct z1_charger *chg, bool online)
{
	int temp = INT_MIN;

	if (!online) {
		if (chg->charging) {
			z1_charger_disable(chg);
			chg->charging = false;
		}
		return;
	}

	if (!chg->bat_psy) {
		chg->bat_psy = power_supply_get_by_name(Z1_BATTERY_PSY_NAME);
		if (!chg->bat_psy)
			return; /* battery driver not up yet: stay off */
	}

	if (!z1_charger_temp_ok(chg, &temp)) {
		if (chg->charging) {
			dev_warn(chg->dev,
				 "battery temp %d (0.1C) out of gate, stopping charge\n",
				 temp);
			z1_charger_disable(chg);
			chg->charging = false;
		} else if (temp != INT_MIN) {
			dev_warn_ratelimited(chg->dev,
				 "battery temp %d (0.1C) out of gate, not charging\n",
				 temp);
		} else {
			dev_dbg(chg->dev,
				"battery temp unavailable, not charging\n");
		}
		return;
	}

	if (!chg->charging) {
		if (!z1_charger_enable(chg))
			chg->charging = true;
		return;
	}

	/* Already enabled: heartbeat.  If the feed ever fails repeatedly
	 * the 4 s watchdog cuts charging by itself (safe direction). */
	if (z1_charger_feed_wdt(chg))
		dev_err_ratelimited(chg->dev, "watchdog feed failed\n");
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

static void z1_charger_remove(struct platform_device *pdev)
{
	struct z1_charger *chg = platform_get_drvdata(pdev);

	/* The devm-managed poll work is cancelled after .remove; stop it
	 * here first so it cannot re-enable charging behind our back, then
	 * take the vendor disable path.  If any of this never runs, the
	 * still-armed 4 s watchdog cuts charging on its own. */
	cancel_delayed_work_sync(&chg->poll_work);
	mutex_lock(&chg->lock);
	z1_charger_disable(chg);
	chg->charging = false;
	mutex_unlock(&chg->lock);
	if (chg->bat_psy)
		power_supply_put(chg->bat_psy);
}

static struct platform_driver z1_charger_driver = {
	.driver = {
		.name = "z1-charger",
		.of_match_table = z1_charger_of_match,
	},
	.probe = z1_charger_probe,
	.remove = z1_charger_remove,
};
module_platform_driver(z1_charger_driver);

MODULE_AUTHOR("Z1 mainline bring-up");
MODULE_DESCRIPTION("MT6323 USB charger detect + charge enable (vendor-extracted register map) for Z1");
MODULE_LICENSE("GPL");
