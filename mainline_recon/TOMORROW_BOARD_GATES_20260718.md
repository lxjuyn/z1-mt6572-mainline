# Z1 controlled board gates — 2026-07-18

Do not combine candidates. Flash, boot, collect evidence and decide PASS/rollback
for each gate before starting the next one.

## Gate 1: 2 MiB layout only — completed 2026-07-17

Target:

`out/boot_mainline_mt6572_lima_simplefb_layout2m.img`

SHA256:

`950a13569a497f5b3365f051be36a42332f263ce537f6883f85b0428ed2d5744`

The gate passed with strict write/readback validation. Linux RAM is
`0x80200000..0xbf3fffff`; simplefb, Lima and USB gadget are healthy. PMIC MFD
and CONNSYS consumers remain disabled.

Recorded evidence:

- Linux System RAM begins at `0x80200000`;
- low 2 MiB is not System RAM;
- framebuffer, Lima and USB gadget baseline are healthy;
- C3 enumeration works but C3 UART receive remains an independent issue;
- MMC block-device enumeration was already absent in the prior mainline
  baseline and remains separate from this layout gate.

## Gate 2: MT6323 MFD only

Run only after Gate 1 is either accepted or rolled back and a known-good
baseline is re-established.

Rejected first target:

`out/boot_mainline_mt6572_lima_simplefb_mfd_only.img`

`26e8d4b33c9708090f826ef55c5682fdb4e1bb0d0099ee8c28ed9ee26fb3bd42`

This image is invalid: it left `CONFIG_PINCTRL_MT6397=y` and did not
explicitly disable `mt6397codec`.

Rejected second target:

`out/boot_mainline_mt6572_lima_simplefb_mfd_only_strict.img`

SHA256:

`8c53ccf62029a4813ab897b6bc88a87717a3a94aca9e576ff794588ad79d1c85`

The hardware path produced useful evidence, including CID low byte `0x23`, but
this image used the old `0x80000000` RAM layout. It did not preserve the
accepted layout2m baseline and is now INVALID/SUPERSEDED.

Corrected replacement target:

`out/boot_mainline_mt6572_lima_simplefb_layout2m_mfd_only_strict.img`

SHA256:

`e1439d7474aa49eab88b820519cff8753d8ccc174398b6a099bdaf83253a0871`

Status:

**OFFLINE BOARD-CANDIDATE GO / NOT YET BOARD TESTED**

Its isolated build, direct payload/layout validation, manifests, rollback
audit, helper binding, launcher preflight and independent re-audit all pass.

Product archive:

`bridge_backups/products/products_mt6572_layout2m_mfd_only_strict_candidate_20260718_164718/`

Archive manifest SHA256:

`003731ae40e6b9634999f7a76cd82ec1368f488b23cc602a8dab91c0c18795bf`

Required evidence for the replacement:

- PMIC CID `0x23`;
- no PWRAP timeout;
- no PMIC IRQ storm;
- no PMIC child driver binds;
- framebuffer, Lima and USB gadget baseline remain healthy;
- Linux MMC absence does not regress or get credited as an MFD PASS. Physical
  eMMC boot is already proven by LK/mtkclient, while Linux `mmc0` remains a
  later regulator-dependent gate.

Stop after this gate. The offline CONNSYS chip-ID and I2C-controller audits
produced objects/DTBs only, not boot images. Do not combine either one with
the MFD-only test.

If Gate 2 passes, run the separately compiled read-only 28-rail MT6323 state
audit over gadget serial before designing any regulator image. This is a
no-flash helper and must unload after capture:

`mainline_recon/mt6323_regulator_state_audit_20260718/`

## Common flash protocol

Primary rollback and accepted baseline:

`out/boot_mainline_mt6572_lima_simplefb_layout2m.img`

Rollback SHA256:

`950a13569a497f5b3365f051be36a42332f263ce537f6883f85b0428ed2d5744`

Tertiary known-good mainline recovery:

`out/boot_mainline_mt6572_lima_simplefb_fresh.img`

SHA256:

`5a0c7358ec7ac2b4963373f873ba9190abf0bfee0caebedae304dde8fbee9641`

Secondary factory recovery:

`1/boot.img`

Secondary SHA256:

`2dd41702e839ec2547ce0dea2bbd3603a5d019786502d939ebba4a6b578eaab5`

Sequence:

1. Z1 off, normal USB removed, C3 disconnected, download cable ready.
2. Recheck target and rollback SHA256, size and boot-image validation.
3. Start the flash waiter first with 20 attempts. Every write is accepted only
   after a full partition read and exact comparison over the image length.
4. Only then connect battery/download cable and hold power until capture.
5. After successful write/readback, remove the download cable.
6. Connect C3 and confirm UART reception before powering Z1.
7. Connect normal USB only after the Linux screen appears.

The flash process must be started interactively after the battery is charged;
no unattended flash command is active.
