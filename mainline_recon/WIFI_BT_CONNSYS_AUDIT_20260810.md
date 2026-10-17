# WiFi/BT CONNSYS 排查结论（2026-08-10 凌晨）

> 来源：workflow `wf_690b5a5e`（12 agent audit + fix + crosscheck）
> 状态：离线审计完成，无代码改动（缺口均为驱动移植级，非小修复）

## 结论摘要

### 已就绪（平台层）
- MT6572 CONNSYS 平台配置全部编进 zImage：供电（VCN18/VCN28 regulator）、CONN 电源域（SCPSYS domain 3）、irq-sysirq、MT6323 MFD
- `drivers/soc/mediatek/mtk6572-connsys.c`（7333B）是**编译正确、fail-closed 的 platform 骨架**：可独立 `status="okay"` 启用做首个上板 gate——真实上 VCN18/28 + CONN genpd + 读 chip ID + 清 EMI（0xbe080000 no-map 内，无内存风险），失败路径有界不挂 boot
- dts `connsys_pwr@18070000` 节点 5 项（compatible/reg×2/power-domains/vcn18/vcn28）与 driver 完全匹配
- **VCN 供电物理链路可行（pwrap 写已放行）**，真卡点是 dts 未启用 + 前置只读快照 gate 未上板跑
- WiFi 固件链基本齐：`WIFI_RAM_CODE_SOC`（MTKW/platform=0x30 SOC 模式）+ `WMT_SOC.cfg` 在位，驱动期望名 `/lib/firmware/WIFI_RAM_CODE`

### 真缺口（驱动移植级）
1. **WLAN**：mainline 零支持，须移植 vendor 集成 CONNSYS WLAN 栈。隔离的 `drv_wlan/mt6620` 是外部 SDIO 变体**不对路**。
2. **BT 传输层**：mainline 完全缺失 BTIF/STP/WMT 驱动，无可直接复用件；须从 vendor 3.4 移植 WMT 核心 + STP + BTIF HAL 三层完整栈（量级 ≈ 已完成的 wlan 外部栈）。
3. **btmtkuart 不可用**：不支持 MT6620 且走 UART 不走 BTIF。唯一可复用：`btmtk.c` 固件下载协议助手。
4. **BT 固件缺失**：无对应 BT firmware 文件；WMT patch（CONN MCU 固件）无明确对应。
5. 固件加载名/路径/ABI 全未定。

### 对接点
现有 `mtk6572-connsys.c` 平台层之上挂 BTIF 设备。

### 前置 gate（未过，须上板）
MT6323 MFD-only 镜像 + VCN rail + layout2m 内存基线 → 已备候选镜像但未刷。

## 下一步建议
1. **首个 gate**：刷包含 `connsys_pwr` status="okay" 的镜像，验证 VCN 上电 + chip ID（0x6572）+ EMI 清零——这是 WiFi/BT 一切工作的前提，风险可控（fail-closed）。
2. **WLAN 移植**：评估移植 vendor CONNSYS WLAN 栈的工作量（独立立项）。
3. **BT 移植**：WMT/STP/BTIF 三层移植（更大工作量，后置）。

## 12-agent 深挖补充发现（2026-08-10 凌晨 workflow w7mwjgizd）

### 驱动 probe 流程（fail-closed，可安全独立启用）
`enable VCN18(1.8V)+VCN28(2.8V) → dev_pm_domain_attach(POWER_ON)+pm_runtime_get_sync 上 CONN genpd → ioremap 0x18070000 poll +0x8 chip ID(0x6572/0x6582, 100次×100us=20ms 有界) → ioremap 0xbe080000 memset_io 清 343KiB 共享 EMI → 常驻保持`。每步失败 dev_err_probe + 逐级清理，不挂 boot。

### 最可能失败点（首个 gate 的瓶颈）
**CONN TOPCKGEN 时钟门 bit26@0x10000084 未实现（驱动自身 TODO）**——CONN MCU 可能无时钟，chip ID poll 大概率超时。需先补（建议经 clk/regmap 而非裸写）。

### 关键警示
- **z1.dts write-deny 注释已过期**：`CONFIG_MTK_PMIC_WRAP_Z1_WRITE_DENY` 未设 → 一旦 enable，VCN18/28 **会真写 PMIC**。上板 gate 前先核 VCN 电压/功耗在允许范围。
- **EMI 基址 workaround**：dts 已从厂商 0x80080000 改到 0xbe080000（no-map 内）——厂商 FW 可能硬编码 0x80080000，CONN MCU 若跑飞此值为疑点。
- **dts 多余 supply**：vcn33_bt/vcn33_wifi 驱动不消费（vcn33 仅 RF 校准阶段需要）。

### BTIF/STP/WMT 缺口（mainline 全缺）
- 全树唯一 "BTIF" 命中是 8250 `mediatek,mtk-btif`（UART 风格，供外置 MT7622/7668 类）——**不可复用**为 CONNSYS BTIF 传输。
- BT 需自建三块：①WMT 核心（wmt_core/ctrl/func/ic_soc）②STP 帧（stp_core/stp_btif）③BTIF 底层（hal_btif+hal_btif_dma，基址 0x1100E000 FIFO+DMA+自有 IRQ）。量级 ≈ wlan 外部栈。
- 仅 `btmtk.c` 的 WMT 固件下载协议可复用；btmtkuart/btmtksdio/mt76 均不支持 MT6572。

### 内核配置已就绪（非缺口）
CONNSYS 平台层全=y 已编进 zImage：MTK_SCPSYS(+CONN 域) / MFD_MT6397 / REGULATOR_MT6323 / MTK_PMIC_WRAP / CONFIG_MTK_MT6572_CONNSYS。缺的是**驱动源码**（BTIF/WLAN AHB/WMT），非开关。

## CONNSYS 首个上板 gate 执行单（workflow wcqim362i，5 agent）

### 唯一必改
`mt6572-z1.dts:122` connsys_pwr `status="disabled"→"okay"`，仅重编 dtb。carveout（79-82）与 reg[1]=0xbe080000 不动。

### 通过判据
- ✅ gate PASS：dmesg `CONNSYS up: chip ID 0x6572`（或 0x6582）+ `CONNSYS shared EMI cleared` + mmc0 枚举/CMD13 无回归 + 无 panic
- ❌ 失败态可读：`CONNSYS chip ID poll timed out (last 0x0)` → 下一步补 TOPCKGEN bit26 门控（已修复在 `a4c17f314`）；`missing VCN18 supply` → MFD 前置没过

### ⚠️ 关键：现 mkbootimg 丢 ramdisk（实测 rd_size=0）！
不能用 `./build/mtkbootimg/mkbootimg --mtk 1` 打含 ramdisk 的镜像。用已验证 splice 脚本 `/tmp/splice_connsys.py`（以 cmd13 图做基底，仅替换 kernel 容器，字节一致）。产物 ≈4.87MB ≤6MB。

### 顺序建议（勿与 eMMC 合并）
1. 明天原样刷 final5，只验 eMMC CMD13 块读（单变量）
2. 另建同基镜像开 connsys_pwr，刷读 chip ID + 清 EMI
3. 过后再逐门：BTIF(IRQ122)→WLAN AHB(0x180f0000, IRQ123)→GDMA→固件 ABI

### 固件加载缺口（4 项）
1. **WIFI_RAM_CODE vs _SOC 不匹配**：drv_wlan 加载 `"WIFI_RAM_CODE"`（config.h:916），CONNSYS 需 `WIFI_RAM_CODE_SOC`。修法：拷 `WIFI_RAM_CODE_SOC` 为 `/lib/firmware/WIFI_RAM_CODE`，或改 config.h 重编 wlan.ko。
2. **ramdisk 无 /lib/firmware**：当前 initramfs 只有 bin/dev/etc/newroot/proc/sbin/sys/tmp/usr。
3. **CONN MCU patch**（mt6572_82_patch_e1_{0,1}_hdr.bin）：须 userspace wmt_loader 经 ioctl 下发，现缺加载体。
4. WMT_SOC.cfg 缺失非致命。

### 已实施修复
- `a4c17f314` CONN TOPCKGEN 时钟门（TOPCKGEN bit26 + SPM key 0x0b160001）
- `f48a3b8dc` EMI 日志打实际映射地址（0xbe080000 而非常量）

## 相关文件
- `drivers/soc/mediatek/mtk6572-connsys.c` — CONNSYS 平台骨架
- `arch/arm/boot/dts/mediatek/mt6572-z1.dts` connsys_pwr 节点（113-123 行，disabled）
- `out/firmware_backup/` — WIFI_RAM_CODE_SOC + WMT_SOC.cfg
