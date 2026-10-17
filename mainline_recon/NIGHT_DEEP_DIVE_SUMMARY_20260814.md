# 夜间深挖总结 + 下次上板计划 (2026-08-14)

> 汇总 4 份夜间报告（全部存在，无缺产）：
> 1. `BTIF_EMI_FIX_REVIEW_20260814.md` — 只读复核 btif ranges + EMI base 两修复
> 2. `DRIVER_REMAINING_AUDIT_20260814.md` — 剩余外围驱动 8 项只读审计（基于 boot log `z1_boot_mainline_20260814_021713.log`，git HEAD `e3198bbc3`）
> 3. `WIFI_NVRAM_EXTRACT_PLAN_20260814.md` — 本地 NVRAM 分区镜像解析出 WIFI 512B blob
> 4. `CONNSYS_MAINLINE_REFERENCE_20260814.md` — 联网调研 CONNSYS func-on 时序/固件/NVRAM/ST7785M 参考
>
> 板子关机。本文件为只读汇总，不含任何新硬件/编译动作。关键事实已实测复核（NVRAM blob MAC/md5）。

---

## 1. BTIF/EMI 修复结论

**两个修复在源码层均成立、互为一致，dtb 合法。真正待上板回答的只剩一个：CONN 固件是否看得到迁走的共享 EMI。**

| 检查项 | 结论 |
|---|---|
| 修复① btif 节点加空 `ranges;` | ✅ 成立。空 ranges = 1:1 翻译；`of_translate_one` 中 `ranges` 非 NULL 不 return 1，`rlen==0` 走 1:1 分支，btif_tx/rx reg 正确翻译成 0x11000380/0x11000400。**反证**：缺 ranges → of_iomap NULL → `hal_btif_dma_hw_init` NULL 解引用 Oops（正是 log 里的 crash）。空 `ranges;` 是本修复正确且必要机制，不能依赖 powerpc 专属的 `of_empty_ranges_quirk` |
| 修复② CONSYS_EMI_AP_PHY_BASE 0x80080000→0xbe080000 | ✅ 成立。gConEmiPhyBase=0xbe000000（carveout 底），ioremap(0xbe080000, 343K)；EMI 区 [0xbe080000, 0xbe0d5c00) 完全落在 carveout [0xbe000000, 0xbe100000)（1MB no-map）内，尾部余 169KB 不越界；dts reg[1]=<0xbe080000 0x55c00> / carveout / 头文件宏三处数值闭环；全树无残留 0x80080000 |
| dtc 编译 | ✅ exit 0，仅 2 条**既有无关** warning（simple_bus_reg pinctrl 格式；/connsys@18070000 与 /connectivity@18070000 重复 unit-address） |

**隐患（源码层无法排除，需上板）：**
1. **【最大隐患】`CONSYS_EMI_FW_PHY_BASE=0xf0080000` 未随迁**（wmt_plat_alps.c:81 写入 CONN MCU 的共享 EMI 视图地址）。0xf0000000 别名窗口若固定落在旧 0x80080000，固件读写共享 EMI 仍打旧位置——现在那里是普通 RAM，轻则握手失败重则静默踩内存；若别名窗口按当前 EMI 映射落 0xbe080000 则一致。**源码无法证明哪边成立**。dts 注释建议：若 CONN MCU 行为异常，carveout + connsys_pwr reg[1] 一起回退。
2. 既有 DT 卫生问题：`/connsys@18070000`（in-tree connsys_pwr, disabled）与 `/connectivity@18070000`（out-of-tree connectivity）重复 unit-address。当前 compatible 不同无冲突；将来若 enable connsys_pwr 会双映射同 regulator，需留意不双上电。
3. btif 驱动既有怪癖：`hal_dma_set_default_setting` 读子节点 reg 第 2 个 cell（=size 0x80）当 phy base，目前只进打印不影响流程，标注即可。

**上板判定标准**：log 中 `hal_btif_dma_hw_init` 不再 NULL Oops（修复①生效）；CONN MCU chip ID + EMI 握手正常；若 EMI 区 0xbe080000 无固件数据流 → 需处理 CONSYS_EMI_FW_PHY_BASE 迁移问题。

---

## 2. 下次上板一次性验证清单（按优先级）

> 刷机命令参照 CLAUDE.md：`tools/flash_with_retry.sh bootimg <img> /home/<user>/claude/6572/1/boot.img 8`（刷写才插 download 线，正常开机拔 USB；C3 桥 UART 线不动）。抓 log：`./z1_capture_mainline.sh 600`。
> 板子起来后以下全部可一次性跑。**带 ✔ 的纯只读无硬件动作，优先做。**

### P0 — func-on 复测（当前卡点，BTIF/EMI 修复后首次验证）

```sh
# 1) 确认 5 模块 + 节点（期望无 Oops）
lsmod | grep -E "mtk_stp|mtk_wmt|wlan"
ls -l /dev/stpwmt /dev/wmtWifi            # c 190 0 / c 153 0
ls -d /proc/driver/wmt_dbg /proc/wmt      # WMT 调试节点存在

# 2) 单独验证 BTIF（不触发 wifi，期望不再 Oops）
cat /sys/kernel/debug/btif/* 2>/dev/null; echo "btif read done"
# dmesg 无 hal_btif_dma_hw_init NULL 崩溃 / no Oops = 修复①生效

# 3) 固件文件头校验（期望 MTKE / ALPS）
xxd /etc/firmware/WIFI_RAM_CODE_MT6582 | head -1      # MTKE magic
xxd /etc/firmware/*_patch*_hdr.bin | sed -n '1p;13p'  # +0x0C 处 ALPS

# 4) func-on（期望 dmesg 出现 chip id / RAM code 加载，wlan0 出现）
echo 1 > /dev/wmtWifi; sleep 5
dmesg | tail -80    # 看 [WMT] patch 下载 / chip id / BTIF DMA / EMI 握手日志
ip link             # 期望 wlan0

# 5) WMT 详细日志（若 func-on 静默，开调试重试）
echo 0xDB9DB9 > /proc/driver/wmt_dbg
echo "7 9 1" > /proc/driver/wmt_dbg    # 开 LPBK 会阻塞~1.3s；调试完 echo "7 9 0" 关
```

判定：**BTIF 修不好则 WiFi/BT 都不可能好**（BTIF 同时承载两路）；`/dev/wmtWifi` 没出现 = wmt_wifi 没 probe（查 mknod 主次号 c 153 0）；func-on 无日志 = 固件路径错而非 BTIF 问题。

### P1 — NVRAM 部署 + MAC 验证（P0 之前或并行都行，可现装不重刷）

```sh
# 已切出（见 §4 NVRAM 方案），上板后拷入（等价 ramdisk 内置）：
mkdir -p /etc/firmware/nvram
cp /root/wifi-nvram-WIFI.bin /etc/firmware/nvram/WIFI   # 512B，路径=驱动 WIFI_NVRAM_FILE_NAME
# func-on 成功后（或上电即验，需 wlan0 已 up）：
ip link set wlan0 up
cat /sys/class/net/wlan0/address    # 期望固定 <MAC>（不再随机 = glLoadNvram 读到）
iw dev wlan0 scan | grep -E 'BSS|SSID'    # scan 正常、单播帧不丢
# 若 MAC 随机 / NVRAM WARNING ERR 0x10 = NVRAM 文件缺失或路径不对
```

### P2 — 剩余驱动（✔ = 纯只读，零硬件动作）

```sh
# ✔ eMMC 健康（旧卡 H4G1d，读法现成，最优先做，0 风险）
cat /sys/block/mmcblk0/device/pre_eol_info   # 0x01=Normal 0x02=WornOut80% 0x03=Critical 0x04=EWR Over
cat /sys/block/mmcblk0/device/life_time      # 两字节 "typA typB"（0x01=0-10% ... 0x0a=90-100%）
dd if=/dev/mmcblk0boot0 of=/tmp/boot0.img bs=512 count=8192   # 只读安全（force_ro）

# ✔ WDT 状态（已 15s 内核喂狗，只读确认）
ls -l /dev/watchdog*; cat /sys/class/watchdog/watchdog0/timeout   # 15
cat /sys/class/watchdog/watchdog0/identity

# ✔ UART 现状（ttyS0=console；mainline 无 ttyMT* 名）
cat /proc/tty/driver/serial; ls -l /dev/ttyS*

# ✔ USB device 模式（g_serial 已就绪；Z1 无 OTG/主机口）
ls -l /dev/ttyGS0; cat /dev/ttyGS0 &

# 显示 DRM（需重刷后才可测，见 §3 修复方向；当前 simplefb 240x320x16 在跑）
ls /dev/dri/ 2>/dev/null; ls /sys/class/drm/ 2>/dev/null

# 音频 / RTC（需重刷后才可测，见 §3；当前 /proc/asound 无、/dev/rtc* 无）
cat /proc/asound/cards 2>/dev/null; ls -l /dev/rtc* 2>/dev/null
```

### P3 — 刷机前确认（改源/打包后必做）

```sh
# dtb 合法性 + 两个修复进产物的自检（复刻 BTIF_EMI_FIX_REVIEW 的核对）：
dtc -I dts -O dtb -o /tmp/mt6572-z1_check.dtb fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts && echo "dtc OK"
dtc -I dtb -O dts /tmp/mt6572-z1_check.dtb | grep -A3 "btif@1100e000"   # 期望含 ranges; #address-cells=<0x01>
# 产物三处一致：头文件 CONSYS_EMI_AP_PHY_BASE=0xbe080000 / dts connsys_pwr reg[1]=<0xbe080000 0x55c00> / carveout connsys@be000000
```

---

## 3. 剩余驱动状态表

| 设备 | 状态 | 缺口 | 修复方向 |
|---|---|---|---|
| **CONNSYS WiFi/BT (BTIF)** | **待复测**（源码已修：btif ranges + EMI base 0xbe080000） | 修复②的最大隐患 CONSYS_EMI_FW_PHY_BASE=0xf0080000 未随迁 | 上板 func-on；若 EMI 0xbe080000 无固件数据流 → 评估连 FW_PHY_BASE 一起迁（32 位表达不了，若别名窗口固定则需确认硬件行为） |
| **ST7785M 显示 (DRM)** | 缺失（panel 驱动在树内，真实 init 表已从 LK 提取；DRM 整体未使能） | CONFIG_DRM/DRM_MEDIATEK/DRM_PANEL_SITRONIX_ST7785M/PHY_MTK_MIPI_DSI/MTK_IOMMU_V1 全未开；dts &dsi + panel@0 disabled；simplefb 与 DRM 会抢 fb0 | config 开 5 项 + dts 去 dsi/panel disabled + 删 simplefb framebuffer0；DSI 引脚 GPIO79-84 已连，reset/AVDD 无板级证据可先不加（probe 仍过）；H-timing 仍是 ST7789V 族默认标 TODO |
| **音频 (MT6323+MT6572 AFE)** | 缺失（骨架在树，整树 CONFIG_SOUND 关） | dts audsys 塞 `GIC_SPI 0` 占位 IRQ（非法，非负 → request_irq → 挂死 kernel_init，实测 155s backtrace）；MFD diag 模式不建 codec cell；MT6323 寄存器 bit 定义缺失（无 datasheet 硬 blocker） | dts 删 audsys `interrupts` 属性（platform_get_irq 返回负 → warn+continue 不挂）；开 CONFIG_SOUND + SND_SOC_MT6572 + SND_SOC_MT6323；mt6397-core diag devs 补 codec cell；真实寄存器需逆向 vendor 源 |
| **RTC (mt6323-rtc)** | 缺失（驱动已编 RTC_DRV_MT6397，无设备无 rtc0） | 双根因：MFD diag 不建 rtc cell + rtc-mt6397 probe 强制 platform_get_irq 负值直接 return | 方案A（快）：diag devs 补 rtc cell + probe 把 IRQ 改 optional（无 IRQ 跳 request_irq 禁 alarm，只读/写时间）；方案B（正路）：解 diag 做通 pwrap IRQ controller |
| **WATCHDOG (mtk-wdt)** | ✅ 可用（15s 内核喂狗，无复位） | 无（nowayout=0，未来 userspace 接管需 watchdog daemon） | 无需改 |
| **GPIO/LED** | 缺失（LEDS_GPIO 已编，dts 无 gpio-leds 节点） | LED 具体 GPIO 未知（需万用表/原理图/原厂 blib 证据） | 拿到 GPIO 号后 dts 加 gpio-leds（active-low 则 `<&pio N 1>`）；若灯在 MT6323 走 mt6323_leds（需补 diag cell + CONFIG_LEDS_MT6323） |
| **USB host** | 不适用（device 模式工作，g_serial ready） | Z1 无 OTG/主机口（听力机 micro-USB 充电/数据），dr_mode=peripheral 正确 | 无需改 |
| **UART 其它口** | 部分可用（ttyS0=console） | uart1 dtsi disabled 无 override；无 uart2/3 节点；mainline 无 ttyMT* 名 | 若 Z1 有第二路物理 UART（板证）则 z1.dts `&uart1 { status="okay" }` |
| **eMMC 剩余** | ✅ 可用（boot0/1/rpmb 注册；健康数据 boot 期解码+sysfs 导出） | 无（读法现成） | 上板读 pre_eol_info/life_time，按值判断旧卡 H4G1d 是否需降级只读归档 |

**另记**：boot log（021713）末尾 mtk_wmtd 曾在 `hal_btif_dma_hw_init [mtk_btif_drv]` NULL 崩——该 log 早于 git HEAD `e3198bbc3`（btif ranges 修复），源码已修未重刷，P0 第 2 步专测此项。

---

## 4. NVRAM 方案（提取路径，已实测）

**本地就有 Z1 权威 Wi-Fi NVRAM，无需上板提取**（纠正 `CONNSYS_FW_READINESS_20260814.md` §5.4 用文件名搜索误判"本地无 NVRAM"的结论）。

**提取路径**（`/home/<user>/claude/6572/1/NVRAM` = stock 5MB NVRAM 分区镜像，零修改原版）：

```bash
dd if=/home/<user>/claude/6572/1/NVRAM bs=1 skip=$((0x20000)) count=512 \
   of=/home/<user>/claude/6572/mainline_recon/wifi-nvram-WIFI.bin
md5sum  wifi-nvram-WIFI.bin    # 已实测 = a6182f634a2554bc881e87fdf111ecd1
xxd -l 16 wifi-nvram-WIFI.bin  # 已实测 = 04 01 00 00 20 72 0d 39 0d 1f 00 00 26 26 00 00
```

- blob = 512B `WIFI_CFG_PARAM_STRUCT`（MT6628 版本 0x0104，**真实 MAC <MAC>**，其余 TxPwr/efuse 全默认）。分区索引记录尾字段 0x202=514 是 daemon 校验/保留 2B 尾缀，**驱动只读 0~511**，部署 512B 即可；若严格要 514 末尾补 2 个 0。
- 驱动读取路径：`/tmp/connsys_build/wlan/os/linux/platform.c:124` `#define WIFI_NVRAM_FILE_NAME "/etc/firmware/nvram/WIFI"`；读取实现 `gl_init.c glLoadNvram()`（offset 510 校验通过 → fgNvramAvailable=TRUE → MAC 固定）。
- **部署**：打包期在 `fork_linux/scripts/repack_ramdisk_connsys.sh` 固件段后加 `mkdir -p "$TMP/rd/etc/firmware/nvram"` + `cp wifi-nvram-WIFI.bin "$TMP/rd/etc/firmware/nvram/WIFI"`（当前 `out/ramdisk_mainline_connsys.gz` 无 `/etc/`）；或上板现装 `/etc/firmware/nvram/WIFI`；SD 分期口径放 `/tmp/connsys_build/tools/wifi-fw/nvram-WIFI`（deploy-modules-sd.sh 期望路径，该 staging 目录当前不存在）。
- 兜底（本地源丢失时）：上板 mount 各分区找 `nvram/APCFG/APRDEB/WIFI`（512 或 514B）拷入，见 WIFI_NVRAM_EXTRACT_PLAN §6。
- **验证判定**：`/sys/class/net/wlan0/address` = `<MAC>` 且 `ip addr` 一致 = NVRAM 生效；MAC 随机 / `NVRAM WARNING ERR 0x10` = 文件缺失或路径错。
- 文件头对照：MAC @ +4 起 6 字节、文件尾 2B checksum 与 mtkclient `mtk_crypto.py` 写回逻辑一致（wifi_offset = toc_size(0x20000) + TOC 记录的偏移 —— 与实测 offset 0x20000 吻合）。

---

## 5. 联网参考（关键 URL + 建议）

**结论性事实**：没有现成 "MT6572 CONNSYS mainline 跑通" 先例——mt6572-mainline 上游 README 明写 "Connectivity: not existent in the upstream"，CONNSYS 移植是 Z1 自己的活。最接近实战参考是同栈不同 SoC 的 BPI-R2 (MT7623) + postmarketOS Wiki。

| 用途 | URL |
|---|---|
| MT6572 主线 fork（无 CONNSYS，平台状态表） | https://github.com/mt6572-mainline/linux |
| postmarketOS WiFi 页（Mediatek 节，**权威 func-on 时序**） | https://wiki.postmarketos.org/wiki/WiFi |
| **BPI-R2 MT7623 CONNSYS 实测 log**（launcher 输出、patch 拼名、BTIF 必须通） | https://forum.banana-pi.org/t/bpi-r2-internal-wifi-bt-mt6625l-kernel/4307 |
| openwrt wmt.init（完整 func-on 脚本） | https://github.com/Ysurac/openmptcprouter/blob/develop/common/package/utils/wmt/files/wmt.init |
| launcher 源码 + 默认宏（CUST_COMBO_*） | https://github.com/MediaDeb/android_hardware_mediatek/blob/master/combo_tool/src/stp_uart_launcher.c ；https://github.com/Envcc/MediaTek-MTK-UART-enable |
| CONNSYS 固件格式语义（ALPS@+0x0C / MTKE@+0x00 / WMT_SOC.cfg 纯文本） | https://github.com/bsg100/gemini-linux/blob/main/blockers.md |
| MT6572/82 CONNSYS 固件文件名清单（Wiko） | https://github.com/thiz11/mediatek/blob/master/config/wiko/proprietary-files.txt |
| NVRAM WIFI 解析/写回逻辑（MAC@+4，2B checksum） | https://github.com/bkerler/mtkclient/blob/main/mtkclient/Library/mtk_crypto.py |
| wmt_dbg 调试命令（koreader 实测） | https://github.com/koreader/koreader/blob/master/platform/kobo/enable-wifi.sh |
| BTIF base 参考值（MT6735/6797 = 0x1100C000） | https://github.com/littlekernel/lk/blob/master/platform/mediatek/mt6735/include/platform/mt_reg_base.h |
| MT65xx 主线笔记（寄存器定位/IO_VIRT_TO_PHYS） | https://github.com/arzam16/empty-sixty-five |

**建议**（按优先级）：
1. **func-on 时序以 postmarketOS Wiki + BPI-R2 为准**：insmod 顺序 mtk_stp_wmt_soc → mtk_stp_bt_soc → mtk_wmt_wifi_soc → wlan_gen2；mknod `/dev/stpwmt c 190 0` + `/dev/wmtWifi c 153 0`；固件加载顺序 **先 ROM patch（ALPS magic）后 WiFi RAM code（MTKE magic）**；最后 `echo 1 > /dev/wmtWifi`。CONNSYS/SOC 时代无 `-d /dev/ttyMT2`（那是 UART combo 才需要），func-on 由内核侧 WMT 完成固件下载 → BTIF DMA 是关键路径。
2. **WMT_SOC.cfg 不是卡点**：实测全文仅 4 行 key=value（`coex_wmt_ant_mode=1` 等），只要内容合法即可；它不决定固件路径。
3. **patch 命名不必纠结**：BPI-R2 显示 launcher 按 `前缀+版本+_hdr.bin` 拼名；Z1 的 `mt6572_82_patch_e1_*` 或 `ROMv1_patch_MT6582_*` 都属 CONNSYS 一代，按 ramdisk 实际文件核对即可。固件同硅片跨 3.4/7.0 内核通用。
4. **ST7785M 走 DSI 路线**：mainline 无 ST7785M 驱动，`sitronix-st7789v.c` 不覆盖（不同控制器）；MT6572 主线 DRM DBI/DPI 是 DEAD，只有 DSI 可走（mt2701 兼容数据）。Z1 屏若实际是 ST7789 兼容需读屏 ID/对照原厂 init 序列确认——但项目已有自写 panel 驱动+真实 LK init 表（DRIVER_REMAINING_AUDIT §1），走 DSI 使能路径即可。
5. **BTIF base 对照**：MT6735/6797 = 0x1100C000（infrasys 0x11000000 域）；Z1 BTIF base 0x1100e000 同域合理；BTIF DMA Oops 典型根因就是 DMA 缓冲映射到错误 EMI 地址——修复②正对此。

---

## 6. 下次上板行动摘要（一句话）

刷入含 btif ranges + EMI 0xbe080000 修复的 boot.img（可含 NVRAM 内置的 ramdisk）→ 抓 log 第一看 `hal_btif_dma_hw_init` 不再崩 → `echo 1 > /dev/wmtWifi` 看 wlan0 + chip id → 验 wlan0 MAC=<MAC> → 顺带跑 P2 只读项（eMMC 健康/WDT/UART/ttyGS0）→ 若 EMI 区无固件数据流再处理 CONSYS_EMI_FW_PHY_BASE 迁移问题。
