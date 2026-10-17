# MT6323 MFD-only board gate

Status: **NO-GO / SUPERSEDED — do not flash**

Independent audit found that this first artifact still had
`CONFIG_PINCTRL_MT6397=y` and did not explicitly disable the inherited
`mt6397codec` node. Neither had a binding path on this MT6323 image, but the
artifact did not satisfy the strict MFD-only isolation rule. A corrected image
must have a new SHA256.

Purpose: validate only the existing PWRAP path, MT6323 chip ID and PMIC parent
IRQ setup before enabling any PMIC child driver.

Enabled:

- `CONFIG_MFD_MT6397=y`
- existing `CONFIG_MTK_PMIC_WRAP=y`

Explicitly disabled in config:

- MT6323 regulator;
- MT6323 poweroff;
- MT6323 RTC;
- MT6323 PMIC keys;
- MT6323 AUXADC;
- MT6323 efuse;
- MT6323 LED.

The first artifact explicitly disabled these board-DT children:

- `adc`;
- `efuse`;
- `mt6323regulator`;
- `mt6323keys`;
- `power-controller`;
- `rtc`;
- `thermal`.

Expected probe side effects are limited but not read-only:

- read PMIC CID at `0x0100`;
- write `0` to MT6323 `INT_CON0` (`0x0160`) and `INT_CON1` (`0x0166`);
- create a 32-entry IRQ domain and request the existing PMIC parent IRQ;
- if that IRQ fires, read/clear MT6323 interrupt status.

No regulator provider is present, so this candidate cannot clamp voltages,
enable rails or perform regulator-core unused-rail cleanup.

Artifact:

- image: `boot_mainline_mt6572_lima_simplefb_mfd_only.img`
- size: 5,564,416 bytes
- SHA256: `26e8d4b33c9708090f826ef55c5682fdb4e1bb0d0099ee8c28ed9ee26fb3bd42`
- 6 MiB partition spare: 727,040 bytes

Packaging and child-isolation validation: PASS. Board status remains untested.

That original validation was incomplete because it did not check
`CONFIG_PINCTRL_MT6397`, `leds` and `mt6397codec`; therefore its PASS result is
superseded.

Immediate rollback/stop conditions:

- PWRAP timeout or PMIC CID other than `0x23`;
- PMIC parent IRQ storm;
- unexpected regulator/RTC/keys/poweroff/AUXADC/efuse/thermal probe;
- reset loop, eMMC error, UART loss, USB loss or framebuffer/GPU regression.

Rollback image is `/home/<user>/claude/6572/1/boot.img`, SHA256
`2dd41702e839ec2547ce0dea2bbd3603a5d019786502d939ebba4a6b578eaab5`.
