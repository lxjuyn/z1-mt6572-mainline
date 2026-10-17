# MT6323 read-only regulator-state audit

Status: **COMPILE/STATIC AUDIT PASS / FIRST BOARD TRANSPORT ATTEMPT STOPPED
BEFORE MODULE TRANSFER / PREFIX-FIX LIVE-VALIDATED / BOARD RE-RUN PENDING**

This is an external, one-shot, read-only probe for the MT6323 PMIC. It is
bound byte-for-byte to the module built from:

- source fork:
  `mainline_recon/fork_linux_layout2m_mfd_only_strict_src_20260718`;
- exact candidate kernel build:
  `mainline_recon/build_layout2m_mfd_only_strict_board_gate_20260718/kbuild`;
- candidate release: `7.0.0-rc7`;
- module SHA256:
  `9d6b7db8dee88e62872f73942abbe6e7057d2d649e3eaf21ca2d15e6e76be680`.

The module performs only `regmap_read()` operations:

- validates the MT6323 CID low byte (`0x23`);
- reads the enable/status register used by each of the 28 regulators in
  `drivers/regulator/mt6323-regulator.c`;
- reports software-enable and QI/status bits;
- reads the three buck voltage-control selectors that the upstream regulator
  probe reads;
- reads the VCN28 mode/control register;
- reports `writes=0` at begin, completion and unload.

It does not:

- enable `CONFIG_REGULATOR_MT6323`;
- register any regulator;
- call a regulator consumer API;
- change a voltage, mode or enable bit;
- enable VCN18/VCN28;
- resume CONNSYS;
- map CONNSYS MMIO;
- request an IRQ;
- remain loaded after evidence collection.

The read-only probe is necessary because enabling the unmodified upstream
regulator driver is not a read-only operation. DT constraints can clamp
voltages, `regulator-boot-on`/`regulator-always-on` can write enable bits during
registration, and the regulator core can disable unused rails after a delay.
The first regulator gate must therefore be designed from captured state and
must not simply enable the full driver.

## Static and build result

- exact module/source provenance: PASS;
- exact module byte match to the isolated build: PASS;
- live undefined-symbol allowlist: PASS;
- all imports exported by the exact candidate `vmlinux.symvers`: PASS;
- exact candidate vermagic: PASS;
- exact `W=1` external build diagnostics: empty;
- 28-rail register/enable/QI mapping: PASS;
- deploy transport/parser synthetic tests: PASS;
- manifest binding for the module and source: PASS.

The module is **not a boot image** and must never be passed to the MTK
flasher. It is transferred only after Linux has booted and the ordinary USB
gadget serial device is present.

## Board-run procedure

Run only with the approved `layout2m + strict MFD-only` image already restored.
The user must keep the Z1 powered and connected through ordinary USB; C3 is
not required for this probe and remains disconnected until its wiring is
corrected.

```sh
python3 mainline_recon/mt6323_regulator_state_audit_20260718/deploy_and_audit.py
```

The deployer:

1. uses paced byte-at-a-time shell input;
2. transfers the module through short base64 append commands;
3. verifies remote size and SHA256;
4. checks framebuffer, Lima render node, gadget serial, PWRAP and `mt6397`;
5. writes a unique marker to `/dev/kmsg` and parses only the fresh dmesg
   section;
6. requires CID `0x23`, exactly 28 ordered rails, exact register/mask fields,
   four control reads and `writes=0`;
7. unloads the module and proves it is absent from `/proc/modules`;
8. attempts the same verified cleanup even if the host-side command times out.

No image has been produced from this module and no board run is currently
authorized. After a successful board run, archive the complete output,
including the IRQ snapshot, before designing the first regulator/CONNSYS
change.

## 2026-07-20 transport erratum

The first Stage-B attempt reached the exact strict-MFD runtime baseline but
stopped before module transfer/`insmod`: a silent remote reset command left the
interactive `~ # ` prompt on the same line as the completion marker, while the
host parser correctly requires a standalone marker line.  The raw failed
capture and two shell-only transport diagnostics are preserved in
`mainline_recon/board_regulator_state_captures/mt6323_regulator_state_20260720_141223`.

`z1_serial_helper_robust.py` now prefixes every command with `echo;`, forcing
a newline before its completion marker.  A live ordinary-USB shell test of the
actual helper then passed.  This repair does not change the module, kernel,
PMIC access policy, parser requirements or cleanup policy.  The initial failed
attempt did not reach module transfer or `insmod`; VCN registration remains
forbidden until a fresh full `writes=0` board capture passes.

## 2026-07-20 runtime parser/cleanup erratum

The subsequent full runtime attempt reached module transfer, `insmod`, and the
complete read-only PMIC report, but its host parser treated the normal Linux
`dmesg` timestamp prefix as part of the payload and stopped before its normal
cleanup routine.  An immediate independent shell cleanup returned
`EMERGENCY_RMMOD_RC=0` and proved `EMERGENCY_MODULE_ABSENT=1`.

The parser now removes only the leading per-line kernel timestamp before its
unchanged exact checks; the recovery helper now also forces a newline before
its own marker.  The repair and preserved raw output are documented in
`RUNTIME_PARSE_CLEANUP_FIX_20260720.md`.  This result remains an incomplete
capture until the entire runner completes a new clean pass, including verified
normal cleanup and post-cleanup runtime snapshot.

A further replay check corrected the host's hard-coded numeric rail table to
the exact MT6323 register addresses from the candidate header; this does not
change the byte-identical read-only module.  The validator now binds that
numeric table to `registers.h`, and re-parses the preserved runtime report.

The strict parser also normalizes ordinary-USB serial CRLF before its exact
line checks; raw-byte replay of the preserved board transcript passes.
