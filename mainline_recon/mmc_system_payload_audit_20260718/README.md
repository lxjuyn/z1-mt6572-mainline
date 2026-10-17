# Z1 MT6572 MMC / system-payload audit — 2026-07-18

## Scope

This is a read-only audit of the currently built Z1 MMC configuration and the
factory `system.img`.  It does not mount the image, write a partition, alter a
DTS, or start an MMC controller.

## Proven facts

- The factory scatter declares MT6572, eMMC storage, and `MSDC_0` as the boot
  channel.  It allocates a 6 MiB boot image and a 900 MiB Android/system
  partition.
- The current mainline DTS enables `mmc0@11120000` as a non-removable MMC host
  and supplies an eight-data-line MC0 pin group, while the same node currently
  declares `bus-width = <1>`.
- The current build enables `MMC_MTK`, but disables `MFD_MT6397`.  The enabled
  MMC node consumes MT6323 VEMC3V3 and VIO18 regulator phandles, so no eMMC
  board candidate is justified until the separate strict MT6323/MFD regulator
  gate has been board-proven.
- `mmc1@11130000` remains disabled.  There is no Z1-specific removable-slot,
  card-detect, write-protect, rail, or connector evidence; it must not be
  presented as TF/microSD support.

The vendor `cci72_we_jb3` source is only an unbound reference: it describes an
8-data-pin MSDC0 eMMC configuration under `MTK_EMMC_SUPPORT`, but it is not by
itself proof that the source exactly matches this Z1 wiring.

## Required gate order

1. Board-prove the existing strict MT6323/MFD regulator gate independently.
2. Build a dedicated **read-only MMC0** candidate from the exact source/config
   with no `mmc1` enable and no system-image mutation.  Do not change the
   one-bit bus width merely because the pin group contains eight data pins.
3. Boot it and collect a complete log showing MMC0 probe, both regulator
   results, clock/pinctrl/IRQ state, and a stable eMMC enumeration.
4. Only after step 3, inspect block-device partitions read-only and mount the
   factory system image read-only from the device.  Compare its SHA256 with
   the preserved factory image before any replacement plan.
5. A Linux-only system partition is a later, separate transactional flashing
   project with a verified factory rollback image.  It is not part of this
   audit.

`validation.txt` reporting `VALIDATION=PASS` means the evidence chain and its
safety conclusion are internally consistent.  It does **not** mean Linux has
enumerated eMMC or that TF support exists.
