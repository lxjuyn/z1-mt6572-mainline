# Z1 (MT6572) Boot 稳定性方案 — 汇总 (2026-08-14)

> 两份只读审计的汇总 + 行动清单:
> 1. `mainline_recon/EMMC_TIMING_AUDIT_20260814.md`
> 2. `mainline_recon/CMDLINE_PANIC_AUDIT_20260814.md`
> 两份报告均存在、内容完整, 无缺失项。本文件为只读分析 + 打包建议, 未改源码 / 未编译 / 未碰硬件。

---

## 0. 一句话结论

**当前 boot 不稳定 (~39.5s 重启循环) 不是 eMMC 时序问题, 也不是 panic 被 panic=0 隐藏; 真正根因是 regulator 被"无用资源回收"在 39.5s 关掉引发的硬件复位。该根因已有 DTS 修复 (always-on, commit `37a481a21`), 待上板刷写验证; 修后仍残留一个间歇性 ~19.7s 的 power-controller `sync_state()` 复位, 需另查。**

---

## 1. eMMC 时序与 boot 不稳定相关性

### 结论: 当前 eMMC 枚举完全健康, 不构成 boot 卡死/重启根因

**铁证 (修后 `213931` + 修前多份日志一致):**
- 每个 boot CMD1 都返回合法非零 R3 (`0x40ff8080`→`0xc0ff8080`), `resp0_first == resp0_second` (无 latch 竞争), `sts_pre/sts_post=0`, `ps=81ff0002` (总线未卡)。
- `mmc0: new MMC card` + `mmcblk0 3.64 GiB` + p1–p6/boot0/boot1/rpmb 全部注册。
- **0 个 eMMC 错误 / 0 个 -110 / 0 个 CMDTMO**。
- 枚举耗时稳定 ~0.9s (7.29→8.18s), 每个 boot 完全一致, 无抖动。boot 到 shell 的 ~19.7s 被 initcall (power-controller/connsys 延迟 probe) 与 115200 console 主导, **不是 mmc**。

**历史两个 eMMC 根因均已修、零复发:**
| 历史问题 | 证据 | 修复 |
|---|---|---|
| 卡在 stuck-busy → CMD1 resp=0 死循环 → WDT 咬死 bootloop (7/27) | `z1_boot_emmc_cid_diag_20260727_185636.log` | BOOTRST 硬复位 (`mtk-sd.c:3286-3296` + probe 期 `3812`) + PB1 厂商基线 `0xffff0009` 外科清除 (`2184-2194`) + 走完整 mmc core 枚举 (`1652-1655`) |
| 块读 cmd_error=-110 (R1B busy 假象, 8/4) | `z1_emmc_test3_233101.log` | `.card_busy` 缺省走 CMD13 轮询 (`mtk-sd.c:3059-3064`) + PB1 清 `BUSY_CHECK_SEL|DDR_CMD_FIX_SEL` |

**~39.5s 重启的真相 (非 eMMC):** `vusb/vmc/vmch: disabling` 出现在每个 boot 39.51s, 紧跟 `Jump to BL` 复位。根因 = Z1 mmc0 无 `vmmc-supply` (fork 跳过 `mmc_regulator_get_supply`, `mtk-sd.c:3661-3665`), 这三个 eMMC 卡电/USB PHY regulator 零消费者, 被 `regulator_late_cleanup` 在 39.5s 掉电 → 板复位。

**eMMC 侧建议 (按优先级):**
1. eMMC 三项修复**勿回退**: `.card_busy` 勿回挂 (`mtk-sd.c:3059`)、PB1 勿整写 `0xffff0009` (`2190-2193`)、BOOTRST/PB1/400kHz 识别序列勿删。`msdc_card_busy` 已是死代码 (`1838-1845`), 留注释警示即可。
2. **吞吐 (非 boot) 约束**: `bus-width=<1>` + 疑似未上 HS (日志从未出现 `new high speed MMC card`), 大块读 (≥256KB) 可能撞 `DAT_TIMEOUT=HZ*5` 软看门狗 (`mtk-sd.c:387`)。上板用 `cat /sys/kernel/debug/mmc0/ios` 确认实际 clock; 若确停 400kHz/1bit 且要大块读, 评估 dts `bus-width=<4>` (需确认 MC0_DAT1-3 板级布线, `mmc0_pins` 现只声明 DAT0)。
3. 诊断探针 (每 CMD1 3 条 dev_info) 每 boot 增加 ~0.3–0.5s printk, 属仪器开销, 如需可关 (不影响正确性)。
4. 健康面 (旁路): H4G1d 老 MLC 卡 `rev` 大概率 <7, sysfs `pre_eol_info/life_time` 恒 `0x00` 勿当健康。只读归档优先, 读安全不耗 P/E。

---

## 2. panic=0 是否隐藏重启 + cmdline 建议

### 结论: panic=0 不是隐藏重启, 反而会让真 panic 变成"挂死"

**mainline 7.0-rc7 `kernel/panic.c` 语义 (与 Android 3.4 时代不同):**
- `panic_timeout` 默认 = `CONFIG_PANIC_TIMEOUT` = 0 (`.config:6154`)。
- `panic=0` → `panic_timeout=0` → `vpanic()` 走**无限循环永久挂死**, **不重启** (`panic.c:770-777`)。
- `panic=10` → 打印 panic 信息 + `Rebooting in 10 seconds..` + 等 10s 再 `emergency_restart()`。
- `panic=-1` → **立即重启** (跳过延时块), 不适合调试。
- 真 panic 字串在任何情况都先 `pr_emerg` 打印到 ttyS0 (log 有 `console [ttyS0] enabled`)。

**推论:** 若真有 panic, UART 必留 `Kernel panic` 字样。全量 grep 100+ 份 `z1_boot_mainline_2026*.log`: **`Kernel panic`/`Oops`/`BUG:`/`not syncing` 全部 0 命中** → 板子重启**不是 panic 路径产生**。panic=0 挂死 + `CONFIG_MEDIATEK_WATCHDOG=y` (挂死期间没人喂 → WDT 复位) 会表现为"卡死若干秒后重启", 但即便如此 panic 字串也早已打印。

**非致命但值得记录的 WARNING:**
1. **`unbalanced disables for vcn28`** (`regulator/core.c:3244`, 模块栈 `wlan_gen2/mtk_wmt_wifi_soc/mtk_stp_bt_soc/mtk_stp_wmt_soc/mtk_btif_drv/cfg80211`): 一次 capture 63 次, 从 [37.5s] 起约每 1.5s 一次。out-of-tree WMT 驱动对 vcn28 反复不配对 disable, 可能把 WiFi/BT 子系统带崩 (见 §3 优先项 4)。
2. `dev_addr_check` (ifconfig 触发, 用户态, 与 boot 无关)。

**现有 cmdline 分析:**
- 实测 (boot header offset 64): `console=tty0 console=ttyS0,115200n8 root=/dev/ram panic=0 ignore_loglevel clk_ignore_unused rdinit=/init`
- `clk_ignore_unused` 是缓解项 (跳过 `clk_disable_unused`, 减少"时钟被关但驱动仍要"死机), 不是掩盖项, 保留但耗电。
- **关键缺口**: 有 `clk_ignore_unused` 却没给 regulator 上同样的保险 — 内核有 `regulator_ignore_unused` (`regulator/core.c:6841-6847`) 未被使用, 而 Z1 重启正是 regulator 被"无用回收"引起。
- `console=tty0` 无害但无用 (无有效显示设备, fbcon 绑 dummy console), 可删。

### 推荐调试 cmdline

```
console=ttyS0,115200n8 root=/dev/ram panic=10 ignore_loglevel clk_ignore_unused \
regulator_ignore_unused earlycon=mtk8250,0x11005000 oops=panic sysrq_always_enabled \
panic_print=0x1f rdinit=/init
```

| 参数 | 改法 | 理由 |
|---|---|---|
| `panic=10` | **0→10** | 真 panic 时先打印信息再延时重启, 10s 足够 UART 读信息; 避免 panic=0 挂死后被 WDT 无标识复位。**勿用 panic=-1** (立即重启丢证据)。 |
| `oops=panic` | **加** | CONFIG_PANIC_ON_OOPS 未开 (oops 默认继续跑)。把 oops 转 panic, 防"带伤运行→无痕复位", 尤其针对 vcn28 反复误 disable。 |
| `regulator_ignore_unused` | **加** | 内核自带, 直接停掉 39.5s regulator 晚清理关机, 与 DT always-on 双保险, **只重打 boot.img 即可生效, 无需重编内核**。 |
| `earlycon=mtk8250,0x11005000` | **加** | 能力已具备 (`SERIAL_EARLYCON=y`+`SERIAL_8250_MT6577=y`)。可抓 console 注册前输出; 当前靠 printk buffer 回放已够, 边际价值低但成本零。 |
| `sysrq_always_enabled` | **加** | `MAGIC_SYSRQ=y`+`SERIAL=<SERIAL> 已开; 卡死时可发 `t/w/l/b` 现场取证。 |
| `panic_print=0x1f` | **加** | panic 时多 dump (regs/locks/CPU), 辅助分析。 |
| `console=tty0` | 可删 | 无显示设备时无意义, 保留 ttyS0 即可。 |
| `ignore_loglevel` | 保留 | 或改 `loglevel=8`; 保证 vcn28 那类 WARNING 可见。 |
| `clk_ignore_unused` | 保留 | 现阶段防时钟被关; 稳定后再移除并省电。 |

**打 img 命令 (只改 cmdline, 复用现 kernel+ramdisk; appdtb 思路, 按当前 Z1 实际成功方式选带/不带 --dt):**
```
cd /home/<user>/claude/6572
cat fork_linux/arch/arm/boot/zImage fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb > /tmp/zImage_appdtb_cmd
./build/mtkbootimg/mkbootimg --kernel /tmp/zImage_appdtb_cmd \
  --ramdisk out/forensic/linux/ramdisk.gz \
  --base 0x10000000 --kernel_offset 0x8000 --pagesize 2048 --mtk 1 \
  --cmdline "console=ttyS0,115200n8 root=/dev/ram panic=10 ignore_loglevel clk_ignore_unused regulator_ignore_unused earlycon=mtk8250,0x11005000 oops=panic sysrq_always_enabled panic_print=0x1f rdinit=/init" \
  -o out/boot_mainline_panicdbg.img
```

---

## 3. 整体 boot 稳定性修复优先级清单

| # | 优先级 | 项 | 现状 | 动作 | 验证判据 |
|---|---|---|---|---|---|
| 1 | **P0** | regulator 39.5s 晚清理关机 | DTS 已修 (`mt6572-z1.dts:313-342` vemc3v3/vmc/vmch/vusb `regulator-always-on`, commit `37a481a21`, 21:30), **修后镜像尚未上板刷写验证** | 刷 `out/boot_mainline_z1_connsys_mod.img` (21:31 含修复); 同时打 `regulator_ignore_unused` cmdline 双保险 (见 §2) | 日志 0 条 `vusb/vmc/vmch: disabling`, 单 boot 存活 >60s |
| 2 | **P1** | 残留间歇性 ~19.7s 复位 (修后 cycle1) | 非确定性, 同镜像 cycle2 存活 9+ 分钟; 复位前最后消息 = power-controller `sync_state()` pending (connsys/connectivity 域) | 查 `drivers/soc/mediatek/mtk-pm-domains.c` + dts power-controller/scpsys 节点, 确认被 sync_state 关的 genpd 是否有在用 consumer; genpd 无 ignore 参数, 需从 DT 标常驻; 复现时复位前 `dmesg -c` | 连续 3 次 boot 均存活 >10min |
| 3 | **P2** | WDT 现状核对 | `mediatek_disable_wdt_early()` 已清 WDT_MODE_EN, `mtk_wdt_init` 只在 HW 已跑时置 `WDOG_HW_RUNNING`, probe 不 start → **15s 不会被 WDT 咬死** (boot#2 存活 9min 佐证) | 保留现状; 若要保底防挂, init 里 `echo V > /dev/watchdog` | — |
| 4 | **P2** | vcn28 unbalanced disables (63x/capture) | out-of-tree WMT 驱动 (mtk_wmt_wifi_soc/mtk_stp_bt_soc 等) 对 vcn28 反复不配对 disable | 独立派 agent 查 `mtk_wmt_wifi_soc` 的 `regulator_disable` 调用路径; 加 `oops=panic` 止血 | capture 内 0 条 `unbalanced disables` WARN |
| 5 | **P3** | eMMC 三项修复防回退 | `.card_busy` 勿回挂 / PB1 勿整写 / BOOTRST+400kHz 序列勿删 | 不动源码; 留注释 | 0 `-110`, CMD1 resp 全非零 |
| 6 | **P3** | 一次性复位/0 输出启动 | 偶发, 非确定性超时 | 抓复现日志 + 加 `sysrq_always_enabled`/`panic_print` 取证 | 定位到具体复位点 |
| 7 | **P4** | 吞吐 (非 boot) | 1-bit + 疑似未上 HS, 大块读可能撞 5s 软看门狗 | 上板查 `debugfs/mmc0/ios`; 如需评估 `bus-width=<4>` (先确认板级布线) | ios 显示实际 clock 档位 |
| 8 | **P4** | cmdline 收敛 | `console=tty0` 删; `clk_ignore_unused` 稳定后移除省电 | 稳定后再做 | — |

**关键约束提醒 (来自 CLAUDE.md, 别重蹈):**
- Z1 拔 USB 才能正常 boot (preloader 看 LineState 判模式); 刷写才插 download 线。
- 每次实质修复/改 img 前备份到 `~/claude/6572/bridge_backups/`, 产物即时归档 `bridge_backups/products/`, 修复验证通过即 git commit + push (仓库公开, 禁密钥)。
- 回滚兜底 `~/claude/6572/1/boot.img` (原厂零修改)。

---

## 4. 证据与行号索引 (fork_linux HEAD `37a481a21`)

- eMMC: `drivers/mmc/host/mtk-sd.c` `z1_direct_cid` `3630`; BOOTRST `3286-3296`/`3812`; PB1 基线+400kHz `3813-3824`; PB1 外科清 `2184-2194`; CMD1 BLKLEN `1265`; `.card_busy` 缺省 `3059-3064`; CMD1 探针 `1477-1507`/`1604-1626`; DAT_TIMEOUT `387`; 无 vmmc-supply `3661-3665`; `mt6572_compat` `563-574`
- dts: `arch/arm/boot/dts/mediatek/mt6572-z1.dts` mmc0 节点 `362-374`, mmc0_pins `401-419`, always-on regulators `320-342`; `mt6323.dtsi` vmc/vmch/vemc3v3/vusb
- WDT: `arch/arm/mach-mediatek/mediatek.c:35-58` (`mediatek_disable_wdt_early`); `drivers/watchdog/mtk_wdt.c`
- panic: `fork_linux/kernel/panic.c` `73` (默认), `718-743` (timeout 分派), `770-777` (==0 挂死), `1187-1195` (oops=panic); `.config:6154 CONFIG_PANIC_TIMEOUT=0`, `6153 CONFIG_PANIC_ON_OOPS=n`, `2593 CONFIG_MEDIATEK_WATCHDOG=y`
- regulator: `drivers/regulator/core.c` `6788-6839` (late_cleanup), `6841-6847` (regulator_ignore_unused), `6872-6899` (30s delayed work), `3244` (vcn28 unbalanced WARN)
- 证据 log (`mainline_recon/`): 修前复位 `z1_boot_mainline_20260814_210837.log` (39.5s regdis→Jump to BL); 修后 `z1_boot_mainline_20260814_213931.log` (regdis=0, 残留 19.7s cycle1 复位); vcn28 WARN `z1_boot_mainline_20260814_152956.log`; 历史根因 `z1_boot_emmc_cid_diag_20260727_185636.log` / `z1_emmc_test3_233101.log`
