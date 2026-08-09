# Jabo Z1 Hearing Aid — mainline Linux port

> Porting the MT6572 hearing-aid device "Z1" from its vendor kernel 3.4.67
> to mainline Linux 7.0-rc7.
> This repository is the build tree: mainline 7.0-rc7 sources + Z1 board
> adaptation drivers.

[中文版 README (Chinese)](README_CN.md)

## Hardware

| Item | Value |
|---|---|
| SoC | MediaTek MT6572 (Cortex-A7 dual-core, 1GB RAM) |
| Vendor kernel | 3.4.67 (`#1 SMP hongyao@R630`, gcc 4.7, 2022-08) |
| Storage | eMMC 4GB (H4G1d 3.64GiB, boot0/boot1/rpmb + p1..p6) |
| Display | Sitronix ST7785M 240x320 DSI |
| Keys | 8-key matrix + PMIC power key + 3 capacitive touch (back/home/multitask) |
| Bridge | ESP32-C3 SuperMini USB-UART ↔ Z1 UART1 (ttyMT0, 115200) |

## Done

### eMMC enumeration fixes (verified on board)

1. **CMD1 stuck-busy root cause** — LK hands the eMMC to the kernel while the
   card is busy, so CMD1 gets no response (resp=0).
   Fix: `mtk-sd.c` pulses BOOTRST in probe via `msdc_z1_emmc_hw_reset()` to
   release the card.
2. **NULL-supply oops** — the Z1 dts `&mmc0` has no `vmmc-supply`, so
   `mmc->supply.vmmc = NULL`, but `msdc_ops_set_ios` used `!IS_ERR()` which does
   not catch NULL → crashed into `regulator_set_voltage(NULL)`.
   Fix: 5 supply guards changed to `IS_ERR_OR_NULL`, `msdc_init_hw` hoisted out
   of the guard. Commit `8bea81fa5`.
3. Verified full enumeration: `mmc0: new MMC H4G1d 3.64GiB` + boot0/boot1/rpmb +
   p1..p6 all registered.

### Peripheral drivers (earlier rounds)

- WiFi / WMT / BT external .ko all build (wlan_mt6620, mtk_stp_wmt,
  mtk_hif_sdio, etc., 7 modules)
- All 13 keys built into zImage (matrix + PMIC power key + goodix 3 keys)
- USB CDC-ACM gadget (musb mediatek + f_acm)
- GPIO/EINT/RTC no-op stubs (`stub_includes/`)

## Current blocker: block-read R1B busy

Enumeration succeeds, but `cat /dev/mmcblk0p2` hangs:

```
mmc0: Card stuck being busy! __mmc_poll_for_busy
mtk-msdc 11120000.mmc: msdc_cmd_done: cmd=6 arg=03B34801; cmd_error=-110
```

- `cmd=6 arg=03B34801` = CMD6 SWITCH writing `EXT_CSD_PART_CONFIG`(179)=0x48
  (partition switch to user before every I/O)
- R1B responds fine (rsp 00000900 = TRAN/READY_FOR_DATA), but **DAT0 stays low**
  after the response
- `msdc_card_busy` reads `MSDC_PS bit16` = DAT0 level to detect busy, never
  sees it release → -110
- Excluded: VEMC power drop (pwrap write-deny blocks it), PB1 BUSY_CHECK_SEL,
  pinctrl default (breaks CMD1 when applied), data timeout (5s watchdog never
  fires), DMA/clock (enumeration already works)

> Investigation direction: is `MSDC_PS bit16` really the DAT0 pin level /
> real card busy / electrical pinmux.

## Build

```bash
# zImage + dtb
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage dtbs
# artifacts: arch/arm/boot/zImage + arch/arm/boot/dts/mediatek/mt6572-z1.dtb
```

Package boot.img (appdtb mode: cat dtb + mkbootimg with --dt):

```bash
cat arch/arm/boot/zImage arch/arm/boot/dts/mediatek/mt6572-z1.dtb > /tmp/zImage_appended
./build/mtkbootimg/mkbootimg --kernel /tmp/zImage_appended \
  --ramdisk <MTK ROOTFS ramdisk.gz> \
  --dt arch/arm/boot/dts/mediatek/mt6572-z1.dtb \
  --base 0x10000000 --kernel_offset 0x8000 --pagesize 2048 --mtk 1 \
  --cmdline "console=tty0 console=ttyS0,115200n8 root=/dev/ram panic=0 ignore_loglevel clk_ignore_unused rdinit=/init" \
  -o boot.img
```

Flash with mtkclient: `tools/flash_with_retry.sh bootimg <img> <rollback_img> 8`.
> Note: plugging Z1 into USB disturbs boot (preloader checks LineState) — only
> plug the download cable for flashing, unplug for normal boot.

## Key files

| File | Purpose |
|---|---|
| `drivers/mmc/host/mtk-sd.c` | MSDC eMMC driver (Z1: BOOTRST / IS_ERR_OR_NULL / z1_direct_cid) |
| `arch/arm/boot/dts/mediatek/mt6572-z1.dts` | Z1 board dts (mmc0/uart/usb/keys/pmic) |
| `arch/arm/boot/dts/mediatek/mt6572.dtsi` | MT6572 SoC dtsi |
| `drivers/pinctrl/mediatek/pinctrl-mt6572.c` | MT6572 pinmux (R1R0 pull support) |
| `stub_includes/` | Stub headers (wakelock/earlysuspend/aee/xlog/mach) |

## History

```
629d81675 dts: revert pinctrl default for mmc0 - breaks CMD1
d7d5597aa fix: keep eMMC VCC (VEMC) powered - regulator-always-on
d7fc80d98 revert: eMMC PB1 restore under z1_direct_cid - breaks CMD1
7a74f3153 fix: eMMC R1B busy misdetect - restore vendor PB1 under z1_direct_cid
8bea81fa5 fix: eMMC probe NULL-supply oops - gate vmmc/vqmmc with IS_ERR_OR_NULL
7f926bfac fix: full-enumeration eMMC - drop CID-only gates (BOOTRST kept)
37818733b import: mainline 7.0-rc7 + Z1 MT6572 fork 驱动基线
```
