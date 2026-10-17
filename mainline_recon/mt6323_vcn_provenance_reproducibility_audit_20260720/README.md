# MT6323 VCN candidate provenance reproducibility audit — 2026-07-20

Status: **OFFLINE REBUILD EXECUTED / REPRODUCIBILITY NOT ESTABLISHED / NO-GO**

This audit records three host-only rebuild attempts against the source and
`dot.config` bound by the VCN18/VCN28 registration-only candidate. It makes no
hardware access, creates no boot image for `out/`, and does not authorize a
flash.

## Inputs and execution

- Historical source: `fork_linux_mt6323_vcn_register_audit_src_20260718`.
- Historical config: `build_mt6323_vcn_register_audit_20260718/dot.config`;
  its SHA256 is `23189a3af137bc60c346c3930fe23a0459b7a9b27d74cc491be02e84bc6774ce`.
- Historical zImage SHA256:
  `271744d3990942f020abc5f24ce9215d97fcc7262b96f6b4d22eb53f92e4f463`.
- Historical Z1 DTB SHA256:
  `79da7f5e134ae86302166ee748ef42eccfe9716fc88278f971d165f96527f0ce`.
- Fresh out-of-tree `olddefconfig`, `zImage`, and `dtbs` rebuild completed.
- A second rebuild fixed `KBUILD_BUILD_TIMESTAMP` to the timestamp recorded in
  the historical `utsversion.h`.
- A third rebuild preserved the historical generated initramfs CPIO bytes and
  used that same fixed timestamp.

## Result

- All three rebuilds completed successfully.
- The rebuilt `mt6572-z1.dtb` exactly matches the historical DTB.
- None of the three rebuilt zImages matches the historical zImage.
- The fresh generated initramfs CPIO differs from the historical CPIO because
  the historical build did not record its generated-CPIO timestamp. Preserving
  the historical CPIO still did not reproduce the historical zImage.
- The retained historical build directory is internally inconsistent: its
  current `include/generated/utsversion.h` contains a timestamped `#2 SMP`
  string, while the retained `init/version.o` embeds an empty timestamp after
  `# SMP`. This demonstrates that the historical output directory is not a
  complete, self-consistent clean-build provenance capture.

## Binding decision

`PROVENANCE_STATIC_BINDING=PASS` remains true: the source snapshot, selected
config, historical log and packaged artifacts are still hash-bound. However,
the stronger claim of a reproducible source/config-to-zImage rebuild is now
explicitly disproven by the captured attempts.

- `REPRODUCIBLE_REBUILD=FAIL_MISMATCH`
- `PROVENANCE_BINDING=STATIC_SNAPSHOT_BOUND_REBUILD_MISMATCH`
- `MISMATCH_SCOPE=ZIMAGE_ONLY_DTB_MATCHES`
- `BOARD_GATE_PROMOTION=NO_GO`
- `BOARD_IMAGE_AUTHORIZATION=NO`
- `VCN_WRITE_AUTHORIZATION=NO`

Before any later independent board-gate review, a fresh candidate must be
rebuilt from a fully recorded clean environment (including generated initramfs
inputs and version/build metadata) and then independently packaged/audited.
