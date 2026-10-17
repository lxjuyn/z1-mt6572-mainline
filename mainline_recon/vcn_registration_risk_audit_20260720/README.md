# MT6323 VCN18/VCN28 registration risk audit — 2026-07-20

Status: **STATIC/INDEPENDENT REVIEW ONLY — NOT A BOOT CANDIDATE**

This audit rechecks the isolated VCN18/VCN28 registration source independently
of its original compile audit.  It does not create an Android image, contact
Z1, open a serial device, or authorize a flash.

## What is proven locally

- The VCN source fork differs from the strict MFD-only fork only in the
  expected MT6323 regulator Kconfig, regulator driver and Z1 DTS files (plus
  the generated DTC include copy and clone provenance).
- Exactly two regulator DT subnodes are enabled: VCN18 and VCN28.  The
  selected decoded DTB nodes have neither `regulator-boot-on` nor
  `regulator-always-on`, and Z1 DTS has no consumer `*-supply` reference to
  either rail.
- The audit-only regulator ops omit enable/disable/voltage-select/mode-write
  callbacks.  The regulator core therefore sees no direct enable callback;
  its attempted no-op enable path returns `-EINVAL`, which the core explicitly
  accepts during registration.  `regulator_ignore_unused` avoids the delayed
  unused-regulator cleanup path.
- The original compile/static validation and immutable product manifest still
  pass.

## What is deliberately *not* claimed

Static analysis cannot prove the actual PMIC register state or every runtime
side effect of regulator registration on this board.  The required preceding
board gate is the external 28-rail, `writes=0` state capture documented in
`../MT6323_REGULATOR_STATE_BOARD_GATE_20260720.md`.

Only after that evidence is captured and reviewed may an explicitly hashed,
rollback-capable VCN registration boot candidate be packaged.  Do not treat
this directory or the older compile-only artifacts as flash authorization.
