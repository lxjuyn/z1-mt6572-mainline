# MT6323 strict MFD-only board result — 2026-07-18

## Decision

**Hardware evidence PASS; formal Gate 2 INVALID because the candidate used the
wrong baseline.**

The tested image was:

`out/boot_mainline_mt6572_lima_simplefb_mfd_only_strict.img`

SHA256:

`8c53ccf62029a4813ab897b6bc88a87717a3a94aca9e576ff794588ad79d1c85`

Its configuration has `CONFIG_MFD_MT6397=y` and
`CONFIG_MTK_PMIC_WRAP=y`, with the audited PMIC child drivers and direct DT
children disabled. However, `CONFIG_MT6572_Z1_FIXED_ZRELADDR` is disabled and
the DT describes Linux RAM at `0x80000000`. The board-approved layout2m
baseline starts Linux RAM at `0x80200000`. This attempt therefore changed both
the memory layout and the MFD state and cannot establish a single-variable
MFD result.

Do not flash or reuse this image for any later gate.

## Preserved positive evidence

- Flash and full boot-partition readback comparison passed.
- Linux booted and exposed framebuffer, Lima render node, UDC and gadget
  serial.
- `1000f000.pwrap` bound to `mt-pmic-pwrap`.
- `1000f000.pwrap:mt6323` bound to `mt6397`.
- PMIC IRQ 46 and PWRAP IRQ 60 remained at zero on both CPUs.
- No PWRAP timeout, PMIC failure or IRQ storm was observed.
- A one-shot, read-only external module performed exactly one
  `regmap_read(MT6323_CID)` and logged:

  `mt6323-cid-probe: raw=0x2023 cid=0x23`

- The low byte is the expected MT6323 CID `0x23`; the original helper rejected
  only because it incorrectly expected the complete register value to be
  formatted as `0x0023`.
- The helper has been corrected to validate the low byte and to force an
  unload on post-load exceptions.
- After a subsequent Z1 reboot, `/proc/modules` contained no probe module.
  PWRAP and MFD bindings were still present, both IRQ counters remained zero,
  and the fault scan was empty.

## Evidence files

- flash log:
  `mainline_recon/z1_mfd_only_flash_20260718_120218.log`
- initial board probes:
  `mainline_recon/z1_mfd_only_probe_20260718_121816.log`
  and
  `mainline_recon/z1_mfd_only_probe_cid_20260718_122018.log`
- one-shot CID run:
  `mainline_recon/mt6323_cid_probe_20260718/board_run_20260718_122520.log`
- post-reboot module/health check:
  `mainline_recon/z1_mfd_only_cid_cleanup_20260718_123408.log`
- CID module SHA256:
  `cc7aea0e899f6a087ab97c8f9d784a1e3aa964269c9458fb230fe5b006c114a0`

## Required next action

1. Return the board to the accepted layout2m image:
   `out/boot_mainline_mt6572_lima_simplefb_layout2m.img`,
   SHA256
   `950a13569a497f5b3365f051be36a42332f263ce537f6883f85b0428ed2d5744`.
2. Build
   `out/boot_mainline_mt6572_lima_simplefb_layout2m_mfd_only_strict.img`
   from the exact layout2m source/config/DT baseline.
3. Verify that the only functional delta is the PWRAP-backed MT6323 MFD
   parent path and that all PMIC child drivers and direct child nodes remain
   disabled.
4. Repeat Gate 2 with full write/readback verification and the same stop
   conditions.
