# MT6572 AP BTIF resource-contract compile audit

Status: **COMPILE/DTB AUDIT ONLY — NOT A BOARD CANDIDATE**

This isolated fork adds a Z1-local, default-disabled platform node and a
validator driver for the recovered AP BTIF resource contract:

- MMIO window: `0x1100e000`, length `0x70`;
- AP BTIF data interrupt: GIC SPI 52, level-low;
- CONN-to-AP wake interrupt: GIC SPI 122, level-low;
- top clock: `CLK_TOP_BTIF` (67);
- CONN power domain: ID 3;
- VCN18 and VCN28 supply phandles.

The driver parses and compares Device Tree data only. It does not map or read
MMIO, request an interrupt, obtain or enable a clock/regulator, resume a power
domain, manipulate reset/GPIO, use DMA/shared EMI, or request firmware.
The audit also checks the expected provider identities: MT6572 sysirq,
topckgen and CONN power controller, plus the MT6323 VCN18/VCN28 regulator
nodes.

The node is present only in `mt6572-z1.dts` and remains `status = "disabled"`.
The source fork has no regular hardlinked files. The result is a W=1 object
compile plus a targeted Z1 DTB compile; it is not a full kernel link.
No zImage or boot image is produced by this audit.

## Gate order

This result cannot skip the existing board gates:

1. formally close the strict MFD-only gate;
2. execute the read-only MT6323 rail snapshot;
3. execute the VCN18/VCN28-only registration gate;
4. execute the isolated CONNSYS chip-ID gate;
5. only then design a separate BTIF hardware gate.

The first BTIF hardware gate must still establish clock/pinctrl and
power-sequencing behavior. This compile audit is not an HCI, WMT/STP,
firmware, DMA or wakeup implementation.
