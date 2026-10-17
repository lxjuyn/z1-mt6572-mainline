# Z1 layout2m + strict MT6323 MFD-only board gate

Date: 2026-07-18

## Scope

This gate changes one functional layer relative to the board-approved
`layout2m` baseline: it enables the PWRAP-backed MT6323 MFD parent while all
direct MT6323 children and all corresponding child drivers remain disabled.
It does not test regulators, CONNSYS, audio, touch, keys, TF/MMC, baseband,
suspend or charging.

Target:

`boot_mainline_mt6572_lima_simplefb_layout2m_mfd_only_strict.img`

SHA256:

`e1439d7474aa49eab88b820519cff8753d8ccc174398b6a099bdaf83253a0871`

## Exact parent-side effects

The audited parent code performs these operations during probe:

1. obtains the PWRAP parent regmap;
2. reads `MT6323_CID` and keeps the low eight bits as the chip ID;
3. obtains the PMIC parent IRQ;
4. writes zero to `MT6323_INT_CON0` and `MT6323_INT_CON1`, masking every PMIC
   interrupt source;
5. creates the MT6397-family linear IRQ domain;
6. requests the threaded parent IRQ named `mt6397-pmic`;
7. registers a PM notifier;
8. asks generic MFD core to add the MT6323 cells.

The generic MFD core sees each matching DT child as `status = "disabled"` and
skips it error-free. No MT6323 child platform device should exist.

If the parent IRQ fires, the handler reads `MT6323_INT_STATUS0/1`, dispatches
mapped nested IRQs if any, then writes each observed status value back to
clear it. During suspend/resume the PM notifier would program wake/current
masks; suspend is outside this gate and must not be exercised.

## Risk-reduction evidence

An earlier image exercised the same PWRAP/MFD parent path and produced:

`mt6323-cid-probe: raw=0x2023 cid=0x23`

PWRAP and MFD bound, both observed IRQ counters remained zero, and no PMIC
fault was found. That image used the obsolete `0x80000000` RAM layout, so its
formal gate result is invalid. It is only risk-reduction evidence:

`../MT6323_MFD_ONLY_STRICT_BOARD_RESULT_20260718.md`

## Preconditions

All conditions are mandatory:

- Z1 battery charged; do not attempt this gate on a low battery.
- Z1 powered off and normal USB disconnected.
- C3 disconnected before flashing.
- reliable MTK download cable ready but not yet inserted.
- `validation.txt` ends in `VALIDATION=PASS`.
- `CANDIDATE_SHA256SUMS` verifies.
- target, primary rollback and factory hashes match `ROLLBACK_AUDIT.md`.
- independent re-audit result is GO.
- no `/dev/ttyACM*` and no active `0e8d:*` MTK USB device before starting.

## Flash sequence

1. Start `z1_flash_board_gate.sh` first. It validates all three image hashes,
   reruns the offline validator, then waits for the MTK port for up to 20
   attempts.
2. Only after the waiter is active: connect the battery, insert the download
   cable and hold power for 3-5 seconds.
3. The flash is accepted only after a complete `bootimg` readback whose
   target-length prefix exactly matches the candidate.
4. Remove the download cable. Keep Z1 powered off.
5. Connect C3 and start a fresh UART capture.
6. Power Z1 on. Do not press other keys and do not test suspend.
7. After Linux text appears, connect normal USB.

## Acceptance checks

The exact candidate passes only if all checks below pass:

- Linux reaches the known framebuffer console.
- normal USB enumerates and gadget serial becomes available.
- `/proc/iomem` reports `80200000-bf3fffff : System RAM` and does not expose
  the low CONNSYS carveout as System RAM.
- framebuffer, Lima render node, UDC and `/dev/ttyGS0` remain present.
- `1000f000.pwrap` binds to `mt-pmic-pwrap`.
- `1000f000.pwrap:mt6323` binds to `mt6397`.
- no `1000f000.pwrap:mt6323:*` child platform device exists.
- the one-shot CID helper reports raw `0x2023`, low-byte CID `0x23`, and is
  unloaded afterward. Use
  `board_helpers/deploy_cid_probe.py`; its source, module and serial helper are
  bound by the candidate manifest.
- PMIC/PWRAP IRQ counts do not increase by more than four over three seconds
  and do not show an accumulated storm.
- kernel fault scan is empty.
- `probe_layout2m_mfd_only.py` prints
  `LAYOUT2M_MFD_ONLY_BOARD_GATE=PASS`.

## Immediate stop conditions

Power Z1 off and return to the primary `layout2m` rollback image if any of
these occurs:

- flash write or readback verification failure;
- no Linux text within 90 seconds;
- panic, Oops, external abort, PWRAP timeout or repeated PMIC error;
- rapidly increasing PMIC/PWRAP IRQ count;
- RAM starts at `0x80000000` or the low carveout is exposed;
- an MT6323 child platform device appears;
- CID low byte is not `0x23`;
- framebuffer, render node, UDC or gadget serial regresses;
- USB cannot enumerate after one controlled reconnect.

The flash helper automatically rolls back only when writing or readback
verification fails. It cannot automatically roll back a candidate that
flashes successfully but then fails during boot. That case requires powering
off and starting the manual rollback waiter documented in
`ROLLBACK_AUDIT.md`.

## End state

After evidence capture, restore the board-approved `layout2m` image even if
the gate passes. This preserves a known-good board state while the next
regulator/CONNSYS gate is prepared offline.
