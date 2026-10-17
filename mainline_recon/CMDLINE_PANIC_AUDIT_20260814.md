# Z1 (MT6572) mainline cmdline / panic 审计 — 2026-08-14

> 只读分析。数据来源: boot 映像实际 cmdline、boot log 全量 grep、`fork_linux` 源码与 `.config`。
> 结论先行: **Z1 频繁重启不是 panic 隐藏, 是 regulator/电源域被"无用资源回收"关掉引发的硬件复位**。当前 cmdline 反而会让真 panic 变成"卡死"而非重启。

---

## 0. 实测 cmdline (从已刷 boot 映像 header 读出, 非假设)

从 `out/boot_mainline_z1_connsys_mod.img` (MTK header, cmdline 位于 offset 64) 提取:

```
console=tty0 console=ttyS0,115200n8 root=/dev/ram panic=0 ignore_loglevel clk_ignore_unused rdinit=/init
```

与任务描述完全一致。`out/boot_mainline_z1_emmc_clean_writefix.img`、`out/boot_mainline_connsys.img` 同为这一条。

---

## 1. panic=0 的影响 —— 与直觉相反: 不 panic, 而是永久挂死 (hang), 不会重启

mainline 7.0-rc7 `kernel/panic.c` 的语义 (与 Android 3.4 时代不同, 别用老经验):

- `panic_timeout` 默认 = `CONFIG_PANIC_TIMEOUT` = 0 (`.config:6154`)。
- cmdline `panic=0` → `panic_timeout = 0`。

`vpanic()` 中 (kernel/panic.c:718-743, 770-777):

```c
if (panic_timeout > 0) {                 /* >0: 打印 "Rebooting in N seconds.." 并延时 N 秒 */
    pr_emerg("Rebooting in %d seconds..\n", panic_timeout);
    ... mdelay ...
}
if (panic_timeout != 0) {                /* !=0: 调 emergency_restart() 重启 */
    emergency_restart();
}
pr_emerg("---[ end Kernel panic - not syncing: %s ]---\n", buf);
...
for (i = 0; ; i += PANIC_TIMER_STEP) {   /* panic_timeout==0 走这里: 无限循环, 永久挂死 */
    touch_softlockup_watchdog();
    ... mdelay ...
}
```

结论:
- **`panic=0` 时 panic 表现为"挂死", 不是重启。** 常见的"panic=0 立即重启"说法在这条 mainline 上是错的。
- `panic=10` → 打印 panic 信息 + "Rebooting in 10 seconds.." + 等 10 秒再 `emergency_restart()`。
- `panic=-1` → **立即重启** (跳过 >0 的延时块, 但 `!=0` 块直接 `emergency_restart()`), 不适合调试。
- panic 信息 (`Kernel panic - not syncing: ...`) 在挂死/重启**之前**就 `pr_emerg` 打印了。Z1 的 ttyS0 console 全程可用 (log 里有 `console [ttyS0] enabled`), 所以真 panic 一定会在 UART log 里留下 "Kernel panic" 字样。

**对 Z1 的推论**: 如果真有 panic, 现象会是"UART 先出 Kernel panic 字串, 然后静默卡死"。当前板子"卡死/重启"里**没有任何 panic 字串** (见 §2), 所以:

> **板子重启不是 panic 被 panic=0 隐藏。** 但有另一种组合: panic=0 挂死 + `CONFIG_MEDIATEK_WATCHDOG=y` 的硬件 WDT 在挂死期间没人喂 → WDT 复位 → 表现为"卡死若干秒后重启"。即便如此, panic 字串也早已打印, log 里不存在。

---

## 2. boot log 全量 grep: panic / Oops / BUG / WARNING

对 `mainline_recon/z1_boot_mainline_2026*.log` (100+ 个, 含 07/12 ~ 08/14 全部) 全量 grep:

- **`Kernel panic`: 0 命中** (仅 3 个 lima 测试 log 里命中的是脚本自身的 grep 模式字串, 非真实 panic)。
- **`Oops` / `BUG:` / `not syncing`: 0 命中**。
- **`WARNING:`: 2 类真实命中, 均非致命但值得记录**:

  1. `WARNING: drivers/regulator/core.c:3244 at _regulator_disable+0x140/0x1a0, CPU#0/1: mtk_wmtd/64`
     - 内容: `unbalanced disables for vcn28` — WMT 无线守护进程 `mtk_wmtd` (pid 64) 反复对 `vcn28` 做**不配对 disable** (enable_count==0 时再 disable)。
     - 证据: `z1_boot_mainline_20260814_152956.log` 一次 capture 内出现 **63 次**, 从 [37.5s] 起约每 1.5s 一次。模块栈: `wlan_gen2 / mtk_wmt_wifi_soc / mtk_stp_bt_soc / mtk_stp_wmt_soc / mtk_btif_drv / cfg80211`。
     - 影响: 不 panic (CONFIG_PANIC_ON_OOPS 未开, WARN 后继续), 但说明 out-of-tree WMT 驱动在错误地操作 vcn28 (连接子系统电源轨)。反复开关 vcn28 可能使 WMT/WiFi 子系统进入坏态, 与本板重启/卡死可能是同源问题 (见 §5.4)。
  2. `WARNING: net/core/dev_addr_lists.c:520 at dev_addr_check+0x108/0x130, CPU#0: ifconfig/225`
     - `z1_boot_mainline_20260814_202853.log` [93.1s], 用户态 ifconfig 触发, 与 boot 无关。

**结论**: 无任何被忽略/吞掉的 panic。板子重启不是内核 panic 路径产生的。

---

## 3. clk_ignore_unused 分析

- 实测生效: 每个 boot 都有 `[    9.4s] clk: Not disabling unused clocks` (clk_disable_unused 被跳过)。
- `clk_ignore_unused` 是**缓解项不是掩盖项**: 它防止 `clk_disable_unused` 把"还没被谁使用"的时钟关掉, 从而**减少**"时钟被关但某驱动仍需要 → 死机/复位"这一类问题。它不会掩盖任何时钟问题, 只是让时钟保持开启。
- 保持开着的代价: 全系统时钟不进入低功耗, 电量消耗略增 (听力机电池供电, 长期看应移除), 但不构成复位原因。
- **关键类比缺口**: 时钟有 `clk_ignore_unused`, 但**本 cmdline 没有给 regulator 上同样的保险**。内核其实有对应的 `regulator_ignore_unused` (drivers/regulator/core.c:6841-6847 `__setup("regulator_ignore_unused")`)。而 Z1 的重启正是**regulator 被"无用回收"关掉**引起的 (见 §5.1)。

---

## 4. console=tty0 (虚拟终端) 分析

- `.config` 开了 `CONFIG_VT=y` + `CONFIG_VT_CONSOLE=y` + `CONFIG_DUMMY_CONSOLE=y` + `CONFIG_FRAMEBUFFER_CONSOLE=y`。
- Z1 无有效显示设备时 fbcon 不注册, VT 绑到 dummy console → tty0 收到的 printk 输出被丢弃, 无害。
- 每个 boot log 有 `console [tty0] enabled` + `console [ttyS0] enabled`, printk 会同时送两个 console。tty0 是最后一个参数之外的普通 console, `/dev/console` 由最后一个 `console=` (ttyS0) 决定。
- **结论: console=tty0 无害但无用**, 不会导致挂死/重启。可留可删; 若担心 VT 子系统在无显示时异常, 可移除只留 `console=ttyS0,115200n8`。

---

## 5. 关键建议: panic 配置 + 定位卡死的 cmdline 方案

### 5.1 先说真正的根因 (不是 panic): ~39.5s regulator 晚清理关机 → 硬件复位

boot log 铁证 (`z1_boot_mainline_20260814_210837.log`, 修复前映像):

```
[   19.7s] mtk-power-controller ... sync_state() pending due to ...
[   39.511217] vusb: disabling
[   39.514551] vmc: disabling
[   39.517753] vmch: disabling
< 下一字节就是 preloader banner "F0: 1028 0000" / "Jump to BL" = 硬件复位重跑 >
```

- 机制: `regulator_init_complete` (drivers/regulator/core.c:6872-6899) 是 late_initcall_sync, 调度一个 **30 秒后** 的 delayed work (`regulator_init_complete_work`, msecs_to_jiffies(30000))。到点后 `regulator_late_cleanup()` (core.c:6788-6839) 把所有 "无 consumer 且未标 always_on" 的 regulator 关掉。
- `vusb/vmc/vmch` (USB PHY 与 eMMC/SD 电源轨) 在 Z1 fork 里没有 consumer (`mmc0` 无 `vmmc-supply`), 于是被判定 unused → 39.5s 关掉 → **板子复位** (重启循环, 每 ~2 分钟一轮, 与 12 次 "Jump to BL" 对齐)。
- **已有修复**: `arch/arm/boot/dts/mediatek/mt6572-z1.dts:313-342` 已把这四个 regulator 标 `regulator-always-on` (vemc3v3/vmc/vmch/vusb), 注释写明同因。该 DTS 修复于 **2026-08-14 21:30** 提交, `out/boot_mainline_z1_connsys_mod.img` (21:31) 内嵌的 appended dtb 已带 always-on (已解包验证)。
- **修复后残留**: `z1_boot_mainline_20260814_213931.log` (21:39, 应为修复映像) 里 `vusb: disabling` 已消失 (0 命中), 但 cycle1 仍在 ~19.7s `mtk-power-controller sync_state()` 字串后立刻复位 (cycle2 又成功活过 19.7s 与 39.5s, 存活到 capture 结束)。即 regulator 关机触发已消, 但**仍残留一个间歇性的 ~19.7s 电源域 (scpsys/mtk-power-controller sync_state) 复位**, 需另查 (power domain 常驻 / fw_devlink 处理)。

### 5.2 推荐 cmdline (定位卡死 + 止血)

```
console=ttyS0,115200n8 root=/dev/ram panic=10 ignore_loglevel clk_ignore_unused \
regulator_ignore_unused earlycon=mtk8250,0x11005000 oops=panic sysrq_always_enabled \
panic_print=0x1f rdinit=/init
```

逐项理由:

| 参数 | 改法 | 理由 |
|---|---|---|
| `panic=10` | **0 → 10** | 真 panic 时打印 `Kernel panic - not syncing` + `Rebooting in 10 seconds..` 再重启。10 秒足够在 UART 上读到信息, 且避免 panic=0 挂死后被 WDT 无标识复位 ("卡死几秒→重启"无法区分)。**不要用 panic=-1** (立即重启, 丢失证据)。 |
| `oops=panic` | **加** | CONFIG_PANIC_ON_OOPS 未开 (oops 默认继续跑)。加此参数把 oops 转成 panic, 防止"oops 后系统带伤运行→随后无痕复位"。尤其 mtk_wmtd 的 vcn28 反复误 disable (core.c:3244) 属于这类带伤运行风险。 |
| `regulator_ignore_unused` | **加** | 内核自带 (core.c:6841-6847)。直接停掉 39.5s 的 regulator 晚清理关机, 与 DT always-on 双保险, 立即生效无需重编内核 (只重打 boot.img)。 |
| `earlycon=mtk8250,0x11005000` | **加** | `.config` 已满足 (`CONFIG_SERIAL_EARLYCON=y` + `CONFIG_SERIAL_8250_MT6577=y`)。可抓到 ttyS0 注册前 (前几 ms) 的输出。当前 log 能靠 printk buffer 回放拿到 [0.000000], 边际价值低, 但成本为零, 若未来 hang 发生在 console 注册前就是救命稻草。 |
| `sysrq_always_enabled` | **加** | `CONFIG_MAGIC_SYSRQ=y` + `MAGIC_SYSRQ_SERIAL=<SERIAL> 已开; 此参数确保串口 sysrq 可用, 卡死时可发 `t/w/l/b` 现场取证。 |
| `panic_print=0x1f` | **加** | panic 时多 dump (regs/locks/CPU), 辅助分析。 |
| `console=tty0` | 可删 | 无显示设备时无意义 (见 §4)。保留 ttyS0 即可。 |
| `ignore_loglevel` | 保留 | 或改 `loglevel=8`。保证 WARNING 可见 (否则 152956 里那 63 次 vcn28 WARN 会被吞)。 |
| `clk_ignore_unused` | 保留 | 现阶段保留 (防时钟被关)。稳定后再移除以暴露时钟 bug, 并省电。 |

### 5.3 打 img 的命令 (只改 cmdline, 复用现 kernel+ramdisk)

沿用 appdtb 思路 (zImage 尾部 cat dtb, mkbootimg 不带 --dt, 或按当前 Z1 实际成功方式):

```
cd /home/<user>/claude/6572
cat fork_linux/arch/arm/boot/zImage fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb > /tmp/zImage_appdtb_cmd
./build/mtkbootimg/mkbootimg --kernel /tmp/zImage_appdtb_cmd \
  --ramdisk out/forensic/linux/ramdisk.gz \
  --base 0x10000000 --kernel_offset 0x8000 --pagesize 2048 --mtk 1 \
  --cmdline "console=ttyS0,115200n8 root=/dev/ram panic=10 ignore_loglevel clk_ignore_unused regulator_ignore_unused earlycon=mtk8250,0x11005000 oops=panic sysrq_always_enabled panic_print=0x1f rdinit=/init" \
  -o out/boot_mainline_panicdbg.img
```

> 注意: 若当前成功映像用的是 `cat zImage dtb` + **不带 --dt** (如 `boot_mainline_z1_connsys_mod.img` dt_size=0, 走 mainline APPENDED_DTB), 就照上面不带 --dt; 若像 appdtbdual 那样需要 LK 看到非 0 dt_size 则加 `--dt ...`。打包前先照抄当前最后一次成功打包脚本的调用。

### 5.4 非 cmdline 的跟进项 (同样重要)

1. **重新刷 `out/boot_mainline_z1_connsys_mod.img`** (含 21:30 的 always-on 修复) 并验证 `vusb: disabling` 不再出现。regulator_ignore_unused cmdline 只是止血, DT always-on 才是根治。
2. **残留的 ~19.7s 复位**: 与 `mtk-power-controller sync_state()` 关联, 间歇性 (同 capture 内 cycle2 存活)。查 `drivers/soc/mediatek/mtk-pm-domains.c` 与 Z1 dts 的 power-controller/scpsys 节点, 确认被 sync_state 关掉的 power domain 是否有仍在使用的 consumer (firmware/devlink 没到位)。这类与 clk/regulator 同族, 但 genpd 侧没有现成 ignore 参数, 需从 DT 侧标常驻。
3. **mtk_wmtd "unbalanced disables for vcn28"** (core.c:3244, 一次 capture 63 次): out-of-tree WMT 驱动 (wlan_gen2/mtk_stp_*) 的 regulator 使用有 bug, 反复误关 vcn28 连接轨。可能正是它把 WiFi/BT 子系统带崩。值得作为独立问题派 agent 查 mtk_wmt_wifi_soc 的 regulator_disable 调用路径。

---

## 6. 证据索引

- cmdline 实测: MTK boot header offset 64, 从 `out/boot_mainline_z1_connsys_mod.img` / `boot_mainline_z1_emmc_clean_writefix.img` / `boot_mainline_connsys.img` 读出。
- panic 语义: `fork_linux/kernel/panic.c:73` (panic_timeout=CONFIG_PANIC_TIMEOUT), `:718-743` (timeout 分派), `:770-777` (panic_timeout==0 无限挂死), `:1187-1195` (oops=panic)。
- panic_timeout 默认值: `.config:6154 CONFIG_PANIC_TIMEOUT=0`; `CONFIG_PANIC_ON_OOPS` 未开 (6153)。
- WDT: `.config:2593 CONFIG_MEDIATEK_WATCHDOG=y` (挂死期间喂狗停 → 复位, 解释"卡死→重启"的 WDT 通道)。
- regulator 晚清理: `drivers/regulator/core.c:6788-6839` (late_cleanup), `:6841-6847` (regulator_ignore_unused), `:6872-6899` (30s delayed work), `:3244` (unbalanced disables WARN)。
- DTS always-on 修复: `arch/arm/boot/dts/mediatek/mt6572-z1.dts:313-342` (vemc3v3/vmc/vmch/vusb `regulator-always-on`, 注释写明 ~39.5s 关电→板子复位), 修改于 2026-08-14 21:30。
- 复位证据 (修复前): `mainline_recon/z1_boot_mainline_20260814_210837.log` — `vusb/vmc/vmch: disabling` 后紧跟 preloader `F0: 1028`, 一次 capture 6 次 reboot 5 次 regdis。
- 修复后残留: `mainline_recon/z1_boot_mainline_20260814_213931.log` — regdis=0, 但 cycle1 在 `mtk-power-controller sync_state()` 后 ~19.7s 复位。
- vcn28 WARN: `mainline_recon/z1_boot_mainline_20260814_152956.log` (63 次)。
- 无 panic 证据: 全 `z1_boot_mainline_2026*.log` grep `Kernel panic|Oops|BUG:|not syncing` 均 0 命中。
- earlycon/8250 能力: `.config:1902 SERIAL_EARLYCON=y`, `:1920 SERIAL_8250_MT6577=y`, `:6267-6293 DEBUG_LL=y` (DEBUG_UART_PHYS=0x11005000)。
- sysrq: `.config:6090-6093 MAGIC_SYSRQ=y / SERIAL=<SERIAL>; `drivers/tty/sysrq.c:90-98 sysrq_always_enabled`.
