# Z1 mainline baseband resource gap matrix (2026-07-20)

Scope: independent read-only evidence artifact for comparing available Z1 factory/system evidence against the current Z1 mainline DTS/config. This directory only records evidence; it does not change DTS, Kconfig, `system.img`, boot images, partition images, or flashing scripts.

Mandatory NO-GO tokens: `NO_MODEM_START`, `NO_MODEM_RESET`, `NO_SYSTEM_WRITE`, `NO_HARDWARE_COMMANDS`, `NO_FLASHING`.

Additional evidence token used where appropriate: `EVIDENCE_ABSENT`.

## Method and evidence inputs

- Current mainline board DTS: `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts`.
- Current MT6572 SoC DTSI: `fork_linux/arch/arm/boot/dts/mediatek/mt6572.dtsi`.
- Current mainline config: `fork_linux/.config`.
- Factory/system evidence:
  - `1/system.img` was inspected read-only with `debugfs -R 'ls -p ...'`; no mount and no write were performed.
  - Existing extracted payload subset: `out/z1_factory_drv/{WMT_SOC.cfg,wmt_loader,wlan_loader}`.
  - Existing notes: `mainline_recon/W2_FACTORY_DUMP.md` and `mainline_recon/SYSTEM_PARTITION_DRIVER_PAYLOAD_PLAN_20260718.md`.
- Manifests in this directory record the exact input and output SHA-256 values.

## Mainline DTS/config anchors

- Board identity: `model = "Z1 hearing-aid"` and `compatible = "jbo,z1", "mediatek,mt6572"` in `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts:15-18`.
- Main RAM starts at `0x80000000` and is `0x40000000` bytes in `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts:43-46`.
- `reserved-memory` exists, but only for CONNSYS, framebuffer, and CMA: `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts:48-70`.
- The only radio-adjacent reserved carveout currently named in the DTS is `connsys@80000000`, `reg = <0x80000000 0x100000>`, `no-map`: `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts:53-56`.
- CONN power domain provider exists default-off with no consumer: `fork_linux/arch/arm/boot/dts/mediatek/mt6572.dtsi:245-277`, especially `power-domain@MT6572_POWER_DOMAIN_CONN` at `:271-276`.
- Watchdog/TOPRGU provider exists (`#reset-cells = <1>`), but this is not an MD RGU node: `fork_linux/arch/arm/boot/dts/mediatek/mt6572.dtsi:280-286`.
- No DTS/DTSI hits were found for `ccci`, `ccif`, `md_rgu`, `md-rgu`, `mau`, `emi`, `modem`, `md1`, or `md2` under `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts` and `fork_linux/arch/arm/boot/dts/mediatek/mt6572.dtsi`.
- Config anchors: `CONFIG_MACH_MT6572=y` at `fork_linux/.config:317`; `# CONFIG_MTK_SCP is not set` at `fork_linux/.config:5437`; `CONFIG_MTK_SCPSYS=y` and `CONFIG_MTK_SCPSYS_PM_DOMAINS=y` at `fork_linux/.config:5549-5550`. No `CONFIG_*CCCI*`, `CONFIG_*CCIF*`, or `CONFIG_*MODEM*` hits were found in `fork_linux/.config`.
- MediaTek SoC driver Makefile currently lists only CMDQ/DEVAPC/DVFSRC/INFRACFG/PMIC_WRAP/REGULATOR_COUPLER/MMSYS/MUTEX/SVS/SOCINFO objects, with no CCCI/CCIF/modem object: `fork_linux/drivers/soc/mediatek/Makefile:1-11`.

## Factory/system anchors

These are unique path evidence from `debugfs` read-only listings of `1/system.img` unless a source line is stated.

- Factory identity note confirms Z1/MT6572: `mainline_recon/W2_FACTORY_DUMP.md:6-11`.
- Factory notes state `/lib/modules/wlan.ko` is an 8-byte placeholder and that no loadable `wlan.ko/wmt.ko/stp.ko/hci.ko` exists in system: `mainline_recon/W2_FACTORY_DUMP.md:15`.
- Factory WiFi/BT payload notes list `/etc/firmware` and WMT files: `mainline_recon/W2_FACTORY_DUMP.md:19-33`.
- Factory modem firmware payload is present in `1/system.img`: `/etc/firmware/modem_1_2g_n.img` size `2399148`; `/etc/firmware/catcher_filter_1_2g_n.bin` size `1367`.
- Factory modem database is present in `1/system.img`: `/etc/mddb/BPLGUInfoCustomAppSrcP_MT6572_S00_MOLY_WR8_W1315_MD_WG_MP_V36_P1_1_2g_n` size `2316206`.
- Factory CCCI/RIL/user services are present in `1/system.img`: `/bin/ccci_fsd` size `96120`, `/bin/ccci_mdinit` size `38660`, `/bin/rild` size `9512`, `/bin/rildmd2` size `9512`, `/bin/gsm0710muxd` size `63032`, `/bin/gsm0710muxdmd2` size `67128`.
- Factory modem log/diagnostic services are present in `1/system.img`: `/bin/mdlogger` size `128736`, `/bin/dualmdlogger` size `124636`, `/bin/ecccimdlogger` size `112680`, `/bin/emcsmdlogger` size `112680`, `/bin/md_minilog_util` size `5292`.
- Factory modem/RIL libraries are present in `1/system.img`: `/lib/libem_modem_jni.so`, `/lib/libril.so`, `/lib/librilmtk.so`, `/lib/librilmtkmd2.so`, `/lib/libreference-ril.so`, `/lib/libutilrilmtk.so`, `/lib/mtk-ril.so`, `/lib/mtk-rilmd2.so`.
- Factory system image contains no rc/init service file under `/`, `/etc`, or `/vendor` in the read-only listing. Service start order and arguments are therefore `EVIDENCE_ABSENT` from the currently available persistent factory unpack evidence.
- Existing system payload plan states current mainline boots `root=/dev/ram` and does not automatically mount Android system: `mainline_recon/SYSTEM_PARTITION_DRIVER_PAYLOAD_PLAN_20260718.md:32-40`.
- Existing system payload plan also explicitly forbids in-place `system.img` modification: `mainline_recon/SYSTEM_PARTITION_DRIVER_PAYLOAD_PLAN_20260718.md:42-49` and `:57-67`.

## Present / absent / ambiguous resource gap matrix

| Resource class | Factory/system evidence | Current Z1 mainline DTS/config evidence | Verdict | Gap / next evidence needed |
|---|---|---|---|---|
| CCCI kernel/device node | Userland CCCI binaries exist: `/bin/ccci_fsd`, `/bin/ccci_mdinit`. Modem firmware and DB exist under `/etc/firmware` and `/etc/mddb`. | No `ccci` hits in `mt6572-z1.dts`, `mt6572.dtsi`, or `fork_linux/.config`; no CCCI object in `drivers/soc/mediatek/Makefile:1-11`. | **ABSENT in mainline** | Need vendor kernel/DTS/register inventory for MT6572 CCCI before any driver or node can be proposed. `EVIDENCE_ABSENT` for register tuple. |
| CCIF mailbox/interrupt | Factory CCCI services imply a vendor CCCI/MD IPC stack, but system image path evidence alone does not expose CCIF register bases, IRQs, clocks, or reset lines. | No `ccif` hits in current Z1 DTS/DTSI/config. | **ABSENT / AMBIGUOUS** | Missing authoritative MT6572 CCIF AP<->MD register and IRQ evidence. Do not infer from other MediaTek SoCs. `EVIDENCE_ABSENT`. |
| MD RGU / modem reset | Factory has `/bin/ccci_mdinit`, but no persistent unpack evidence here maps its reset sequence to a DTS resource. | Generic watchdog/TOPRGU provider exists at `mt6572.dtsi:280-286`; no `md_rgu`/`md-rgu`/`modem reset` node or config. | **ABSENT / AMBIGUOUS** | Need vendor kernel reset definitions or bootloader/kernel symbols. `NO_MODEM_RESET`; do not test reset lines. |
| EMI / MD shared memory controller | Factory modem payload exists (`/etc/firmware/modem_1_2g_n.img`), but no current factory unpack evidence identifies modem EMI windows, security, or ownership. | No `emi` node or modem shared-memory carveout in Z1 DTS/DTSI. Existing `memory@80000000` and `reserved-memory` only define RAM/CONNSYS/framebuffer/CMA. | **ABSENT / AMBIGUOUS** | Need vendor LK/kernel memory layout or baseband inventory. `EVIDENCE_ABSENT` for modem EMI layout. |
| MAU / memory access protection | No factory path or notes prove MAU region programming for modem. | No `mau` hits in current DTS/DTSI/config. | **ABSENT** | Need MT6572 MAU register map and factory programmed ranges before modeling. `EVIDENCE_ABSENT`. |
| Reserved memory for modem | Factory firmware image and MD database exist, but no persistent factory unpack evidence identifies a modem reserved-memory address/size. | `reserved-memory` exists at `mt6572-z1.dts:48-70`, but entries are only `connsys@80000000`, framebuffer, and CMA. | **ABSENT for modem; PRESENT for non-modem CONNSYS/framebuffer/CMA** | Add nothing until modem carveout is proven. Do not reuse CONNSYS carveout as modem memory. |
| Modem firmware payload | `/etc/firmware/modem_1_2g_n.img`, `/etc/firmware/catcher_filter_1_2g_n.bin`, and `/etc/mddb/BPLGU...` are present in `1/system.img`. | Current mainline boot path is `root=/dev/ram`; no automatic Android system mount (`SYSTEM_PARTITION...:32-40`); no modem firmware loader/config. | **PRESENT in factory; ABSENT in mainline runtime path** | Payload exists, but mainline has no safe mount/load path and no kernel ABI. `NO_MODEM_START`. |
| Modem services / RIL / logging | `/bin/ccci_fsd`, `/bin/ccci_mdinit`, `/bin/rild*`, `/bin/gsm0710muxd*`, `/bin/mdlogger*`, RIL libraries are present. | No mainline init/service integration; no Android bionic userspace runtime in current initramfs; no rc service file found in persistent factory system evidence. | **PRESENT in factory; ABSENT/AMBIGUOUS in mainline** | Need boot/vendor init evidence for ordering/args and a Linux userspace policy. Do not run Android modem services. |
| CONNSYS radio-adjacent carveout | Factory notes list WiFi/BT firmware/loaders (`W2_FACTORY_DUMP.md:19-33`). | `connsys@80000000` reserved memory exists at `mt6572-z1.dts:53-56`; CONN genpd provider exists at `mt6572.dtsi:271-276`, default-off with no consumer. | **PRESENT but not baseband modem** | Keep isolated from modem work; do not treat CONNSYS evidence as CCCI/MD evidence. |

## Hard safety conclusion

The factory system image clearly contains modem payloads and CCCI/RIL userland pieces, but the current Z1 mainline DTS/config does not contain CCCI, CCIF, MD RGU, modem EMI/shared-memory, MAU, modem reserved-memory, modem firmware loader, or modem services. Therefore the safe matrix result is: factory userland/payload **present**, mainline modem hardware/resource model **absent**, and the register/IRQ/reset/memory details needed to fill it are **ambiguous / EVIDENCE_ABSENT**.

Do not start, reset, wake, or probe the modem from this artifact: `NO_MODEM_START`, `NO_MODEM_RESET`, `NO_SYSTEM_WRITE`, `NO_HARDWARE_COMMANDS`, `NO_FLASHING`.
