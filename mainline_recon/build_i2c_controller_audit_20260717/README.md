# MT6572 I2C controller compile audit

Scope: compile and DT validation only. This directory is not a board image.

## Isolation

- Production `fork_linux` source and config were not edited.
- The isolated source copy changes only `.config`:
  `CONFIG_I2C_MT65XX=y`.
- Target driver/Kconfig/Makefile/DTS files compare byte-identical with the
  production tree.
- The first `--reflink=always` copy attempt was unsupported by the filesystem;
  its partial tree was renamed `FAILED_REFLINK` before any edit. The completed
  source copy used `--reflink=auto`.

## Results

- `i2c-mt65xx.o`: PASS under W=1, no compiler diagnostics.
- Object SHA256:
  `1d372df3fae78d4aba26d121704aa6975596ce4c892dc9bba7f6148c58e476fc`.
- Full `dtbs` target generated the Z1 DTB successfully.
- Z1 DTB SHA256:
  `a2011c73f0c772899b2ea42a11b6848f118aa<PHONE>b9cf45648b8fa1d3`.
- Decompiled I2C1 is enabled; the Goodix client remains disabled.
- The decompiler emits the pre-existing pinctrl unit-address warning.

`mt6572_z1_dtb_W1_build.log` records an invalid direct make-goal experiment.
It is not a driver/DT compile failure; the corrected `dtbs` build passed and
produced the archived target DTB.

No zImage, boot image or flash command was produced.
