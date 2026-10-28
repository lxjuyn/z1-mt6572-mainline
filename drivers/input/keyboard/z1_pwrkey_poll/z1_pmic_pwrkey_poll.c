// SPDX-License-Identifier: GPL-2.0-only
/*
 * z1_pmic_pwrkey_poll.c - polled MT6323 PMIC power key for the MT6572 Z1 board.
 *
 * FALLBACK PATH ONLY.  The normal path is the MFD/irqchip route:
 *
 *   mt6397-core.c (mediatek,mt6323 MFD)  ->  mt6397_irq_init()
 *     -> mt6323-keys cell (mediatek,mt6323-keys)
 *       -> drivers/input/keyboard/mtk-pmic-keys.c
 *
 * That path reports KEY_POWER from the PMIC EINT and is what you want.
 * This driver exists for the case where the PMIC interrupt line cannot be
 * brought up (EINT mis-wired, IRQ storm, PMIC INT output dead), and it only
 * needs the pwrap *read* path, which works even with the fork's historical
 * PMIC write-deny gate armed.
 *
 * How the key state is read (verified against this tree, not from memory):
 *
 *   Register : MT6323_CHRSTATUS = 0x0142
 *              include/linux/mfd/mt6323/registers.h:94
 *   Bit      : bit 1 (mask 0x2) = PWRKEY_STATUS, 0 while the key is held down
 *              drivers/input/keyboard/mtk-pmic-keys.c:80-82
 *                mt6323_regs.keys_regs[MTK_PMIC_PWRKEY_INDEX] =
 *                    MTK_PMIC_KEYS_REGS(MT6323_CHRSTATUS, 0x2, ...)
 *              drivers/input/keyboard/mtk-pmic-keys.c:207-216
 *                key_deb = regmap_read(CHRSTATUS) & deb_mask;
 *                pressed = !key_deb;
 *
 * So the level is "active low": bit clear == key pressed.
 *
 * DT binding (not present in the DT yet; add only if you need this fallback):
 *
 *   pwrkey-poll {
 *           compatible = "z1,pmic-pwrkey-poll";
 *           mediatek,pwrap = <&pwrap>;
 *           linux,keycode = <KEY_POWER>;   // 116
 *           poll-interval-ms = <50>;
 *           debounce-samples = <3>;
 *   };
 */

#include <linux/delay.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>
#include <linux/mfd/mt6323/registers.h>

#define Z1_PWRKEY_CHRSTATUS	MT6323_CHRSTATUS	/* 0x0142 */
#define Z1_PWRKEY_STATUS_MASK	BIT(1)			/* 0x2, 0 = pressed */

#define Z1_PWRKEY_DEF_POLL_MS	50
#define Z1_PWRKEY_DEF_DEBOUNCE	3
#define Z1_PWRKEY_MAX_DEBOUNCE	20

struct z1_pwrkey {
	struct device *dev;
	struct regmap *regmap;
	struct input_dev *input;
	struct delayed_work work;
	unsigned int keycode;
	unsigned int poll_ms;
	unsigned int debounce;
	unsigned int stable;	/* consecutive samples agreeing with `pressed` */
	bool pressed;
	bool reported;
};

static void z1_pwrkey_poll(struct work_struct *work)
{
	struct z1_pwrkey *pk = container_of(to_delayed_work(work),
					   struct z1_pwrkey, work);
	unsigned int val;
	bool now_pressed;

	if (regmap_read(pk->regmap, Z1_PWRKEY_CHRSTATUS, &val)) {
		/* Keep polling: a transient pwrap read failure is not fatal. */
		schedule_delayed_work(&pk->work, msecs_to_jiffies(pk->poll_ms));
		return;
	}

	now_pressed = !(val & Z1_PWRKEY_STATUS_MASK);

	if (now_pressed == pk->pressed) {
		pk->stable = 0;
	} else if (++pk->stable >= pk->debounce) {
		pk->pressed = now_pressed;
		pk->stable = 0;

		if (pk->pressed != pk->reported) {
			pk->reported = pk->pressed;
			input_report_key(pk->input, pk->keycode, pk->pressed);
			input_sync(pk->input);
			dev_dbg(pk->dev, "KEY_POWER %s (CHRSTATUS=0x%04x)\n",
				pk->pressed ? "pressed" : "released", val);
		}
	}

	schedule_delayed_work(&pk->work, msecs_to_jiffies(pk->poll_ms));
}

static int z1_pwrkey_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *pwrap_np;
	struct platform_device *pwrap_pdev;
	struct z1_pwrkey *pk;
	int ret;

	pk = devm_kzalloc(&pdev->dev, sizeof(*pk), GFP_KERNEL);
	if (!pk)
		return -ENOMEM;

	pk->dev = &pdev->dev;
	pk->keycode = KEY_POWER;
	pk->poll_ms = Z1_PWRKEY_DEF_POLL_MS;
	pk->debounce = Z1_PWRKEY_DEF_DEBOUNCE;

	of_property_read_u32(np, "linux,keycode", &pk->keycode);
	of_property_read_u32(np, "poll-interval-ms", &pk->poll_ms);
	of_property_read_u32(np, "debounce-samples", &pk->debounce);

	if (!pk->poll_ms)
		pk->poll_ms = Z1_PWRKEY_DEF_POLL_MS;
	if (!pk->debounce || pk->debounce > Z1_PWRKEY_MAX_DEBOUNCE)
		pk->debounce = Z1_PWRKEY_DEF_DEBOUNCE;

	/*
	 * We need the pwrap regmap.  Grab it from the pwrap platform device
	 * named by our own phandle, which is exactly the regmap the MT6323
	 * MFD children use (dev_get_regmap(dev->parent, NULL)).
	 */
	pwrap_np = of_parse_phandle(np, "mediatek,pwrap", 0);
	if (!pwrap_np) {
		dev_err(&pdev->dev, "missing mediatek,pwrap phandle\n");
		return -EINVAL;
	}

	pwrap_pdev = of_find_device_by_node(pwrap_np);
	of_node_put(pwrap_np);
	if (!pwrap_pdev) {
		dev_err(&pdev->dev, "pwrap device not found (deferred)\n");
		return -EPROBE_DEFER;
	}

	pk->regmap = dev_get_regmap(&pwrap_pdev->dev, NULL);
	if (!pk->regmap) {
		dev_err(&pdev->dev, "pwrap regmap not available (deferred)\n");
		return -EPROBE_DEFER;
	}

	pk->input = devm_input_allocate_device(&pdev->dev);
	if (!pk->input)
		return -ENOMEM;

	pk->input->name = "z1-pmic-pwrkey-poll";
	pk->input->phys = "z1-pmic-pwrkey-poll/input0";
	pk->input->id.bustype = BUS_HOST;

	input_set_capability(pk->input, EV_KEY, pk->keycode);
	__set_bit(EV_KEY, pk->input->evbit);
	__set_bit(EV_SYN, pk->input->evbit);

	ret = input_register_device(pk->input);
	if (ret)
		return ret;

	/* Seed the state so we do not emit a spurious event at probe. */
	{
		unsigned int val = 0;

		if (!regmap_read(pk->regmap, Z1_PWRKEY_CHRSTATUS, &val))
			pk->pressed = !(val & Z1_PWRKEY_STATUS_MASK);
		pk->reported = pk->pressed;
	}

	INIT_DELAYED_WORK(&pk->work, z1_pwrkey_poll);
	platform_set_drvdata(pdev, pk);
	schedule_delayed_work(&pk->work, msecs_to_jiffies(pk->poll_ms));

	dev_info(&pdev->dev,
		 "polled MT6323 power key ready: keycode=%u poll=%ums debounce=%u\n",
		 pk->keycode, pk->poll_ms, pk->debounce);

	return 0;
}

static void z1_pwrkey_remove(struct platform_device *pdev)
{
	struct z1_pwrkey *pk = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&pk->work);
}

static const struct of_device_id z1_pwrkey_of_match[] = {
	{ .compatible = "z1,pmic-pwrkey-poll" },
	{ }
};
MODULE_DEVICE_TABLE(of, z1_pwrkey_of_match);

static struct platform_driver z1_pwrkey_driver = {
	.probe = z1_pwrkey_probe,
	.remove = z1_pwrkey_remove,
	.driver = {
		.name = "z1-pmic-pwrkey-poll",
		.of_match_table = z1_pwrkey_of_match,
	},
};
module_platform_driver(z1_pwrkey_driver);

MODULE_DESCRIPTION("Polled MT6323 PMIC power key for MT6572 Z1 (fallback path)");
MODULE_LICENSE("GPL");
