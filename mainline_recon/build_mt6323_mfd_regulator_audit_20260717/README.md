# MT6323 MFD + regulator compile-only audit

Result: **compile/link PASS; regulator board probe NO-GO**

An isolated reflink/copy of `fork_linux` was cleaned and rebuilt with:

```config
CONFIG_MFD_MT6397=y
CONFIG_REGULATOR_MT6323=y
# CONFIG_POWER_RESET_MT6323 is not set
# CONFIG_RTC_DRV_MT6397 is not set
```

The MFD core, IRQ implementation and MT6323 regulator objects compile with
`W=1` and no target-specific warnings.  A complete zImage and Z1 DTB link
successfully.  The only full-tree warning is the already-existing
`pinctrl-mtk-common-v2.h` forward-declaration warning, unrelated to this audit.
The production `fork_linux` configuration, vmlinux and zImage hashes remained
unchanged.

## Why this image must not be packaged or flashed

MT6323 MFD probe reads the PMIC CID, masks both PMIC interrupt banks, creates an
IRQ domain and requests the parent interrupt.  Enabling the regulator driver
then registers all 28 rails.  Current DT constraints can clamp voltage, enable
boot/always-on rails, and late-disable unused non-always-on rails.  That is not
a read-only probe and has not been validated against Z1's bootloader rail
state.

The first PMIC board gate must therefore be **MFD-only**, with every MFD child
explicitly disabled in the Z1 DTS.  Regulator, poweroff, RTC, keys, PMIC ADC,
efuse and thermal must remain disabled.

Artifacts in this directory are compile evidence only.  `zImage` is not a
flash target.
