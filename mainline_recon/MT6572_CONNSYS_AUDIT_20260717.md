# MT6572 integrated CONNSYS audit — 2026-07-17

## Correct architecture

Z1 uses the MT6572 SoC-integrated connectivity block, not an external MT6620 combo chip:

- `mediatek/config/cci72_we_jb3/ProjectConfig.mk`: `MTK_COMBO_CHIP=MT6572_CONSYS`, BT=`MTK_CONSYS_MT6572`, GPS=`MTK_GPS_MT6572`, FM=`MT6627_FM`.
- Correct vendor tree: `mediatek/kernel/drivers/conn_soc/`.
- Correct control/transport/HIF: `wmt_ic_soc.c`, `stp_btif.c`, integrated WLAN AHB HIF.
- The previously compiled `wlan_mt6620.ko`, `mtk_hif_sdio.ko`, STP-UART and external GPIO PMU/RST stack are compile references only and are superseded for Z1.

The stock path does not need the external Wi-Fi SDIO power gate (`wmt_func.c` and `wmt_ctrl.c` disable that path under `#if 0`). The WLAN Makefile builds `hif/ahb/ahb.o` and MT6572 `ahb_gdma.o`; `mmc1` is not the next Wi-Fi gate.

## Proven physical resources

Vendor virtual IO uses `IO_VIRT_TO_PHYS(v) = 0x10000000 | (v & 0x0fffffff)`.

| Resource | Physical address | Evidence |
|---|---:|---|
| TOPCKGEN | `0x10000000` | `mt_reg_base.h`, static IO map |
| INFRACFG AO | `0x10001000` | same |
| SPM | `0x10006000` | same |
| AP RGU | `0x10007000` | same |
| CONN MCU config | `0x18070000` | virtual `0xf8070000` |
| CONN chip ID | `0x18070008` | expected `0x6572` or `0x6582` |
| CONN TOP CR | `0x180b0000` | virtual `0xf80b0000` |
| WLAN AHB HIF | `0x180f0000`, length `0x5c` | vendor AHB HIF |
| AP DMA HIF | `0x11000100`, length `0x38` | `hif_gdma.h` / `ahb_gdma.c` |
| BTIF wake IRQ | GIC SPI 122, low | `mt_irq.h` |
| WLAN HIF IRQ | GIC SPI 123, low | `mt_irq.h`, `ahb.c` |
| CONN WDT IRQ | GIC SPI 124, low | `mt_irq.h` |

Vendor power flow in `mtk_wcn_consys_hw_reg_ctrl()` enables MT6323 VCN18/VCN28, clears TOPCKGEN bit 26 at `0x10000084`, writes SPM unlock/config `0x0b160001`, calls `conn_power_on(SYS_CON)`, waits 10 us, then polls chip ID. Power control is `0x10006280`; status ACK registers are `0x1000660c`/`0x10006610`, bit 1. Bus protection uses INFRACFG bits 0 and 4.

Do not copy these raw writes into a WLAN driver. Mainline must express them through MT6323 regulators, clocks and the MT6572 SCPSYS domain, with each transition independently tested.

## Z1 carveout is proven; current mainline layout is wrong

The stock Z1 log proves:

- CONNSYS: `0x80000000..0x800fffff` (1 MiB).
- Linux `get_phys_offset()`: `0x80100000`.
- Linux usable kernel RAM begins at `0x80100000`.
- Vendor source defines `RESERVED_MEM_SIZE_FOR_CONNSYS=0x100000` and `PHYS_OFFSET=DRAM start + 1 MiB`.
- CONNSYS firmware shared EMI is `0x80080000`, size 343 KiB (`0x55c00`), inside that carveout; vendor init maps and clears it.

Current mainline declares RAM at `0x80000000`. Its appended-DTB zImage uses `CONFIG_AUTO_ZRELADDR=y`; the decompressor takes `pc & 0xf8000000` and outputs the kernel at `0x80008000`. Therefore the kernel occupies the carveout before reserved-memory is processed, exactly matching the board log:

`Reserved memory: failed to reserve memory for node 'connsys@80000000'`.

A direct mainline `PHYS_OFFSET=0x80100000` is not safe with `CONFIG_ARM_PATCH_PHYS_VIRT`: `__fixup_pv_table` requires 2 MiB alignment and deadloops otherwise. The first layout experiment must therefore be isolated. The conservative candidate is to reserve the first 2 MiB and use fixed `PHYS_OFFSET=0x80200000` / `ZRELADDR=0x80208000`, accepting 1 MiB waste; exact Kconfig/decompressor handling must be compile-audited before packaging. Do not enable any CONNSYS consumer until this layout-only image boots with USB and memory accounting intact.

## Safe batch completed

Compile-only archive:

`bridge_backups/products/products_mt6572_conn_genpd_compile_20260717_180122/`

It adds a local non-conflicting CONN domain ID 3, exact status/control/bus-protection data and `MTK_SCPD_KEEP_DEFAULT_OFF`, plus a DTS provider child. There is no consumer, clock, regulator, IRQ, MMIO or EMI mapping, so provider probe cannot request CONNSYS power-on. zImage and Z1 DTB compile passed; config remained byte-identical.

## Required sequence

1. Keep external MT6620/SDIO modules quarantined.
2. Simplefb/Lima and the 2 MiB layout-only board gates are complete.
3. Board-test the strict MT6323 MFD-only candidate.
4. Independently design and board-test MT6323 regulator registration; the
   existing driver registers the full rail table, not only VCN18/VCN28.
5. Promote the compiled chip-ID-only CONNSYS consumer only after steps 3 and
   4 pass: VCN18/VCN28 + genpd, map only MCU config, read ID, unwind; no
   IRQ/HIF/EMI/firmware.
6. Port SOC WMT + BTIF using IRQ 122.
7. Port WLAN AHB HIF using `0x180f0000`, IRQ 123; DMA remains a later sub-batch.
8. Map/clear EMI only after the layout-only gate passes and only as an explicit firmware ABI step.

## Layout Kconfig probe result

A separate `KCONFIG_CONFIG` probe attempted to disable `AUTO_ZRELADDR` and set `PHYS_OFFSET=0x80200000`. `make olddefconfig` restored the configuration byte-for-byte to the current config because both options are hidden/defaulted by `ARCH_MULTIPLATFORM` + `ARM_PATCH_PHYS_VIRT`. Evidence:

- `mainline_recon/layout_2m_kconfig_test.config`
- `mainline_recon/layout_2m_kconfig_test.log`

Therefore the layout-only batch cannot be produced by editing `.config` alone. It needs an explicit, backed-up source-level Kconfig/decompressor mechanism for the Z1-only build, followed by symbol/address inspection before any image is packaged.

## 2 MiB-aligned layout compile audit completed

A default-off `CONFIG_MT6572_Z1_FIXED_ZRELADDR` mechanism was added to the MediaTek ARM Kconfig/decompressor. The isolated build enabled it and temporarily changed the Z1 memory node to `0x80200000` size `0x3fe00000`. Results:

- compressed vmlinux contains one `0x80208000` literal and zero `0x80008000` literals;
- DTB contains RAM `0x80200000..0xbfffffff` and preserves CONNSYS `0x80000000..0x800fffff`;
- zImage/DTB compiled successfully; no boot image was packaged;
- archive: `bridge_backups/products/products_mt6572_z1_layout2m_compile_20260717_183638/`;
- normal Z1 DTS was restored byte-identically; normal config has the new option explicitly disabled and the rebuilt decompressor contains zero fixed-address literals.

The remaining risk is runtime overlap/relocation behavior when LK loads the compressed zImage at `0x80108000` and it expands at `0x80208000`. ARM zImage has explicit self-relocation overlap handling, but this still requires a dedicated board gate after simplefb v2, never in the same first test.

## Layout board result

The dedicated layout gate passed on 2026-07-17. Linux System RAM begins at
`0x80200000`; simplefb, Lima and USB gadget remained healthy. This proves only
the memory layout. It did not enable or power CONNSYS.

## Chip-ID-only compile audit

An isolated default-off audit driver now exists under:

`mainline_recon/fork_linux_connsys_chipid_audit_src_20260717/`

It acquires VCN18/VCN28, resumes the CONN genpd, reads only
`0x18070008`, performs synchronous runtime-PM unwind and disables the rails.
It does not request IRQs, clocks or reset lines and does not map EMI, load
firmware, perform DMA or write WLAN/BTIF registers.

Results:

- W=1 source/object compile passed with no source diagnostics;
- object SHA256
  `17dd95cb2121a7d09d53f2522f50392fd66e9c943d70a2342621191f6f96b81f`;
- target DTB compiled with the audit node disabled;
- no zImage or boot image was produced.

This is not board-eligible until strict MFD-only and the separate full
regulator-registration gate both pass. Runtime PM/genpd and rail sequencing
remain compile-checked but unobserved on hardware.
