# Z1 touch-controller identity audit

Status: **OFFLINE FACTORY-IMAGE AND COMPILE-EVIDENCE AUDIT ONLY**

This audit reads the original Z1 `boot.img` directly. It parses the Android
boot header, tests every XZ magic candidate in the kernel region with
`xz --single-stream`, accepts exactly one candidate that decompresses to a
Linux 3.4.67 kernel, and verifies the exact embedded strings:

- driver name `hyn_ts`;
- `HYN_CST836U_DWZN_MT6572_4.4_V1.0_20181106`.

The archived four-line stock boot-log excerpt records:

- panel framework size `240x320`;
- `hyn_ts` probing I2C bus 1, address `0x15`;
- probe failure and no loaded capacitive/generic touch driver.

The old `cci72_we_jb3` source snapshot selects `GT9XX`, but it is not direct
Z1 hardware identity evidence and cannot override the 2022 factory-image
evidence. Consequently, the speculative mainline Goodix node at `0x5d`
remains disabled.

The four-line runtime excerpt is content-hashed, but the complete original
serial capture, capture metadata and device identifier are not present.
`1-0015` therefore means only that the factory log reported a client on Linux
I2C adapter 1 at address `0x15`; it does not prove a successful transfer or a
one-to-one mapping to the current mainline `&i2c1`.

The older controller-only audit proves only that its archived source snapshot
compiled `i2c-mt65xx.o` and a DTB whose I2C1 node was enabled while its Goodix
child was disabled. It is not a reproducible build of the current board tree.
A new current-tree controller-only pre-gate is required before any board use.

Unknown and intentionally not inferred:

- CST836U reset and interrupt GPIOs;
- interrupt type/polarity;
- power rails and sequencing;
- coordinate inversion/rotation;
- firmware/config protocol compatibility with an available mainline driver.

The current mainline tree has Hynitron support only for the explicit
compatibles `hynitron,cst340` and `hynitron,cst816s`. It has no
`hynitron,cst836u` compatible. The CST816S binding example also uses address
`0x15`, but an address match is not protocol compatibility evidence. Both
Hynitron drivers remain disabled in the current production configuration.

The first safe board gate is still I2C1-controller-only. It must not bind a
touch client or run a broad `i2cdetect` scan. After the adapter baseline
passes, a separate narrowly scoped, read-only transaction at `0x15` can be
designed from verified CST836U protocol evidence.
