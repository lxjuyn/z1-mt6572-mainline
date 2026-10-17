# MT6323 VCN18/VCN28 registration audit — compile-only

Status: **COMPILE-ONLY / VALIDATION PASS / NOT BOARD ELIGIBLE**

This isolated build enables:

- `MFD_MT6397=y`
- `REGULATOR_MT6323=y`
- `REGULATOR_MT6323_AUDIT_AVAILABLE_ONLY=y`

The DT enables only the MT6323 regulator container and the `ldo_vcn18` and
`ldo_vcn28` subnodes. All other MT6323 direct children and all other MT6323
regulator subnodes are disabled. The audit ops expose state/status reads but
no enable, disable, voltage-select, mode-write, current-limit, or other write
callbacks.

The exact functional delta from the corrected layout2m + strict MFD-only
baseline is limited to the regulator driver, its audit Kconfig option, and
the two explicitly available regulator nodes. The layout2m memory map and
fixed `0x80208000` ZRELADDR are preserved.

`validate_vcn_register_audit.py` ends with `VALIDATION=PASS`.

This directory intentionally contains no Android boot image and no packaged
`zImage_appdtb`. It must not be flashed. A later board gate is allowed only
after:

1. the MFD-only board evidence has been reviewed;
2. the regulator-state read-only deployer has been repaired and independently
   re-audited;
3. an explicit board-gate launcher is created with target and rollback hashes;
4. the battery and physical USB procedure are rechecked.
