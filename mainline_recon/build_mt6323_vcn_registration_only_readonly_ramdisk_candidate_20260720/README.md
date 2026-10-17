# MT6323 VCN registration-only candidate with capability-minimized ramdisk

Status: **HOST PACKAGE + STATIC VALIDATION ONLY / BOARD-GATE PROMOTION NO-GO**

This candidate combines the previously audited VCN18/VCN28 registration-only
kernel/DTB with a new non-interactive capture ramdisk.  It is a host artifact
only: it is not in `out/`, has no flash launcher, has not been written to Z1,
and must not be flashed.

The image is 5,765,120 bytes, below the fixed 6 MiB boot partition limit.
The ramdisk replaces the root shell/getty diagnostic interface with a static
ARM PID 1 that configures USB ACM, emits bounded logs, invokes only fixed-argv
`busybox mdev -s`, then sleeps.  The ramdisk's static capability policy passes
but its USB runtime is untested.

The candidate still cannot be promoted because:

1. PMIC MFD write policy is static-pass but requires candidate-runtime trace
   evidence;
2. source/config/build-to-zImage provenance is statically snapshot-bound, but
   the completed clean host rebuild audit found zImage mismatches even though
   the Z1 DTB reproduced; see
   `mainline_recon/mt6323_vcn_provenance_reproducibility_audit_20260720`;
3. the new ramdisk's USB ACM runtime is untested;
4. board authorization remains absent.

`BOARD_IMAGE_AUTHORIZATION=NO` and `VCN_WRITE_AUTHORIZATION=NO` remain
binding.
