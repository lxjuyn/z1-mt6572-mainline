// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2019 MediaTek Inc.

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/suspend.h>
#include <linux/mfd/mt6323/core.h>
#include <linux/mfd/mt6323/registers.h>
#include <linux/mfd/mt6328/core.h>
#include <linux/mfd/mt6328/registers.h>
#include <linux/mfd/mt6331/core.h>
#include <linux/mfd/mt6331/registers.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/mfd/mt6397/registers.h>

static void mt6397_irq_lock(struct irq_data *data)
{
	struct mt6397_chip *mt6397 = irq_data_get_irq_chip_data(data);

	mutex_lock(&mt6397->irqlock);
}

static void mt6397_irq_sync_unlock(struct irq_data *data)
{
	struct mt6397_chip *mt6397 = irq_data_get_irq_chip_data(data);

	regmap_write(mt6397->regmap, mt6397->int_con[0],
		     mt6397->irq_masks_cur[0]);
	regmap_write(mt6397->regmap, mt6397->int_con[1],
		     mt6397->irq_masks_cur[1]);
	if (mt6397->int_con[2])
		regmap_write(mt6397->regmap, mt6397->int_con[2],
			     mt6397->irq_masks_cur[2]);

	mutex_unlock(&mt6397->irqlock);
}

static void mt6397_irq_disable(struct irq_data *data)
{
	struct mt6397_chip *mt6397 = irq_data_get_irq_chip_data(data);
	int shift = data->hwirq & 0xf;
	int reg = data->hwirq >> 4;

	mt6397->irq_masks_cur[reg] &= ~BIT(shift);
}

static void mt6397_irq_enable(struct irq_data *data)
{
	struct mt6397_chip *mt6397 = irq_data_get_irq_chip_data(data);
	int shift = data->hwirq & 0xf;
	int reg = data->hwirq >> 4;

	mt6397->irq_masks_cur[reg] |= BIT(shift);
}

static int mt6397_irq_set_wake(struct irq_data *irq_data, unsigned int on)
{
	struct mt6397_chip *mt6397 = irq_data_get_irq_chip_data(irq_data);
	int shift = irq_data->hwirq & 0xf;
	int reg = irq_data->hwirq >> 4;

	if (on)
		mt6397->wake_mask[reg] |= BIT(shift);
	else
		mt6397->wake_mask[reg] &= ~BIT(shift);

	return 0;
}

static struct irq_chip mt6397_irq_chip = {
	.name = "mt6397-irq",
	.irq_bus_lock = mt6397_irq_lock,
	.irq_bus_sync_unlock = mt6397_irq_sync_unlock,
	.irq_enable = mt6397_irq_enable,
	.irq_disable = mt6397_irq_disable,
	.irq_set_wake = pm_sleep_ptr(mt6397_irq_set_wake),
};

static int mt6397_irq_handle_reg(struct mt6397_chip *mt6397, int reg,
				 int irqbase)
{
	unsigned int status = 0;
	int i, irq, ret, handled = 0;

	ret = regmap_read(mt6397->regmap, reg, &status);
	if (ret) {
		dev_err(mt6397->dev, "Failed to read irq status: %d\n", ret);
		return 0;
	}

	for (i = 0; i < 16; i++) {
		if (status & BIT(i)) {
			irq = irq_find_mapping(mt6397->irq_domain, irqbase + i);
			if (irq) {
				handle_nested_irq(irq);
				handled++;
			}
		}
	}

	regmap_write(mt6397->regmap, reg, status);
	return handled;
}

/*
 * Z1: storm guard for the PMIC parent EINT.
 *
 * The MT6323 interrupt line is requested from the mt6572 EINT controller.  If
 * the DT trigger type does not match the line's real electrical behaviour
 * (level vs edge, polarity), the handler is entered with no PMIC status bit
 * set, and for a level-triggered mismatch it is re-entered forever -- a storm
 * that hangs the device.  The polarity cannot be verified without hardware,
 * so count consecutive entries that handled nothing and mask the parent IRQ
 * instead of letting a wrong guess lock the machine.  A bad guess then
 * degrades to "PMIC interrupts do not work", which is the state before the
 * EINT-number fix anyway.
 */
static unsigned int z1_pmic_irq_empty_streak;

static irqreturn_t mt6397_irq_thread(int irq, void *data)
{
	struct mt6397_chip *mt6397 = data;
	int handled;

	handled  = mt6397_irq_handle_reg(mt6397, mt6397->int_status[0], 0);
	handled += mt6397_irq_handle_reg(mt6397, mt6397->int_status[1], 16);
	if (mt6397->int_status[2])
		handled += mt6397_irq_handle_reg(mt6397, mt6397->int_status[2], 32);

	if (handled) {
		z1_pmic_irq_empty_streak = 0;
		return IRQ_HANDLED;
	}

	if (++z1_pmic_irq_empty_streak >= 64) {
		dev_err(mt6397->dev,
			"PMIC IRQ entered %u times with no status bit set; masking the parent IRQ. Check the DT EINT trigger type (level/polarity).\n",
			z1_pmic_irq_empty_streak);
		disable_irq_nosync(irq);
		z1_pmic_irq_empty_streak = 0;
	}

	return IRQ_HANDLED;
}

static int mt6397_irq_domain_map(struct irq_domain *d, unsigned int irq,
				 irq_hw_number_t hw)
{
	struct mt6397_chip *mt6397 = d->host_data;

	irq_set_chip_data(irq, mt6397);
	irq_set_chip_and_handler(irq, &mt6397_irq_chip, handle_level_irq);
	irq_set_nested_thread(irq, 1);
	irq_set_noprobe(irq);

	return 0;
}

static const struct irq_domain_ops mt6397_irq_domain_ops = {
	.map = mt6397_irq_domain_map,
};

static int mt6397_irq_pm_notifier(struct notifier_block *notifier,
				  unsigned long pm_event, void *unused)
{
	struct mt6397_chip *chip =
		container_of(notifier, struct mt6397_chip, pm_nb);

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		regmap_write(chip->regmap,
			     chip->int_con[0], chip->wake_mask[0]);
		regmap_write(chip->regmap,
			     chip->int_con[1], chip->wake_mask[1]);
		if (chip->int_con[2])
			regmap_write(chip->regmap,
				     chip->int_con[2], chip->wake_mask[2]);
		enable_irq_wake(chip->irq);
		break;

	case PM_POST_SUSPEND:
		regmap_write(chip->regmap,
			     chip->int_con[0], chip->irq_masks_cur[0]);
		regmap_write(chip->regmap,
			     chip->int_con[1], chip->irq_masks_cur[1]);
		if (chip->int_con[2])
			regmap_write(chip->regmap,
				     chip->int_con[2], chip->irq_masks_cur[2]);
		disable_irq_wake(chip->irq);
		break;

	default:
		break;
	}

	return NOTIFY_DONE;
}

int mt6397_irq_init(struct mt6397_chip *chip)
{
	int ret;

	mutex_init(&chip->irqlock);

	switch (chip->chip_id) {
	case MT6323_CHIP_ID:
		chip->int_con[0] = MT6323_INT_CON0;
		chip->int_con[1] = MT6323_INT_CON1;
		chip->int_status[0] = MT6323_INT_STATUS0;
		chip->int_status[1] = MT6323_INT_STATUS1;
		break;
	case MT6328_CHIP_ID:
		chip->int_con[0] = MT6328_INT_CON0;
		chip->int_con[1] = MT6328_INT_CON1;
		chip->int_con[2] = MT6328_INT_CON2;
		chip->int_status[0] = MT6328_INT_STATUS0;
		chip->int_status[1] = MT6328_INT_STATUS1;
		chip->int_status[2] = MT6328_INT_STATUS2;
		break;
	case MT6331_CHIP_ID:
		chip->int_con[0] = MT6331_INT_CON0;
		chip->int_con[1] = MT6331_INT_CON1;
		chip->int_status[0] = MT6331_INT_STATUS_CON0;
		chip->int_status[1] = MT6331_INT_STATUS_CON1;
		break;
	case MT6391_CHIP_ID:
	case MT6397_CHIP_ID:
		chip->int_con[0] = MT6397_INT_CON0;
		chip->int_con[1] = MT6397_INT_CON1;
		chip->int_status[0] = MT6397_INT_STATUS0;
		chip->int_status[1] = MT6397_INT_STATUS1;
		break;

	default:
		dev_err(chip->dev, "unsupported chip: 0x%x\n", chip->chip_id);
		return -ENODEV;
	}

	/*
	 * Mask all interrupt sources.
	 *
	 * Z1 diagnostics, deliberately NON-FATAL.
	 *
	 * These are real serial writes to the PMIC through the pwrap regmap.
	 * If the wrapper write path is gated or broken they fail with
	 * -EPERM / -ETIMEDOUT, and upstream discarded the return value, which
	 * made a dead write path look like a successful probe.
	 *
	 * We report each failure loudly, but we do NOT abort the probe: this
	 * irqchip init runs inside the MT6397/MT6323 MFD probe, whose other
	 * cells (regulators, keys, RTC) are what actually bring the board up.
	 * Returning early here would take down the power key, the PMIC
	 * keyboard, the RTC and - critically - the regulators that power the
	 * touch panel, turning a cosmetic warning into a dead device.  So keep
	 * probing like before and let the log carry the diagnosis.
	 */
	ret = regmap_write(chip->regmap, chip->int_con[0], 0x0);
	if (ret)
		dev_err(chip->dev, "failed to mask INT_CON0: %d (continuing)\n", ret);

	ret = regmap_write(chip->regmap, chip->int_con[1], 0x0);
	if (ret)
		dev_err(chip->dev, "failed to mask INT_CON1: %d (continuing)\n", ret);

	if (chip->int_con[2]) {
		ret = regmap_write(chip->regmap, chip->int_con[2], 0x0);
		if (ret)
			dev_err(chip->dev, "failed to mask INT_CON2: %d (continuing)\n", ret);
	}

	/*
	 * Read INT_CON0 back and report it.  A write path that silently drops
	 * writes leaves the bootloader's mask value here instead of 0x0000,
	 * while a working path reads back exactly 0x0000.  This one line tells
	 * us whether a dead power key is a PMIC-bus problem or an interrupt/DT
	 * problem.  A failed readback is likewise non-fatal.
	 */
	{
		unsigned int con0 = ~0U;

		if (regmap_read(chip->regmap, chip->int_con[0], &con0))
			dev_err(chip->dev, "INT_CON0 readback failed (continuing)\n");
		else
			dev_info(chip->dev,
				 "PMIC write path: INT_CON0(0x%04x) readback 0x%04x (expect 0x0000)\n",
				 chip->int_con[0], con0);
	}

	chip->pm_nb.notifier_call = mt6397_irq_pm_notifier;
	chip->irq_domain = irq_domain_create_linear(dev_fwnode(chip->dev), MT6397_IRQ_NR,
						    &mt6397_irq_domain_ops, chip);
	if (!chip->irq_domain) {
		dev_err(chip->dev, "could not create irq domain\n");
		return -ENOMEM;
	}

	/*
	 * Z1: the MT6323 DT interrupt (interrupt-parent = <&pio>,
	 * interrupts = <150 IRQ_TYPE_LEVEL_HIGH>) lives on the mt-eint
	 * irqchip.  mtk_eint_irq_request_resources() must xlate that hwirq
	 * back into a pin via the pinctrl gpio_xlate, and mt6572-pinctrl
	 * cannot: it prints "Can not find pin" and returns -EINVAL, so
	 * request_irq(37) fails (board-proven,
	 * z1_boot_mainline_20261101_000604.log lines 809-813: -EINVAL ->
	 * "failed to register irq=37; err: -22" -> "probe ... failed with
	 * error -22").  The PMIC pwrap bus itself is healthy - the INT_CON0
	 * readback above returned 0x0000 - so this is purely an
	 * irq-request/pinctrl mapping gap, not a dead PMIC.
	 *
	 * Abort the probe here (mainline behaviour) and the whole MFD dies
	 * with it: regulators, keys, RTC, LEDs, codec and charger cells are
	 * never added, which took down card0, the power key and made
	 * battery/usb defer waiting for the mt6323 ldo_vusb / adc suppliers.
	 * Keep the r5c-era effective behaviour instead: report the errno
	 * loudly and let the probe continue.  We deliberately keep
	 * chip->irq_domain (devm_mfd_add_devices() needs it to map the
	 * cells' IRQ resources) and skip register_pm_notifier() because
	 * enable_irq_wake() in the PM notifier would act on an irq that has
	 * no installed handler.  PMIC interrupt delivery stays dead until
	 * the mt6572 EINT xlate gap is fixed; DT enumeration completeness
	 * wins.
	 */
	ret = devm_request_threaded_irq(chip->dev, chip->irq, NULL,
					mt6397_irq_thread, IRQF_ONESHOT,
					"mt6397-pmic", chip);
	if (ret) {
		dev_err(chip->dev,
			"failed to register irq=%d; err: %d (continuing: keep irq_domain, PMIC cells keep probing)\n",
			chip->irq, ret);
		return 0;
	}

	register_pm_notifier(&chip->pm_nb);
	return 0;
}
