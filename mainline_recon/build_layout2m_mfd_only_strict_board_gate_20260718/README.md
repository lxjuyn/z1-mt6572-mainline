# Z1 layout2m + strict MT6323 MFD-only board candidate

Status: **OFFLINE BOARD-CANDIDATE GO / NOT YET BOARD TESTED**

This candidate starts from the exact configuration and board DTS captured from
the board-approved layout2m image. Its intended functional delta is only:

- `CONFIG_MFD_MT6397=y`;
- all MFD child Kconfig symbols explicitly disabled or unavailable;
- the nine direct MT6323 child nodes explicitly disabled.

The fixed layout remains:

- `CONFIG_MT6572_Z1_FIXED_ZRELADDR=y`;
- compressed kernel destination `0x80208000`;
- DT RAM `0x80200000 + 0x3fe00000`.

The enabled parent path reads the PMIC CID, writes zero to the two MT6323
interrupt-enable banks, creates an IRQ domain, requests the threaded parent
IRQ and registers a suspend notifier. If the parent IRQ fires, it reads the
status banks and writes the observed status back to clear it. The generic MFD
core skips the disabled DT child nodes without creating their platform
devices. It must not bind regulator, RTC, keys, poweroff, AUXADC, efuse, LED,
codec or thermal child drivers.

Offline validation now checks the boot payload directly, scans the compressed
`zImage` address literal, inspects linked archives and `vmlinux`, verifies
disabled-node MFD semantics and audits the exact parent-side effects. The
targeted MFD `W=1` build is clean. The full-kernel `W=1` build has 51 captured
pre-existing diagnostics outside this MFD delta; see
`full_build_W1_diagnostics_unique.txt`.

This image is a separate board gate. Do not flash it until:

1. `validation.txt` ends in `VALIDATION=PASS`;
2. `CANDIDATE_SHA256SUMS` verifies;
3. `ROLLBACK_AUDIT.md` and `BOARD_GATE_PLAN.md` are present;
4. `INDEPENDENT_REAUDIT.md` records GO;
5. the Z1 battery is charged and the operator explicitly starts the gate.
