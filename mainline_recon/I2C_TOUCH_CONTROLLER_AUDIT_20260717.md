# MT6572 I2C1 and touch controller audit

## Evidence

The Z1 DTS enables I2C1 at `0x1100a000`, IRQ 29, 400 kHz, using pins 113/114.
The speculative reference-board Goodix GT911 client at `0x5d` remains
disabled.

The original factory boot image and an archived four-line runtime-log excerpt
provide two different evidence types.

Direct parsing of `1/boot.img` tests each XZ magic candidate and finds exactly
one stream that decompresses to the factory Linux 3.4.67 kernel. It contains:

```text
hyn_ts
HYN_CST836U_DWZN_MT6572_4.4_V1.0_20181106
```

The archived four-line Z1 vendor-log excerpt records:

```text
hyn_ts: probe of 1-0015 failed with error -1
mtk_tpd: TPD_RES_X = 240, TPD_RES_Y = 320
mtk-tpd: cap touch and Generic touch both are not loaded
```

The embedded version string proves that the factory kernel includes a
Hynitron CST836U-targeted driver and that the runtime attempted `hyn_ts` at
I2C1 address `0x15`. It still does not prove that a responding CST836U is
physically present, because the probe failed. Reset GPIO, interrupt GPIO and
polarity, supply rails, sequencing and panel orientation remain unproven.

The old `cci72_we_jb3` source snapshot selects `GT9XX`. It is not direct Z1
hardware identity evidence and cannot override the 2022 factory-image
evidence; it must not be used to enable the speculative Goodix node.

The four-line log excerpt is content-verified in the audit bundle, but the
complete original serial capture, collection metadata and device identifier
are not present. Treat address `0x15` as a reported factory attempt, not as
proof of a responding physical controller or a proven one-to-one mapping to
the mainline DTS bus.

The reproducible offline validator and hashes are in:

`mainline_recon/touch_identity_audit_20260718/`

## Controller-only compile audit

An isolated source copy enabled only:

`CONFIG_I2C_MT65XX=y`

This older compile snapshot is retained as historical evidence. Its I2C
object and archived DTB results are:

- W=1 `i2c-mt65xx.o` build: PASS, zero diagnostics.
- Object SHA256:
  `1d372df3fae78d4aba26d121704aa6975596ce4c892dc9bba7f6148c58e476fc`.
- Z1 DTB SHA256:
  `a2011c73f0c772899b2ea42a11b6848f118aa<PHONE>b9cf45648b8fa1d3`.
- Decompiled archived DT confirms I2C1 `status = "okay"` and GT911
  `status = "disabled"`.
- No zImage or boot image was produced.

The archived DTB was not built from the current board tree, so it is not a
current board candidate. A fresh current-tree build must structurally verify
that every I2C1 child is disabled, not only `touchscreen@5d`.

The decompiler reports the pre-existing pinctrl simple-bus unit-address
warning. It is unrelated to I2C1 and does not alter the compiled node.

## First board gate

The first touch-related board image may enable only `I2C_MT65XX`; every I2C
client must remain disabled. Probe will map controller/DMA resources, enable
clocks for hardware initialization, request IRQ 29 and register the adapter.
This is an active controller gate, although it should generate no touch
transactions without a client or manual scan.

Required evidence:

- adapter probe succeeds without external abort, IRQ storm or timeout;
- simplefb, Lima, USB gadget and power-state baseline remain healthy;
- no touch client binds;
- no `i2cdetect` or arbitrary transfer is run during this first gate.

Only after that gate passes may a separate, narrowly scoped read-only
transaction at address `0x15` be designed from verified CST836U protocol
evidence. Do not run a broad `i2cdetect` scan. GT911 must stay disabled unless
new board-specific evidence supersedes the factory-image evidence.
