# Z1 current-tree I2C1 controller-only board gate

Status: **COMPILED AND PACKAGED; NOT FLASHED**

This is a single-variable pre-gate for later touch-controller work.  It uses
the board-proven `layout2m` config/DTS/DTB baseline and a fully hashed current
source snapshot.  The candidate changes exactly one kernel configuration
symbol:

```text
-# CONFIG_I2C_MT65XX is not set
+CONFIG_I2C_MT65XX=y
```

The board DTS and produced DTB are byte-identical to the board-proven layout2m
artifacts.  The current source snapshot is pinned by
`SOURCE_TREE_MERKLE.txt`; this proves exactly which current tree was built,
without claiming that every historical source file from the board-pass build
is still available for byte-for-byte comparison.  Its
`i2c@1100a000` controller is `okay`, but every immediate child is required by
the structural DTB validator to be `disabled`.  The current DTB has one such
child, the old speculative `touchscreen@5d` Goodix node, and it remains
disabled.  Neither available mainline Hynitron driver is enabled.

## What this gate proves

- the current source tree builds `i2c-mt65xx.o`, the Z1 DTB and a complete
  `zImage`;
- the complete current source snapshot is bound by a Merkle-style content
  digest, including 93,398 regular files and 85 symlinks;
- the candidate config differs from the board-proven config only by
  `CONFIG_I2C_MT65XX=y`;
- a fresh decompile of the produced DTB contains exactly one MT6572 I2C1 node,
  that controller is enabled, and all its immediate children are disabled;
- the packed boot image preserves the board-proven MTK ramdisk, boot
  addresses, command line, standalone DTB and appended DTB layout;
- the legacy Android SHA-1 id, internal page padding and both complete 512-byte
  MTK headers are independently checked;
- the image is 5,564,416 bytes, below the 6 MiB boot partition limit.

## What this gate does not prove

- that the physical touch controller responds;
- that the factory kernel's embedded CST836U driver was used successfully;
- reset/interrupt GPIOs, power sequencing, IRQ polarity or coordinate
  transforms;
- protocol compatibility between CST836U and the mainline CST340/CST816S
  drivers.

The factory image audit found the exact driver string
`HYN_CST836U_DWZN_MT6572_4.4_V1.0_20181106`, but the archived runtime excerpt
reports probe failure.  No touch client is bound by this image.  A broad
`i2cdetect` scan is not part of the board test.

## Validation

```text
python3 validate_current_i2c1_gate.py
VALIDATION=PASS

python3 package_current_i2c1_gate.py
PACKAGING=PASS_UNTESTED_BOARD
```

Candidate image:

```text
boot_mainline_mt6572_lima_simplefb_layout2m_i2c1_controller_only.img
SHA256 be336ce8bbc62ac72f248594648c53559a7aa2e22bf8c42879af11af73c9ddfe
```

No flash waiter or hardware command was started while producing this gate.

## Integrity model

The packager hashes the validated build inputs again after reading them into
memory, runs a hash-verified private `mkbootimg` copy, validates the complete
output in memory, and atomically publishes the boot image only after PASS.
The later product archive is the release unit; files in this build directory
are staging evidence.

The local account is trusted.  Linux permissions cannot protect a temporary
directory from a hostile concurrent process running as the same `lxj` UID.
If that attacker is in scope, build and publish under a dedicated UID or
mount namespace and have a separate trusted publisher verify the final hash.
