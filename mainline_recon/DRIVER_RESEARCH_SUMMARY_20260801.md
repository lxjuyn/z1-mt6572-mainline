# Z1 其他驱动调研总结 + 夜间进展 (2026-08-01)

## 调研结论（2026-07-31 夜间 5 个 agent 并行调研）

### 1. 音频 (MT6323 codec + MT6572 AFE) — 4 步移植方案
- **mainline 无 mt6397.c codec**（历史上不存在，只有 mt6357/58/59）。MT6323 与 MT6397 不是寄存器级克隆（SPK_CON 0x52 vs 0x600，MT6323 有 ABB_AFE 0x4000 段）。
- MT6323 寄存器：`include/linux/mfd/mt6323/registers.h`（SPK_CON0@0x52-0x6A、AUDTOP_CON0-9@0x700-0x712、ABB_AFE@0x4000）。bit 语义要从 vendor `AudDrv_Kernel.c` 反推。
- MT6572 AFE：物理基址 **0x11140000**（32-bit），DL1/DL2/VUL/AWB memif + ADDA + I2S，寄存器在 vendor `AudDrv_Afe.h`。
- **最大 blocker**：Z1 pwrap 写禁止门（CONFIG_MTK_PMIC_WRAP_Z1_WRITE_DENY）挡掉所有 MT6323 写 → codec 寄存器写不了。编译可先过，出声需处理。
- 方案：新 mt6323.c codec（照 mt6357.c 骨架）→ 新 mt6572-afe-pcm.c（照 mt2701）→ machine driver → config。

### 2. WiFi (MT6572 CONNSYS) — 外部 MT6620 路线废弃
- **Z1 用集成 CONNSYS，不是外部 MT6620**！7 个 .ko 已被 superseded（`products_common_all6_205255/Z1_SUPERSEDED_EXTERNAL_MT6620_NOTICE.txt`）。
- CONNSYS 资源：chipID@0x18070008、WLAN AHB HIF@0x180f0000（SPI 123）、EMI@0x80080000。
- 上电序列：MT6323 VCN18/VCN28/VCN33 → TOPCKGEN 时钟门控 → SPM conn_power_on(SYS_CON) → 轮询 chip ID 0x6572 → EMI 清零。
- 前置：`&pmic`/`&mt6323regulator` 必须使能（夜间已完成，emmc_full5）。
- 参考：`mainline_recon/MT6572_CONNSYS_AUDIT_20260717.md`。

### 3. 其他驱动缺口（recon 盘点）
- **`CONFIG_I2C_MT65XX` 未编**（i2c1 控制器）→ 夜间已开。
- **PMIC 全 disable**（&pmic/&mt6323regulator）→ 夜间已开，PMIC_EINT=GPIO7。电源键/RTC/regulator 现可 probe。
- Goodix GT911：驱动已编但节点 disabled（硬件未证实，保持）。
- 显示：DRM_MEDIATEK/SMI/IOMMU 未编；**ST7785M 需新写 panel 驱动**（DSI 控制器 mtk_dsi 已支持 mt6572，PHY 用 mt2701-mipi-tx）。simplefb 兜底工作中。
- RTC_DRV_MT6397 / KEYBOARD_MTK_PMIC / REGULATOR_MT6323 夜间已开。

### 4. 显示 (ST7785M)
- mainline 无 ST7785；st7789v.c 是 SPI 接口，命令集同族可参考。需新写 `mipi_dsi_driver`。
- LK 实测时序：240x320、1-lane DSI video、data_rate=140MHz、txdiv=4、VSA=3/VBP=6/VFP=8/VACT=320。
- 精确 init 表本地/网上都没有 → 用 ST7789V 同族命令集 + 上板校准。

### 5. USB 交互 (g_serial)
- **MTK ROOTFS 可解包重打包**（512 字节 MTK 头 + gzip-cpio）！`/init` 已有 ttyGS0 getty（line 26），但可能因 /dev/ttyGS0 节点缺失静默死掉。
- `CONFIG_U_SERIAL_CONSOLE` 未开 → console=ttyGS0 无效。夜间已开。
- 修法：改 /init 加等待循环 + 重打包（agent 在做）。

## 夜间改动汇总（emmc_full5）
- eMMC：BOOTRST 复位移入正常 probe + ocr_avail + 400kHz/1bit + **CONFIG_MMC_BLOCK**（mmcblk0 注册）→ 上板实测 `mmc0: new MMC card at address 0001` ✅
- config 新开：MMC_BLOCK / I2C_MT65XX / U_SERIAL_CONSOLE / KEYBOARD_MTK_PMIC / REGULATOR_MT6323 / RTC_DRV_MT6397
- dts：&pmic + &mt6323regulator 使能；console=ttyGS0 加入 cmdline
- 产物：`out/boot_mainline_z1_emmc_full5.img`（5.57MB）+ 归档 `products_emmc_full5_20260801_011514/`

## 实施中（5 个 Sonnet agent）
1. ramdisk ttyGS0 getty 修复 + 重打包
2. mt6323 codec 骨架
3. mt6572 AFE 平台驱动骨架
4. MT6572 CONNSYS 平台层驱动
5. ST7785M panel 驱动骨架
