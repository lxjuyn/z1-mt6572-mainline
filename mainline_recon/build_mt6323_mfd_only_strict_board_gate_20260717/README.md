# MT6323 strict MFD-only board gate

Status: **UNTESTED BOARD CANDIDATE**

This supersedes the rejected `26e8d4...` MFD-only image. The corrected
candidate enables only the MT6323 MFD core and PWRAP path:

- `CONFIG_MTK_PMIC_WRAP=y`
- `CONFIG_MFD_MT6397=y`

Every PMIC child driver is disabled or unavailable:

- regulator, poweroff, RTC, PMIC keys, AUXADC, efuse, LED, thermal;
- `CONFIG_PINCTRL_MT6397` is explicitly disabled;
- the board DT explicitly disables every direct MT6323 child, including
  `leds` and `mt6397codec`.

The only intentional MFD probe side effects are PMIC CID read, PMIC interrupt
mask initialization, parent IRQ request and pending-status clear if the parent
IRQ fires. No regulator provider is present, so the candidate cannot clamp
voltages, enable rails or run regulator unused-rail cleanup.

Artifact:

- image: `boot_mainline_mt6572_lima_simplefb_mfd_only_strict.img`
- size: 5,564,416 bytes
- SHA256: `8c53ccf62029a4813ab897b6bc88a87717a3a94aca9e576ff794588ad79d1c85`
- spare in the 6 MiB boot partition: 727,040 bytes

Offline validation is PASS:

- Android/MTK headers, payloads, appended DT and standalone DT agree;
- exact MFD configuration isolation is PASS;
- all direct PMIC DT children are disabled;
- targeted `mt6397-core` and `mt6397-irq` W=1 rebuild has no diagnostics.

Do not combine this with regulator, AUXADC, poweroff, CONNSYS or display work.
Board stop conditions are: PWRAP timeout, CID other than `0x23`, PMIC IRQ
storm, any unexpected PMIC child bind, reset loop, loss of USB/framebuffer, or
any regulator activity.

Primary rollback is the board-proven fresh Lima/simplefb image:
`out/boot_mainline_mt6572_lima_simplefb_fresh.img`,
SHA256 `5a0c7358ec7ac2b4963373f873ba9190abf0bfee0caebedae304dde8fbee9641`.
The factory Android image `1/boot.img` is retained as a secondary recovery
image.
