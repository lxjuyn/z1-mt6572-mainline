# MT6323 regulator-state board-run parser/cleanup repair — 2026-07-20

## What happened

The second Stage-B board capture (`mt6323_regulator_state_20260720_142416`)
passed the strict-MFD board baseline, transferred the immutable audit module
with matching SHA256, loaded it successfully, and emitted the complete
read-only 28-rail report.  The host run nevertheless stopped as incomplete.

This was a host parser/transport defect, not a PMIC access failure:

1. kernel `dmesg` prefixes module records with timestamps such as
   `[  942.605148] `, while the exact audit parser initially required the
   payload to start at column zero;
2. the recovery path used after a failed parser check echoed its marker on the
   shell prompt line (`~ # MARKER`) instead of a standalone line, so its strict
   completion-marker matcher timed out.

The raw first full-runtime capture is preserved unmodified under
`mainline_recon/board_regulator_state_captures/mt6323_regulator_state_20260720_142416`.

## Proven runtime facts from that preserved capture

- module transfer SHA256: `9d6b7db8dee88e62872f73942abbe6e7057d2d649e3eaf21ca2d15e6e76be680`;
- `INSMOD_RC=0`;
- raw CID `0x2023`, normalized CID `0x23`;
- exactly 28 rails and four control-register reports were emitted;
- begin and complete records both state `writes=0`;
- no fault string was present in the fresh module block;
- the emergency host cleanup subsequently executed `rmmod` successfully and
  proved `EMERGENCY_MODULE_ABSENT=1`.

This file does **not** turn that incomplete host run into a PASS result.  A
fresh full runner pass is still required before any VCN registration design or
board test may proceed.

## Repair

- `parse_rail_block()` strips only a leading Linux `dmesg` monotonic timestamp
  from each line before applying its existing exact grammar.
- `Z1Shell.recover()` now begins with `echo;` just like `command()`, making its
  completion marker standalone.
- The local validator now proves both timestamped-block acceptance and the
  exact recovery newline construction.  It also retains all negative parser
  checks.

No module bytes, kernel image, device tree, PMIC register access policy or
cleanup requirement changed.

## Additional numeric-parser correction found during replay

Replaying the preserved full runtime block after the timestamp repair exposed a
second host-only checker defect: `EXPECTED_RAIL_FIELDS` contained stale compact
register offsets for several rails, whereas the exact candidate's
`include/linux/mfd/mt6323/registers.h` and the read-only module print the real
MT6323 PWRAP register addresses (for example VPROC `0x021a`).  The table now
uses those exact header values.  The VCamIO software-enable mask is explicitly
bound to bit 14 (`0x4000`).

The validator now parses the exact candidate header and proves the deployer's
complete numeric rail table matches the source driver's rail list, register
macros, enable bits and QI bits.  It also re-parses the preserved complete
runtime record successfully.  The module itself remains byte-identical and
read-only.  A fresh end-to-end runner PASS is still required.

## CRLF replay correction

The ordinary USB gadget serial stream preserves `\r\n`.  The host checker now
normalizes CRLF before applying exact line-anchored expressions.  This was
verified by re-parsing the raw-byte preserved board transcript (not a
newline-normalized text copy) and by a synthetic timestamped-CRLF negative/
positive parser suite.  No board-side artifact changed.
