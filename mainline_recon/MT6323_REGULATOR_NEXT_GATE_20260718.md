# MT6323 regulator gate design for Z1 WiFi/BT

Date: 2026-07-18

## Decision

Do **not** enable the unmodified `CONFIG_REGULATOR_MT6323` path as the next
board image.

The next action after the exact `layout2m + strict MFD-only` board gate passes
is a no-flash, read-only snapshot of all MT6323 regulator enable/status bits.
Only after that snapshot may a regulator-registration image be designed.

## Why the unmodified driver is not a read-only gate

`drivers/regulator/mt6323-regulator.c`:

- reads three buck voltage-control selectors and the PMIC CID;
- then calls `devm_regulator_register()` for all 28 rails unconditionally.

The included `mt6323.dtsi` describes all 28 regulator subnodes as available.
Enabling only the regulator container therefore exposes the complete table.

Registration enters regulator-core constraint handling:

- every DT voltage range sets `apply_uV`;
- an out-of-range rail may be written to a constrained voltage;
- `regulator-boot-on` and `regulator-always-on` cause an enable operation
  during registration;
- on a populated-DT system, regulator core assumes full constraints;
- after 30 seconds it disables unused enabled rails that are not always-on,
  unless `regulator_ignore_unused` is present.

This can alter CPU, system, USB, storage, RTC, radio, camera, SIM, vibrator
and other rails in one boot. It is not an acceptable single-variable gate.

## Read-only state audit prepared

Directory:

`mainline_recon/mt6323_regulator_state_audit_20260718/`

Key artifacts:

- module SHA256:
  `9d6b7db8dee88e62872f73942abbe6e7057d2d649e3eaf21ca2d15e6e76be680`
- source SHA256:
  `375cba290de9b264650c84d10aa81260e0c5587070d87f2f65cc31e141cd6f25`
- validation SHA256:
  `af0d31a06d41a619a9628050961435b373f0951c036318961cb6e5d68212f44f`

Offline validation passes:

- all 28 register/enable/QI definitions exactly match the upstream driver;
- module undefined symbols all exist in the exact candidate `vmlinux`;
- undefined symbols include `regmap_read` and no regmap write/update API;
- no regulator register/enable/disable/set API is referenced;
- kernel release/vermagic matches `7.0.0-rc7`;
- source compiled without C compiler diagnostics.

The missing build-tree `Module.symvers` produced expected modpost warnings.
`CONFIG_MODVERSIONS` is disabled; all runtime imports were checked directly
against the exact candidate `vmlinux`.

The module is not yet board-approved. It may run only after the exact MFD-only
candidate passes. It must be unloaded immediately after capture.

## Evidence the read-only run must collect

For each of 28 rails:

- PMIC register address and raw value;
- software-enable bit;
- QI/status bit.

Additional reads:

- CID, low byte must be `0x23`;
- VPROC/VSYS/VPA voltage-control selectors;
- VCN28 mode/control state.

The audit must log `writes=0`, and post-run checks must prove:

- module unloaded;
- MFD/PWRAP bindings unchanged;
- no PMIC/PWRAP IRQ storm;
- no kernel fault;
- framebuffer, Lima and USB remain healthy.

## Proposed regulator-registration gate

This design still requires independent review before implementation:

1. add a default-off Z1 audit option that makes the MT6323 driver register
   only regulator subnodes that are explicitly available in DT;
2. mark all 28 subnodes disabled in the gate DTS;
3. enable only `ldo_vcn18` and `ldo_vcn28`;
4. keep every CONNSYS consumer disabled, so neither rail is requested or
   enabled;
5. append `regulator_ignore_unused` for this registration-only gate, blocking
   the 30-second late cleanup from disabling pre-existing rails;
6. verify the two fixed-voltage constraints cannot trigger a selector write;
7. compile and inspect final archives/`vmlinux` so only the MFD parent and
   MT6323 regulator driver are new;
8. board-test registration and sysfs/debug visibility only, then restore the
   exact MFD-only/baseline image.

The first registration gate must not enable VCN18/VCN28, resume the CONN
power domain or read CONNSYS MMIO.

## WiFi/BT gate order after registration

1. strict MFD-only board pass;
2. read-only 28-rail snapshot on that same image;
3. VCN18/VCN28 registration-only gate;
4. chip-ID-only gate: enable VCN18/VCN28, resume CONN genpd, read
   `0x18070008`, synchronously unwind;
5. BTIF transport and IRQ 122;
6. WLAN AHB HIF at `0x180f0000`, IRQ 123, without GDMA;
7. AP GDMA;
8. firmware/shared-EMI ABI;
9. VCN33 BT/WiFi PA rails and functional RF calibration.

The vendor MT6572 path powers VCN18 and VCN28 before the CONN domain/chip-ID
poll. VCN33 BT/WiFi rails are handled later for radio calibration and must not
be folded into the chip-ID gate.
