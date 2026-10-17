# Independent re-audit record

Date: 2026-07-18

Agent:

`019f742b-0bec-70c3-ab38-069fbd37bbbf` (`lx/gpt-5.6-terra`)

Mode:

- read-only;
- no source or artifact modification;
- no UART/USB access;
- no flash command.

## Audit sequence

### First audit: NO-GO

The first pass confirmed the technical config/layout/DT/payload delta but
identified missing delivery controls: no complete product/rollback audit,
incomplete validator coverage, and insufficient documentation of the parent
MFD side effects and stop strategy.

Those gaps were closed by:

- direct `zImage` address scanning;
- archive/`vmlinux` child-driver exclusion checks;
- disabled-node generic MFD semantic checks;
- exact parent CID/IRQ side-effect checks;
- `BOARD_GATE_PLAN.md`;
- `ROLLBACK_AUDIT.md`;
- candidate and external dependency manifests.

### Second audit: NO-GO

The second pass found one blocking delivery inconsistency: the prescribed
`out/` target used by `z1_flash_board_gate.sh` did not exist. It also listed
non-blocking hardening items for live DTB decompile, source-copy equality,
CID-helper binding and launcher-enforced manifests.

Those gaps were closed by:

- installing the exact target at
  `out/boot_mainline_mt6572_lima_simplefb_layout2m_mfd_only_strict.img`;
- binding that target in `EXTERNAL_DEPENDENCY_SHA256SUMS`;
- making the launcher enforce both manifests;
- adding a safe `PREFLIGHT_ONLY=1` path;
- runtime DTB decompile with the exact kbuild DTC;
- byte-equality checks between live MFD sources and `source_audit`;
- packaging and hash-binding the corrected one-shot CID helper and its
  self-contained deploy/cleanup helper.

### Third audit: GO

Final verdict:

**OFFLINE BOARD-CANDIDATE GO**

The agent independently reran:

- `CANDIDATE_SHA256SUMS`: PASS;
- `EXTERNAL_DEPENDENCY_SHA256SUMS`: PASS;
- `validate_layout2m_mfd_only_strict.py`: `VALIDATION=PASS`;
- `PREFLIGHT_ONLY=1 z1_flash_board_gate.sh`:
  `BOARD_GATE_PREFLIGHT=PASS`.

It independently confirmed:

- exact target size 5,564,416 bytes;
- exact target SHA256
  `e1439d7474aa49eab88b820519cff8753d8ccc174398b6a099bdaf83253a0871`;
- `0x80208000` compressed destination and no `0x80008000` literal;
- runtime DTB parent/child/memory layout;
- parent-only MFD objects and symbols;
- disabled child object/symbol absence;
- parent CID/IRQ side effects;
- corrected CID helper source/module ABI and cleanup behavior;
- automatic rollback scope and manual boot-failure rollback plan.

## Boundary of the verdict

GO means the artifact is eligible for the documented controlled board gate.
It does not mean:

- the exact candidate has already booted;
- PMIC regulator children are safe to enable;
- CONNSYS/WiFi/BT is powered;
- automatic rollback can detect a boot-stage failure.

The exact candidate remains untested on hardware until the operator starts
the documented gate with a charged battery.
