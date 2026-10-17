# MT6323 regulator-state board gate — 2026-07-20

Status: **READY FOR A USER-CONFIRMED, READ-ONLY BOARD RUN; NO FLASH WAITER IS
RUNNING.**

## Purpose and ordering

The strict MT6323 MFD-parent-only board gate already passed on **2026-07-18**
and the Z1 was restored to the approved `layout2m` baseline afterwards.  That
pass proves that the parent can bind and that the PMIC CID low byte is `0x23`;
it does **not** prove the safe initial state of any regulator rail.

The next Wi-Fi/BT prerequisite is therefore a one-shot, external module that
only reads all 28 MT6323 regulator enable/status fields and four control
registers.  It is deliberately run on the previously approved strict-MFD
image, before making the VCN18/VCN28 registration-only build into a boot
candidate.  This preserves the dependency order:

```text
strict MFD parent PASS
  -> read-only regulator-state capture (this gate)
  -> inspect captured VCN18/VCN28 state
  -> package/review VCN18+VCN28 registration-only candidate
  -> CONNSYS chip-ID gate
```

No CONNSYS MMIO, regulator enable/disable operation, clock, reset, IRQ, SDIO,
BTIF, WLAN or firmware access occurs in this gate.

## Immutable inputs

| Item | SHA256 |
| --- | --- |
| strict-MFD boot target | `e1439d7474aa49eab88b820519cff8753d8ccc174398b6a099bdaf83253a0871` |
| approved layout2m rollback | `950a13569a497f5b3365f051be36a42332f263ce537f6883f85b0428ed2d5744` |
| factory boot fallback | `2dd41702e839ec2547ce0dea2bbd3603a5d019786502d939ebba4a6b578eaab5` |
| read-only audit module | `9d6b7db8dee88e62872f73942abbe6e7057d2d649e3eaf21ca2d15e6e76be680` |

Inputs and validation:

- strict-MFD build:
  `mainline_recon/build_layout2m_mfd_only_strict_board_gate_20260718`;
- target/rollback launcher:
  `.../z1_flash_board_gate.sh` (20 MTK connection attempts and full readback);
- module/deployer:
  `mainline_recon/mt6323_regulator_state_audit_20260718`;
- independent MFD evidence-integrity product:
  `bridge_backups/products/products_mt6323_mfd_layout2m_board_evidence_integrity_20260720_123800`.

`tools/mt6323_regulator_state_gate_preflight.sh` checks these local inputs but
never flashes, opens a serial port, transfers a module, or changes source.

## Required physical sequence

### Stage A — download/flash only

I will explicitly say **“可以开始 Stage A”** only after starting the strict
MFD flash waiter and showing that its local manifests/preflight passed.

1. Z1 is powered off.
2. Disconnect **both** the normal Z1 USB cable and C3/USB-to-TTL.
3. Prepare only the MTK download cable.
4. Start the existing strict-MFD flash waiter; only then connect the download
   cable and hold the power key as instructed.
5. Require a successful target write **and full partition readback match**.

### Stage B — normal USB gadget capture only

After Stage A has passed, remove the download cable, leave **C3/USB-to-TTL
disconnected**, boot Z1 and attach its ordinary USB data cable.

I will then run the audited `deploy_and_audit.py` only after the host sees the
ordinary USB gadget (`/dev/ttyACM*`).  The script transfers the module over
the Z1 gadget shell, validates its remote SHA256, inserts it once, parses a
fresh dmesg marker, unloads it, and proves the module is absent afterwards.

Use `tools/run_mt6323_regulator_state_capture.sh` for this stage.  It refuses
generic serial selection: it requires exactly one
`/dev/serial/by-id/usb-Linux_*Gadget_Serial*` node, rejects `ttyUSB*`, records
local preflight and input hashes, archives the complete deploy output before
returning either PASS or FAIL, and never flashes.  A failed/incomplete capture
is explicit stop evidence and must not lead to VCN registration.

**USB-to-TTL/C3 is not needed for this gate.**  Its historical capture was
inconclusive, while the ordinary USB-gadget path produced the valid strict-MFD
evidence.  Do not connect C3 merely for this test.

## Acceptance and stop conditions

Acceptance requires all of the following:

- exact strict-MFD image, PWRAP and `mt6397` runtime identity;
- module remote size/SHA256 match;
- CID low byte `0x23`;
- exactly 28 ordered rail rows and all four expected control reads;
- every audit accounting line reports `writes=0`;
- no fresh fault line; and
- successful `rmmod` with proof that the module is absent.

Stop immediately and do not create a VCN candidate if the module cannot load,
the transport stalls without verified cleanup, a field/parser mismatch occurs,
or any write/fault indication appears.  Preserve the raw output and restore
the approved layout2m baseline with the same controlled readback procedure.

On a clean pass, archive the complete output and IRQ snapshot first.  Only
then may the VCN18/VCN28 compile-only audit be converted into a separately
reviewed boot candidate.
