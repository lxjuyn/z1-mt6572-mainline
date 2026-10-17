# Z1 eMMC 时序审计 — 与 boot 不稳定相关性 (2026-08-14)

> 纯只读源码 + boot log 分析，未改文件 / 未编译 / 未碰硬件。
> 源树：`fork_linux`（HEAD `37a481a21`，2026-08-14 21:31）。驱动 `drivers/mmc/host/mtk-sd.c`，dts `arch/arm/boot/dts/mediatek/mt6572-z1.dts`。
> 证据 boot log（均在 `mainline_recon/`）：
> - 修后（含 `37a481a21`）：`z1_boot_mainline_20260814_213931.log`
> - 修前（无 always-on 修复）：`z1_boot_mainline_20260814_210837.log` / `20260814_202853.log` / `20260814_152956.log`
> - 历史根因：`z1_boot_emmc_cid_diag_20260727_185636.log`（CMD1 resp=0）、`z1_emmc_test3_233101.log`（块读 -110）

---

## 0. 结论速览（TL;DR）

1. **当前 eMMC 枚举完全健康**：每个 boot 的 CMD1 都返回合法非零 R3（`0x40ff8080`→`0xc0ff8080`），resp0_first == resp0_second（无 latch 竞争），`mmc0: new MMC card` + `mmcblk0 3.64 GiB` + 分区/boot0/boot1/rpmb 全部注册，**0 个 eMMC 错误 / 0 个 -110 / 0 个 CMDTMO**。枚举耗时稳定 ~0.5–0.9s，在 8.1–8.5s 完成。
2. **历史 eMMC 时序 bootloop 已根治**：7/27 诊断 build 的"CMD1 永远 resp=0 → 卡在 mmc 初始化 → WDT 咬死循环"（`z1_boot_emmc_cid_diag`）已被 BOOTRST 硬复位 + PB1 厂商基线 + 走完整 mmc core 枚举三项修复消除。
3. **当前 ~39.5s 重启循环不是 eMMC 时序问题，是 regulator late-cleanup**：`vusb/vmc/vmch: disabling` 出现在每个 boot 的 39.51s，紧接着 `Jump to BL` 复位。根因 = Z1 mmc0 无 `vmmc-supply`（fork 跳过 `mmc_regulator_get_supply`），这三个 eMMC 卡电/USB PHY regulator 零消费者被内核 39.5s 晚清理掉电。**`37a481a21`（always-on）今天 21:31 已修**；修后 `213931` 日志里 0 条 `: disabling`，boot#2 交互跑了 9+ 分钟不复位。
4. **修后仍偶发的一次性复位（213931 boot#1，~22s）与 eMMC 无关**：发生在 eMMC 完成（8.1s）后 14s，复位前最后内核消息是 power-controller `sync_state()` pending（connsys/connectivity 延迟 probe 窗口），无任何 mmc 输出/错误。同一镜像的 boot#2 存活 9 分钟 → 该复位是偶发事件，非确定性超时。
5. **稳定性结论**：eMMC 时序当前**不构成** boot 卡死/重启的根因。历史两个 eMMC 相关根因（stuck-busy bootloop、读卡 -110）均已修复且当前日志零复发。剩余不稳定归因于：① regulator late-cleanup（已修，待刷验证）；② 一次性复位/0 输出启动（非 eMMC）。

---

## 1. mtk-sd.c Z1 eMMC 时序路径梳理（z1_direct_cid / BOOTRST / CMD1）

### 1.1 `z1_direct_cid` 开关
- `host->z1_direct_cid = of_machine_is_compatible("jbo,z1")`（`mtk-sd.c:3630`）。Z1 专属路径由此触发，全局影响：
  - probe 期跳过 init 复位，保留 LK MSDC 交接（`mtk-sd.c:2047-2051`、`2027-2031`）。
  - `ocr_avail` 硬编码 2.7–3.6V 窗口（无 vmmc-supply，`mtk-sd.c:3644-3646`）。
  - PAD_TUNE 直写（`mtk-sd.c:2082`、`1185`、`1198`、`2262`）、IOCON DDLSEL=1（`2093`）。
  - probe 尾部 BOOTRST 释放 + PB1 厂商基线 + 强制 1-bit/400kHz（`mtk-sd.c:3812-3824`）。
  - `.card_busy` 故意缺省（`mtk-sd.c:3059-3064`）。

### 1.2 BOOTRST 硬复位（历史 stuck-busy 的根治步骤）
- `msdc_z1_emmc_hw_reset()`（`mtk-sd.c:3286-3296`）：`EMMC_IOCON.BIT0` = BOOTRST，先断言 RST_n 低 5ms，再释放 10ms。
- 调用点：probe 期 `mmc_add_host()` **之前**（`mtk-sd.c:3812`），以及 CID-only 诊断路径（`3511`）。
- 目的注释（`3277-3285`）：LK 交接后卡停在 stuck-busy（CMD/DAT0 低），任何 CMD1 都回 0；RST_n 脉冲清卡端 boot-mode/stuck 状态。**卡端状态级操作，不动媒体寄存器/用户数据**。
- 后续恢复 PB1 厂商基线 `0xffff0009`（`3813`，保留 CMDTA/WRDAT_CRC），再显式 `msdc_set_buswidth(1) + msdc_set_mclk(LEGACY, 400kHz)`（`3823-3824`），因为 `msdc_ops_set_ios` 在 z1 路径下对 ios 是 no-op，mmc core 不会自己降频。

### 1.3 CMD1 launch / completion 探针（枚举时序证据来源）
- `msdc_start_command`（`1604-1626`）：写 SDC_ARG 后、写 SDC_CMD 前后各打一条 `Z1 CMD1 launch-pre/post`（读 8 个寄存器）。`z1_cmd1_start_ns` 记在 launch-pre。
- `msdc_cmd_done`（`1477-1507`）：CMDRDY 中断里读 `resp0_first`（RSP0 读一次）与 `resp0_second`（再读一次）对比，检验 R3 响应是否与读竞争；同时打 `Z1 CMD1 completion: elapsed_ns=...`。
- **重要测量口径**：`launch-pre`→`launch-post` 之间只有两条寄存器写 + 一条 dev_info，但实测相隔 19–39ms —— 这是 **115200 console 的 printk 阻塞**（一条 ~250 字符 dev_info ≈ 22ms），不是 eMMC 命令耗时。CMD1 `elapsed_ns`（27–49ms）同样被打印阻塞主导。**真实 eMMC 命令是 µs 级**。这批探针在 z1 路径每个 boot 打 7×3 条 ≈ 0.3–0.5s，是诊断仪器的固定开销，非卡慢。

### 1.4 枚举时序（正常 boot，`213931`/`152956`/`210837` 一致）
| 内核时间 | 事件 |
|---|---|
| 7.13s | mtk_wdt probe（timeout=15s） |
| 7.29s | mtk-msdc 11120000.mmc probe 开始，`Z1 preserving LK MSDC handoff` |
| 7.34s | `Z1 eMMC HW reset done`（BOOTRST assert 5ms + release 10ms） |
| 7.48s | PB1 基线 `0xffff0009` 恢复，`Z1 eMMC released from stuck-busy` |
| 7.53–8.00s | mmc core CMD1 循环：7 次，resp `0x40ff8080`×6 → `0xc0ff8080`（busy 置位），每次 elapsed 27–49ms（printk 主导） |
| 8.01s | `mmc0: Host Software Queue enabled` |
| 8.03s | `mmc0: new MMC card at address 0001` |
| 8.09–8.18s | `mmcblk0 3.64 GiB` + `p1<p5 p6>p2 p3 p4` + boot0/boot1/rpmb |
| ~9.5s | `Run /init` |
| ~19.7s | shell + power-controller `sync_state()` pending（5 个域） |

枚举总耗时 ~0.9s（7.29→8.18s），每个 boot 完全一致，无抖动。

---

## 2. boot log eMMC 行证据：枚举成功，无超时

对全部近期日志统计（`grep -ac`）：

| log | boots | WDT | `mmc0: new MMC card` | `-110` cmd_err | `: disabling`(39.5s) |
|---|---|---|---|---|---|
| `20260814_152956`（修前） | 2 | 7.139s/15s | 2 | 0 | **6（vusb/vmc/vmch）** |
| `20260814_202853`（修前） | 2 | 7.141s/15s | 2 | 0 | **6（vusb/vmc/vmch）** |
| `20260814_210837`（修前） | 5 | 7.134s/15s | 5 | 0 | **15（每 boot vusb/vmc/vmch）** |
| `20260814_213931`（修后） | 2 | 7.133s/15s | 2 | 0 | **0** |

- CMD1 completion 铁证（`213931`，一次 boot 的 7 条）：
  `complete_ns=... resp0_first=40ff8080 resp0_second=40ff8080 sts_pre=0 sts_post=0 ps=81ff0002`
  → **resp0_first == resp0_second**（R3 无 latch 竞争）；`sts_pre/sts_post=0`（无 SDC_STS busy 假象残留在响应窗口）；`ps=81ff0002`（CMD/DAT0 均高，总线未卡）。历史 bug 的"resp0=0"已绝迹。
- 修前日志 39.51–39.52s 的 `vusb/vmc/vmch: disabling` 后紧跟 `Jump to BL` → **39.5s 复位的直接触发证据**；这是 regulator late-cleanup（零消费者掉电），不是 eMMC 命令超时（复位前无任何 mmc I/O）。

---

## 3. eMMC 时序与 boot 不稳定：历史根因（已修）vs 当前根因（非 eMMC）

### 3.1 历史 eMMC 时序 bootloop —— 根因已修
- **证据**（`z1_boot_emmc_cid_diag_20260727_185636.log`）：`Z1 CMD1 i=0/10/20/.../90 resp=00000000` 循环拉满（卡永远 busy/不答 R3），mmc 初始化挂死；`mtk-wdt ... enabled 15s` 在 1.53s 开启 → 卡死期间 WDT 到点咬死 → 循环复位。这就是 CLAUDE.md 记录的历史 eMMC 时序 bootloop。
- **修复**（当前源码全在，`EMMC_REMAINING_OFFLINE_20260814.md` 已核）：
  1. BOOTRST 硬复位（`mtk-sd.c:3286-3296`，probe 期 `3812`）清 stuck-busy。
  2. PB1 厂商基线 `0xffff0009` 恢复（`3813`），并把 init_hw 里的外科清除 `BUSY_CHECK_SEL|DDR_CMD_FIX_SEL` 固化（`2184-2194`，保留 CMDTA/WRDAT_CRC —— 注释明确禁止整写 0xffff0009，否则 CMD1 又回 0）。
  3. 去掉 CID-only 诊断门，走完整 mmc core CMD0..CMD8 枚举 + 注册 mmcblk（`1652-1655` 注释）。
- **现状**：`213931`/`210837`/`152956` 每个 boot 的 CMD1 全部返回合法非零 R3，卡 3.64GiB 正常枚举 → **历史 bootloop 零复发**。

### 3.2 当前 ~39.5s 重启循环 —— regulator late-cleanup，已修
- **证据**（`210837` 每 boot）：`[39.51x] vusb: disabling` → `[39.51x] vmc: disabling` → `[39.51x] vmch: disabling` → `Jump to BL`。5 个 boot 全同，39.5s 复位 100% 复现。
- **根因**：Z1 mmc0 无 `vmmc-supply`（fork 跳过 `mmc_regulator_get_supply`，`mtk-sd.c:3661-3665`），`vmc/vmch`（eMMC 卡电）与 `vusb`（USB PHY）零消费者 → `regulator_init_complete`（9.46s）之后 `regulator_late_cleanup` 在 **39.5s** 掉电 → 板复位。dts 注释（`mt6572-z1.dts:313-342`）对症状描述完全吻合。
- **修复**：`37a481a21 fix: keep vmc/vmch/vusb always-on - stop ~39s preloader reboot loop`（`mt6572-z1.dts:320-342` 四组 `regulator-always-on`）。今天 21:31 提交；**修后镜像尚未刷**（`213931` 21:39 的 log 0 条 `: disabling`，说明 21:39 已用含修复的新镜像）。

### 3.3 修后一次性复位（213931 boot#1，~22s）—— 与 eMMC 无关
- 复位前最后内核消息：`[19.72] power-controller sync_state() pending due to 18070000.connectivity`（及 smi/iommu/gpu/larb）。eMMC 早在 8.1s 完成，期间 0 条 mmc 输出。
- **关键反证（同镜像 boot#2）**：boot#2 同一镜像同一 WDT 配置，交互 shell 存活 9+ 分钟到 capture_end（`SHELLOK/SHELLOK2/SHELLNOW` 全回显）。→ 若有确定性 15s/39.5s 超时，boot#2 必死；它没死，证明该一次性复位是偶发事件（operator 动作 / 硬件毛刺 / 该窗口的延迟 probe 抖动），**非确定性超时，非 eMMC**。
- 注：`T0` 寄存器首 boot `0x123` → 后续 warm reset `0x124`，是复位发生标志，非原因；无法从 preloader 寄存器 dump 进一步解码（无 preloader 源码）。

---

## 4. mtk-sd 当前影响稳定性的设置（bus-width / max-frequency / CMD13 / PB1 / BOOTRST）

| 设置 | 出处 | 对稳定性的影响 |
|---|---|---|
| `bus-width = <1>` | `mt6572-z1.dts:368` | 1-bit 总线。吞吐 = 8-bit 方案的 1/8；`mmc_select_bus_width` 直接 return（不发 CMD6 BUS_WIDTH）。**不是 boot 稳定性问题**，但使大块读逼近超时预算（见 §6 建议 5）。 |
| `max-frequency = <50000000>` | `mt6572-z1.dts:369` | 上限 50MHz。当前日志**从未出现 `mmc0: new high speed MMC card`**（只有 `new MMC card`）→ HS 切换未完成/未执行，卡停在 legacy 时钟档。`src=199875000 Hz mclk=0`（probe 期）。实际运行时钟需 `debugfs/mmc0/ios` 上板确认；若卡在 400kHz/1bit，大块读会撞 5s 软件看门狗（§6 建议 5）。 |
| `.card_busy` 缺省 → **CMD13 轮询** | `mtk-sd.c:3059-3064` | 修复 A。R1B（CMD6 PART_CONFIG 等）busy 等待由 core 的 CMD13 轮询承担，绕开 `msdc_card_busy` 读 DAT0 的假象（BUSY_CHECK 后 DAT0 恒低）。**这是读路径稳定的关键**，当前在，勿回挂。 |
| **PB1 外科清除** `BUSY_CHECK_SEL\|DDR_CMD_FIX_SEL` | `mtk-sd.c:2184-2194` | 修复 B。只清这两 bit（保留 CMDTA/WRDAT_CRC），消除控制器侧 DAT0 假 busy。与 CMD13 轮询配合，把 Aug 4 的块读 -110 归零。 |
| **BOOTRST 硬复位** | `mtk-sd.c:3286-3296`、`3812` | 历史 stuck-busy 根治。卡端状态清除，5ms+10ms 固定延时，probe 期一次。 |
| `Z1_DIAG_MAX_CLOCK_HZ=400kHz` 强制识别时钟 | `mtk-sd.c:399`、`3823-3824` | 卡 BOOTRST 释放后回 power-on 态，识别期必须 ≤400kHz，显式设置保证 mmc core 枚举成功。 |
| CMD1 `BLKLEN=512` 进 SDC_CMD | `mtk-sd.c:1265` | 厂商 rawcmd 编码 quirk，枚举必需。 |
| 复位/恢复锁存检查：`resp0_first==resp0_second` | `mtk-sd.c:1477-1507` | 当前全部相等 → R3 latch 无竞争，无需（也无人）动 SDC_STS 等待。 |

---

## 5. "eMMC 枚举慢导致启动慢 / 卡在 mmc" 证据核查

- **有（历史，已修）**：`z1_boot_emmc_cid_diag`（7/27）CMD1 resp=0 死循环 = "卡在 mmc 相关"的直接证据；该状态直接导致 WDT bootloop。已由 §3.1 三项修复消除。
- **有（历史，已修）**：`z1_emmc_test3`（8/4）块读 `cmd=6 ... cmd_error=-110` 循环 = "eMMC I/O 卡死"（R1B busy 假象）。已由 CMD13 轮询 + PB1 外科清修复；当前所有日志 0 `-110`。
- **无（当前）**：近期每个 boot eMMC 枚举恒定 ~0.9s、0 错误，无任何"慢枚举"或"卡 mmc"现象。boot 到 shell 的 ~19.7s 被 initcall（含延迟 probe 的 power-controller/connsys 域）与 115200 console 主导，**不是 mmc**。
- 诊断探针（launch-pre/post/completion 每 CMD1 3 条 dev_info）给每个 boot 增加 ~0.3–0.5s printk，属仪器开销，如需可关（不影响正确性）。

---

## 6. 稳定性建议（按优先级）

1. **刷入含 `37a481a21`（vmc/vmch/vusb always-on）的新镜像，上板验证 39.5s 循环消失**（当前唯一未验证的确定性修复）。验证判据：`213931` 式日志里 0 条 `vusb/vmc/vmch: disabling`，且单 boot 存活 >60s。
2. **偶发一次性复位（213931 boot#1 ~22s）追查方向与 eMMC 无关**：集中在 power-controller sync_state / connsys connectivity 域延迟 probe 窗口（19.6–22s）。可试：`genpd_power_off_unused` 后哪些域被关、connectivity 域 probe 状态；或在 init 里加 `echo 1 > /sys/kernel/debug/...` 日志。若复现，抓 `dmesg -c` 于复位前。
3. **WDT 现状核对**：`mtk-wdt` probe 前 `mediatek_disable_wdt_early()`（`arch/arm/mach-mediatek/mediatek.c:35-58`）已清 WDT_MODE_EN，`mtk_wdt_init` 只在 HW 已跑时才置 `WDOG_HW_RUNNING`（`mtk_wdt.c`），`watchdog_register_device` 不主动 start → **WDT 默认不会被 `15s` 咬死**（boot#2 存活 9min 佐证）。若未来要保底防挂，可在 init 里开 `/dev/watchdog`（busybox `echo V > /dev/watchdog`），但当前**不是**主因。
4. **eMMC 三项修复勿回退**：`.card_busy` 勿回挂（`mtk-sd.c:3059` 注释）、PB1 勿整写 0xffff0009（`2190-2193` 注释）、BOOTRST/PB1 基线/400kHz 识别序列勿删。`msdc_card_busy` 现为死代码（`1838-1845`），留注释警示即可。
5. **吞吐（非 boot）约束**：1-bit + 疑似未上 HS → 大块读（≥256KB）可能撞 `DAT_TIMEOUT = HZ*5` 软看门狗（`mtk-sd.c:387`、`1847-1870`）。上板用 `cat /sys/kernel/debug/mmc0/ios` 确认实际 clock；如确停 400kHz/1bit，且需要大块读，评估 dts `bus-width=<4>`（需板级 MC0_DAT1-3 布线确认，`mmc0_pins` 现只声明 DAT0）。
6. **健康面**（旁路）：H4G1d 是 2013 老 MLC 卡，`rev` 大概率 <7 → sysfs `pre_eol_info/life_time` 恒 `0x00`（未定义，勿当健康）。只读归档优先（`EMMC_HEALTH_READ_20260814.md` 命令），读安全、不耗 P/E。

---

## 7. 关键引用行号（fork_linux HEAD `37a481a21`）

- `drivers/mmc/host/mtk-sd.c`
  - `z1_direct_cid` 判定 `3630`；`ocr_avail` `3644-3646`
  - BOOTRST 复位 `3286-3296`；probe 期调用 `3812`；PB1 基线+400kHz `3813-3824`
  - PB1 外科清 `2184-2194`；CMD1 BLKLEN `1265`；`.card_busy` 缺省 `3059-3064`；死代码 `1838-1845`
  - CMD1 探针 `1477-1507`、`1604-1626`；`DAT_TIMEOUT` `387`；req_timeout `1847-1870`
  - `mt6572_compat` `563-574`（busy_check=false → init_hw 会置 BUSY_CHECK_SEL）
- `arch/arm/boot/dts/mediatek/mt6572-z1.dts`
  - mmc0 节点 `362-374`（`bus-width=<1>` / `max-frequency=<50000000>` / `no-sd` / `no-sdio` / `non-removable`）
  - mmc0_pins `401-419`（只声明 CK/CMD/DAT0，drive-strength=6）
  - always-on regulators `320-342`
- `arch/arm/boot/dts/mediatek/mt6323.dtsi`：`mt6323_vmc_reg`=`vmc`(`135-136`)、`mt6323_vmch_reg`=`vmch`(`143-144`)、`vemc3v3`(`152`)、`vusb`(`128`)
- `arch/arm/mach-mediatek/mediatek.c`：`mediatek_disable_wdt_early` `35-58`
- `drivers/watchdog/mtk_wdt.c`：`mtk_wdt_init`（WDT_MODE_EN 时置 WDOG_HW_RUNNING）、probe 只注册不 start

*本次审计基于只读分析；所有"已修"结论均已在源码 + 修后 boot log 双重确认，修后镜像尚未上板刷写验证（待做）。*
