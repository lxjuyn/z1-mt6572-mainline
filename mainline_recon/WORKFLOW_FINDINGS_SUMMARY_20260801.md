# Z1 夜间 Workflow/Agent 发现汇总 (2026-08-01 02:00)

## 集成路线图（w8f1x08at workflow，依赖排序）
- **#1 blocker: 关 `CONFIG_MTK_PMIC_WRAP_Z1_WRITE_DENY`**（挡所有 MT6323 写：regulator/RTC/keys/codec/CONNSYS VCN）— **夜间已关** ✅
- TIER A (compile-only): pmic 验证 → usb ramdisk repack → display panel → emmc 提速(8-bit/HS, set_ios+pinctrl 重构) → connsys 平台 → codec → AFE → DRM stack
- TIER B (needs-board): pmic 板上验证 → connsys chipID → emmc 枚举 → emmc 提速 → connsys 消费 → usb 验收 → audio → display

## 音频初始化寄存器表（whozxan6g workflow）
- **AUDTOP_CON5(0x70A)=0x1114**（DAC bias/trim）、**AUDTOP_CON6(0x70C)=0x37A2→0x37E2 两步**（HP depop，bit6 ADEPOPX_EN 第二写置位）
- TOP_CKPDN0(0x102) bit0 音频 26M clksq toggle（power_init 里 1→0）
- SPK_CON0-12(0x52-0x6A) 全 reset 默认（无 boot 写）；ABB_AFE(0x4000) 未编程靠默认
- 其余寄存器无数组循环，只有这两个固定写值 → codec 驱动最小 init 有据可依

## 跨板 dts diff（w2gy6lksp workflow）
- **高价值：`&mt6323keys { power-off-time-sec=<5>; home { status="disabled" }; }`** — PMIC home=KEY_VOLUMEDOWN 与矩阵键盘重复，4 块板都 disable
- 电池：只有 prestigio 有 simple-battery（数据节点，无消费驱动），Z1 无电池节点 → 需开 CONFIG_POWER_SUPPLY + GENERIC_ADC_BATTERY + MEDIATEK_MT6323_AUXADC（auxadc 只读不受 write-deny 挡）
- 显示：4 块板都 enable &dsi+panel（不同 panel），Z1 保持 disabled 正确（ST7785M 无 mainline 驱动）
- mmc0：prestigio/jty 有 vmmc/vqmmc + 50MHz + highspeed；Z1 1-bit/400kHz（提频等 boot 通后）
- 其余：触摸保持 disabled、USB 保持 peripheral、无 SD/加速度计/haptic

## Boot 参数（wora41elc workflow）
- 显示：仅 simplefb（240x320 RGB565 @ 0xbfd00000），无 DSI 驱动 probe
- 内存：1 GiB（0x80000000-0xbfffffff），connsys 保留 1MiB@0x80000000 reserve 失败（no-map 冲突）
- eMMC：src=199.875MHz、实际 400kHz/1bit、CMD1 resp=0xc0ff8080、CID 已捕获、Host SW Queue enabled、card detected
- 时钟：arch_timer 13MHz、UART 26MHz、GPU 416MHz

## 子系统覆盖（wtbndbc5n workflow）
- 已开：clk/pinctrl/regulator/mfd 全 OK
- 未开（低优先/需注意 zImage 大小）：IOMMU_V1/SMI（最险，涉及 DMA）/thermal（需 THERMAL_OF + nvmem 前置）/nvmem efuse
- **关键风险：zImage ≤6MB 预算** — 每开一个 config 要 make clean 重编 + 检查大小

## 厂商驱动清单（wf1cpobqd workflow）
- 显示 DDP → mainline DRM mt6572（mtk_disp_ovl/rdma 有，color/aal 需加 compatible，bls 无）
- 传感器：KXTJ2（无 exact）/AKM8963（ak8975.c 支持）/MPU3050（有）/CM36283（无 exact）— 需确认 Z1 实机有哪些
- 连接：WMT/STP 已移植（mtk_stp_wmt.ko 等），CONNSYS 平台层是 no-op 桩待重写

## USB 复核（a5f35899 agent）
- HS slew-rate "timed out; using 4" 是 **cosmetic warning**（fork 把厂商 3s 等待压到 10ms），fallback=4 只差厂商真值 5 一档，不影响枚举
- DWC2 双角色无冲突（dts 无 dwc2 节点，dwc2 匹配表无 mtk）
- 外设模式 phy 常驻 powered，开机后插 USB 安全可靠（g_serial ready 已证实）

## 键盘矩阵（a1bd56ec agent）
- **8x9=72 槽正确**（驱动硬编码 /9 %9，厂商 KPD_NUM_KEYS=72）；"3 列"是早期误判
- 槽 0=VOLUMEUP(115)、槽 9=VOLUMEDOWN(114)，dts 配置正确
- **Z1 音量±开关未焊**（只有 PCB pad，无法产生事件）→ 矩阵键盘验证受限

## 固件就绪（abd09f93 agent）
- WiFi 固件**全就绪**：驱动实际加载 `/etc/firmware/WIFI_RAM_CODE`（E1-E5 分支，Z1=E3），不是 _SOC
- **待准备**：建 `/etc/firmware/` 目录 + 拷 WIFI_RAM_CODE + WMT_SOC.cfg；需 userspace wmt loader（ioctl）

## 夜间关键改动汇总
- eMMC 完整修复（emmc_full5：BOOTRST 复位 + ocr_avail + 400kHz + MMC_BLOCK）上板验证 `mmc0: new MMC card` ✅
- 新配置：I2C_MT65XX/U_SERIAL_CONSOLE/KEYBOARD_MTK_PMIC/REGULATOR_MT6323/RTC_DRV_MT6397/MEDIATEK_MT6323_AUXADC
- **write-deny gate 已关**（MTK_PMIC_WRAP_Z1_WRITE_DENY=n）
- dts：&pmic/&mt6323regulator 使能 + connsys/audsys 节点 + keypad 8x9
- ramdisk：ramdisk_ttygs0.gz（ttyGS0 getty 等待修复）
- 待做：&mt6323keys home disable + 最终整合构建

## 补充 (2026-08-01 02:20)
- **GPIO 审计**（w8h4nf8n9）：键盘列=107/108/109(KCOL0-2)、行=110/111/112/142/143(KROW0-4)；connsys 引脚 GPIO115-127(CONN_WB_CTRL0-5/CONN_RSTB 等)已连但 mainline 未 mux（connsys 节点 disabled）；DSI 引脚 GPIO79-84 连 ST7785M；PMIC_EINT=GPIO7；MMC 47-53(DAT1-7) 未 mux（1-bit bring-up）
- **bootlog 扫描**（wrwc5iebf）：确认 MTK ROOTFS 是唯一根（CONFIG_INITRAMFS_SOURCE=""）；eMMC CMD1 stuck 间歇性（旧镜像无 hw reset，现 emmc_full5 已根治）；8250 serial probe 5.5s 最慢；connsys reserved-mem 0x80000000 与内核镜像重叠（node disabled 无害，建议 relocation 到 0xbe000000）
- **Hynitron 触摸**（w527vw65m）：0x15 响应 → 优先 hynitron-cst816x（0x15 是 CST816S 默认地址）；GT911 在 0x14/0x5D
- **connsys 内存 relocation**（wmx24qeel）：carveout 移到 0xbe000000 + reg[1] 0xbe080000 保持一致性；EMI 基址 0x80080000 是厂商固定，relocation 是临时的
- **工具复核**（whrmiwqab 进行中）：z1_morning_test.sh + flash_with_retry.sh + z1_capture_mainline.sh
- **配置修正进行中**：SND_SOC_MEDIATEK/MT6572/MT6323 → =y（内置），MTK_MT6572_CONNSYS =y；构建环境被并发 agent 竞争拖慢
