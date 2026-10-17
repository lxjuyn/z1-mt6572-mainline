# MT6323 VCN18/VCN28 registration-only boot candidate

Status: **HOST PACKAGE + STATIC VALIDATION ONLY / NOT FLASH AUTHORIZED**

This candidate packages the previously validated layout2m VCN registration
audit kernel and DTB with the already board-proven USB diagnostic ramdisk.
The regulator driver registers exactly VCN18 and VCN28, but replaces their
normal operations with a read-only registration-audit operation table.  It
contains no enable, disable, voltage-select or mode-write callback, and the DT
has no VCN consumer supply reference.

The 2026-07-20 prerequisite board evidence has passed:

- strict layout2m MFD-only image write/readback and boot;
- read-only MT6323 CID/28-rail state capture;
- VCN18 and VCN28 both observed off (`sw_en=0`, `qi_on=0`);
- capture completed with `writes=0` and verified module cleanup;
- board-proven layout2m baseline was restored and boot-verified afterward.

Packaging is host-only.  No file from this directory is copied to `out/`, no
flash launcher is created, and no hardware action is authorized by the
existence of this image.  A separate review must promote it to a board gate
and bind exact target/rollback/factory hashes before the user is asked to
power down or connect a download cable.

The later CONNSYS chip-ID node is not included or enabled in this candidate.
Wi-Fi, Bluetooth, BTIF, WLAN HIF, DMA, firmware and shared EMI remain outside
this gate.
