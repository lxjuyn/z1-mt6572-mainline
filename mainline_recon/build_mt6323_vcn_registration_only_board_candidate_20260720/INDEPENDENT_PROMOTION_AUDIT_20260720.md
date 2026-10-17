# Independent promotion audit — 2026-07-20

## Result

```text
MANUAL_INDEPENDENT_REVIEW=GO
BOARD_GATE_PROMOTION=NO_GO
BOARD_TESTED=NO
BOARD_IMAGE_AUTHORIZATION=NO
VCN_WRITE_AUTHORIZATION=NO
```

The candidate is a correctly formed, host-validated package and may be
reviewed manually.  It is **not** promoted to a runnable board gate and must
not be flashed.

## Confirmed host-package facts

- Android boot header, MTK kernel/ramdisk envelopes, boot addresses, command
  line, appended `zImage + DTB`, and the 6 MiB partition limit validate.
- Candidate image SHA256:
  `15fc465fe90a76142516f858c92a3f9f942d5071d45beb27cf4a505353477055`.
- The registration-only regulator ops expose only read-side callbacks; VCN18
  and VCN28 are the only available rails; no VCN consumers or CONNSYS node
  are included.
- The preceding Stage-B board capture proved CID `0x23`, 28 decoded rails,
  VCN18/VCN28 off, `writes=0`, and cleanup.  That capture is prerequisite
  evidence, not runtime evidence for this candidate.

## Promotion blockers

### 1. MFD parent PMIC writes need a policy and runtime evidence

`mt6397-irq.c` masks MT6323 interrupt controls during parent initialization:

```text
0x0160 (INT_CON0) <- 0x0000
0x0166 (INT_CON1) <- 0x0000
```

The IRQ path can acknowledge pending interrupts by writing `0x0172`/
`0x0174`; suspend/resume can also write interrupt masks.  These are not VCN
rail enables, but they are PMIC writes.  A future gate must either reject all
PMIC writes (making this candidate ineligible) or define an exact allowlist,
prevent suspend/resume, and capture candidate-runtime evidence that no write
outside the allowlist occurred.

**Follow-up, 2026-07-20:** the static allow/deny policy is now independently
validated in `products_mt6323_vcn_pmic_write_policy_static_audit_20260720_171154`.
It permits only parent-IRQ initialization masks and qualified status-ack
writes during the ordinary registration phase, and denies regulator writes.
This closes the static-policy portion only; candidate-runtime trace/callsite
evidence is still absent, so promotion remains NO-GO.

### 2. The inherited diagnostic ramdisk is capability-open

The existing ramdisk intentionally exposes a root shell over USB Gadget Serial
and includes BusyBox capabilities such as `devmem`, `nandwrite`, `insmod`,
`modprobe`, `rmmod`, and `mmap`.  It does not automatically perform such
actions, but a registration-only board gate must be mechanically constrained
to a read-only command surface or use a dedicated hardened ramdisk.  The
current ramdisk therefore fails the capability-closure policy.

### 3. Source/config-to-image provenance is incomplete

Current provenance binds the kernel, DTB, ramdisk, packer, candidate, baseline
and factory rollback hashes.  It does not yet mechanically bind the full
source/config/build lineage (VCN regulator, MFD core/IRQ, regulator core, DTS,
exact `.config`, build logs, and packaging relation) to the shipped zImage.

## Required promotion conditions

All of the following must become `PASS` before a board-gate script can exist:

```text
BOOT_HEADER_PACKING=PASS
ALL_MANIFESTS=PASS
PROVENANCE_BINDING=PASS
VCN_STATIC_WRITE_PATH=PASS
MFD_PARENT_WRITE_POLICY=PASS
RAMDISK_WRITE_CAPABILITY_POLICY=PASS
PREREQUISITE_STAGE_B_EVIDENCE_BOUND=PASS
ROLLBACK_TARGET_AND_HASH_BOUND=PASS
BOARD_IMAGE_AUTHORIZATION=NO
```

Even after those host-side conditions pass, a separate review must explicitly
change authorization before any hardware-facing wait/flash command is started.
