# MT6572 integrated WLAN AHB PIO resource-contract audit

Status: **COMPILE/DTB AUDIT ONLY — NOT A BOARD CANDIDATE**

This isolated audit validates the vendor-recovered integrated WLAN AHB-HIF
resources:

- MMIO window: `0x180f0000`, length `0x5c`;
- WLAN HIF interrupt: GIC SPI 123, level-low;
- interrupt provider: the MT6572 sysirq provider.

The driver only parses the Device Tree resource contract. It does not map or
access MMIO, request an IRQ, enable power or clocks, configure AP GDMA/shared
EMI, or load firmware. The Z1 node remains `status = "disabled"`.

This is the PIO-resource step after the chip-ID and BTIF static audits. It
does not establish WLAN power sequencing, pinctrl, firmware ABI, DMA or
functional WiFi operation. No zImage or boot image was produced.
