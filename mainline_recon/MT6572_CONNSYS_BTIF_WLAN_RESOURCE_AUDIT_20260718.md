# MT6572 integrated CONNSYS BTIF/WLAN resource audit

Status: **OFFLINE RESOURCE AUDIT PASS / NO DTS OR IMAGE CHANGE**

This document supersedes the old external-MT6620/SDIO path for Z1 runtime
planning. The current Z1 target tree contains no integrated CONNSYS consumer
or resource node; that is intentional.

## Architecture decision

Z1 uses the MT6572 integrated CONNSYS path:

- VCN18/VCN28 PMIC supplies;
- MT6572 CONN power domain, domain ID 3;
- WMT/STP over the integrated BTIF;
- WLAN AHB HIF;
- AP GDMA only as a later, separate gate.

The existing `drivers/misc/mediatek/connectivity/` and
`drv_wlan/mt6620` code is an old external MT6620/SDIO/GPIO stack. It remains
quarantined and is not a Z1 board candidate. Its GPIO/EINT stubs are inert
no-ops and can produce false compile/runtime confidence.

## Proven resources

| Function | Resource | Evidence/constraint |
|---|---:|---|
| CONN MCU and chip ID | `0x18070000`, length `0x1000`; ID offset `0x8` | independent chip-ID audit source and vendor `conn_soc` |
| CONN power domain | domain `3` | MT6572 SCPSYS/genpd audit |
| VCN supplies | VCN18 = 1.8 V; VCN28 = 2.8 V | MT6323 regulator table/DTS |
| CONN wake | `GIC_SPI 122`, low level | `MT_CONN2AP_BTIF_WAKEUP_IRQ_ID` |
| CONN WDT | `GIC_SPI 124`, low level | `MT_CONN_WDT_IRQ_ID` |
| WLAN AHB HIF | `0x180f0000`, length `0x5c` | vendor integrated `ahb.c` |
| WLAN HIF IRQ | `GIC_SPI 123`, low level | `MT_WF_HIF_IRQ_ID` |
| AP GDMA | `0x11000100`, length `0x38` | vendor `ahb_gdma.c`; later gate only |
| AP BTIF registers | physical `0x1100e000`, length `0x70` | vendor virtual `BTIF_BASE=0xf100e000`, IO mapping |
| AP BTIF data IRQ | `GIC_SPI 52` | `MT_BTIF_IRQ_ID = 32 + 52` |
| BTIF DMA IRQs | `GIC_SPI 41/42` | vendor TX/RX DMA IRQ definitions; not part of first BTIF gate |
| shared CONN EMI | `0x80000000..0x800fffff` | Z1 layout evidence and reserved-memory node |
| firmware shared area | `0x80080000`, size `0x55c00` | vendor CONNSYS firmware ABI |

`GIC_PRIVATE_SIGNALS` is 32 in the vendor MT6572 headers. Thus the vendor
`MT_BTIF_IRQ_ID = GIC_PRIVATE_SIGNALS + 52` maps to SPI 52; it is not the same
interrupt as wake SPI 122. IRQ 122 alone is insufficient for BTIF data.

## Firmware evidence

The only currently accepted SOC-mode WiFi blob is:

```text
out/firmware_backup/firmware/WIFI_RAM_CODE_SOC
size=160464
sha256=f90f7d4a56b970a3dca31585c50be621a657b63cec6578c84380519e2b0a46f8
header=MTKW
platform_field_at_0x10=0x30
```

The actual WMT config is 80 bytes, not the older 66-byte documentation
record:

```text
out/firmware_backup/firmware/WMT_SOC.cfg
size=80
sha256=cea0a94b8a88e9c24e6835446ebc3106d7b100c3709ea98e8bb7368689c8a2b5
```

These hashes are evidence only. They do not establish the mainline firmware
filename or download protocol. The vendor integrated source uses an Android
firmware path and old file APIs; do not infer a mainline ABI from the blob
name or from the external MT6620 driver.

## Required gate order

1. Run the repaired read-only MT6323 state probe on the approved MFD-only
   image and archive VCN18/VCN28 state plus IRQ evidence.
2. Board-test the isolated VCN18/VCN28 registration-only candidate.
3. Board-test the isolated chip-ID-only consumer.
4. Mainline the minimum BTIF binding with AP data IRQ 52 and CONN wake IRQ
   122 as distinct resources. No firmware, HCI, WMT full stack or DMA in this
   gate.
5. Mainline WLAN AHB PIO at `0x180f0000`/SPI 123 without GDMA or firmware.
6. Add AP GDMA as a separate gate.
7. Only then close the firmware/shared-EMI ABI and functional WiFi/BT tests.

No GPIO, pinmux, clock parent, reset line or firmware transfer sequence is
added by this audit. Those items require board/vendor netlist evidence.
