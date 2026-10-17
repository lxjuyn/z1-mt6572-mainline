# MT6572/Z1 current-tree keypad audit — 2026-07-18

## Scope

This is a read-only evidence audit of the keypad path already present in the
current, board-proven Z1 configuration.  It does not edit the kernel, DTS,
boot image, or any hardware-facing script.

The audit answers one narrow question: whether the existing current-tree
keypad driver and DT keymap implement the vendor's documented 72-slot mapping
without relying on the previously rejected three-column interpretation.

## Result

`validation.txt` must report `VALIDATION=PASS`.

The validated mapping is:

| Vendor hardware slot | Current driver row/column | DT Linux keycode | Key |
|---:|---|---:|---|
| 0 | row 0, column 0 | 115 | `KEY_VOLUMEUP` |
| 9 | row 1, column 0 | 114 | `KEY_VOLUMEDOWN` |

The current driver calculates the single-keypad location as `row = key / 9`
and `column = key % 9`; it does not use a three-column stride.  The current
DTB is an 8-by-9 matrix and encodes exactly those two entries.

The vendor `KPD_DL_KEY3 = 8` macro is a boot/download key selection, not an
entry in the runtime `KPD_INIT_KEYMAP`.  It must not be treated as proof that
the running Linux keypad driver maps runtime slot 8 to `KEY_POWER`.

## Hardware boundary

The supplied Z1 has no populated volume-up/down switches.  Existing boot
evidence proves keypad-driver registration, but it cannot prove a physical
press/release scan event on this unit.  Do not short the unverified PCB pads
or add a synthetic electrical test without schematic/layout or continuity
evidence for the pad pair, active level, pull, and reference.

There is no justified source or DT change from this audit.  A future board
test, if the switches are populated or their pads are proven, should capture
only targeted input events for the validated slots 0 and 9.

## Inputs and reproducibility

`INPUT_SHA256SUMS` pins every evidence input.  Run:

```sh
python3 extract_and_validate.py
sha256sum -c OUTPUT_SHA256SUMS
```

The generated evidence records include the toolchain/runtime identity and the
decoded keymap values.  This audit is offline only and does not require Z1,
C3, USB, or a flash operation.
