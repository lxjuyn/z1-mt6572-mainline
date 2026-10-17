# Z1 mainline initcall 卡死 汇总判定 (VERDICT)

> 2026-08-14 · 只读汇总。输入: `INITCALL_HANG_SCENE_20260814.md`(log 实测) + `DEFERRED_PROBE_SOURCE_20260814.md`(源码/dd.c 分析)。
> 结论已回查原始 boot log 抽查验证(`z1_boot_mainline_20260814_002746.log` / `124449.log` / `213931.log`)。

---

## 0. 一句话结论

**当前 (#91, `7.0.0-rc7-z1+`, 含提交 `a00567cbe`) 不存在任何 initcall 卡死。** 全部实机 log 中 578/578 initcall 均 returned，`deferred_probe_initcall` 恒在 ~9.2s 以 67~75ms 返回 0，每段都到达 `Run /init` + 交互 shell。
**历史上真正的"卡死"(内核 #90, 5/5 全挂) 是 late_initcall 段结束后、下一个 initcall 打印之前 (efi_shutdown_init 之后 ~9.31s) 的 CPU 级 stall，不是某 initcall 函数阻塞**，与 `adc-battery` 永久 EPROBE_DEFER(缺 `mt6323-auxadc` cell) 强相关，已由提交 `a00567cbe` 修复。
**"板子反复重启/看起来没起来" 的剩余嫌疑是 WDT 咬死**(timeout=15s nowayout=0 无人喂狗，段1 到 shell 后 ~19.7s 回 preloader)，与 initcall 无关。另有 1/10 偶发 VFS root panic = 独立 initrd/rootfs 交接问题。

---

## 1. initcall 卡死的确切根因

### 1.1 当前日志里没有卡死 (INITCALL_HANG_SCENE 报告的核验结论 — 已回查 `213931.log` 确认)

| 证据 | 值 (两段 boot 均一致) |
|---|---|
| initcall 总数 | 每段 578 个，578 全部 returned，0 未闭合 |
| `deferred_probe_initcall` | 9.20s 进 → 9.27s 回，`returned 0 after ~68290 usecs` |
| 最后输出 | `Freeing unused kernel image` → `Run /init as init process` (9.50/9.51s) |
| shell | `~ #` 出现，第 2 段 `echo SHELLOK/SHELLOK2/SHELLNOW` 全回显 |

"9.x 秒像卡住" = **Run /init 后约 10s 内核控制台静默窗口** (fw_devlink `deferred-probe-timeout` 默认 10s，超时后 19.67s 打 5 条 sync_state + shell 提示符)。在 9.x 秒掐断抓取会把尾部停在 `of_fdt_raw_init / xdp_metadata_init / regulator_init_complete` 一带 → 误判为"卡在 initcall"。这些 initcall 全部 ≤6ms returned。

### 1.2 历史真卡死 (内核 #90) — 是 late_initcall 段结束后的 CPU stall

`z1_boot_mainline_20260814_002746.log` 最后几行(已回查确认，文件就停在这):
```
[9.310695] calling  efi_shutdown_init+0x0/0x58 @ 1
[9.316521] initcall efi_shutdown_init+0x0/0x58 returned -19 after 0 usecs
# ← 之后无任何输出 (9.5 分钟，capture_end 超时)
```
成功 log (#91 `124449.log`) 同一位置继续 `calling of_fdt_raw_init` → ... → shell。

即: **卡死发生在最后一个 late_initcall (`efi_shutdown_init`) 返回之后、下一个 late_initcall (`of_fdt_raw_init`) 的 `calling` 打印之前**。此时没有 initcall 函数在执行 — 是 CPU 级 stall(中断风暴 / 自旋 / hard lockup 类)，**不是"某 initcall 被 deferred probe 阻塞"**。

### 1.3 根因归属 (DEFERRED_PROBE_SOURCE 的分析 — 已回查确认)

- **机制侧 (dd.c)**: 永久 -EPROBE_DEFER 的设备只是反复被重排队，probe 每次 ~100us 就返回，`flush_work` 不会因它卡住。`deferred_probe_initcall` 在有永久 defer 设备的 #90 里照样 67ms 返回 0。**deferred probe 机制本身从没卡过 Z1。**
- **强相关项**: 内核 #90 (`a00567cbe^`) 的 `mt6323_diagnostic_devs` 缺 `mt6323-auxadc` cell → `mt6323-auxadc` 设备不存在 → `adc-battery` 的 `io-channels=<&mt6323_adc ...>` 永远拿不到 IIO channel → 永久 -EPROBE_DEFER (log 里 `probe of adc-battery returned 517` 两次)。
- **修复**: 提交 `a00567cbe` 把 `mt6323-auxadc` cell 加进 `mt6323_diagnostic_devs` (auxadc probe 只需 pwrap regmap，不依赖 MT6323 IRQ 域) → #91 log 里 `probe of mt6323-auxadc returned 0`、`probe of adc-battery returned 0`、`deferred_probe_initcall returned 0 after 68249 usecs`，boot 9/10 进 shell。
- **诚实边界**: 静态分析无法 100% 证明"永久 defer 直接导致 CPU stall" (#90 与 #91 之间可能混入其他构建差异)。但 5/5 挂 vs 9/10 成的统计 + 唯一 deferred 差异就是 auxadc 链，工程上可采信 a00567cbe 已修复。

---

## 2. 是设备 probe 阻塞还是 eMMC 时序？与历史 bootloop 的关系

**两者都不是本轮卡死的主因。**

| 假说 | 判定 |
|---|---|
| 设备 probe 阻塞 initcall | **否**。deferred_probe_initcall 恒 ~9.2s 返回 0；唯一会卡 initcall 的路径(probe() 内部自阻塞)在当前树无候选(AFE 因 GIC_SPI 0 已关 SND 规避)。 |
| eMMC 时序 | **否(与历史 bootloop 同源但本轮已过)**。历史 200250/214311/022635 非确定性 bootloop 是 eMMC 抓 log 时序 + appdtb 拼接 + WDT 回归(slimv3 误关 CONFIG_WATCHDOG) 时代的产物。本轮 log 两段 boot 顺序/耗时几乎完全一致，确定性极高，均到 shell — 非 eMMC 时序。 |
| CPU 级 stall (历史 #90) | **是历史真卡死**。9.31s 位于 efi_shutdown_init 之后，无 initcall 执行。已由 a00567cbe 修掉。 |
| WDT 咬死重启 (当前剩余嫌疑) | **最可能解释"反复重启/起不来"现象**。log: `mtk-wdt 10007000.watchdog: Watchdog enabled (timeout=15 sec, nowayout=0)` @7.13s；initramfs busybox 无喂狗动作；段1 到 shell 后 ~19.7s 回 preloader。WDT 复位窗口(~22s)与重启点(~19.7s)同量级。另一可能 = 操作者手动断电(无法区分，capture 头 `image=<not-supplied>`)。另: 近期提交 `37a481a21` 修过一个 ~39s preloader 重启环(regulator always-on)，同族问题。 |
| 偶发 VFS root panic (1/10) | **独立问题**。initrd/rootfs 交接：`linux,initrd-start/end` 与 LK 实际加载的 MTK ROOTFS payload 长度对不齐(`rootfs image is not initramfs ... XZ decoder` / `check access for rdinit=/init failed: -2`)。与 deferred probe 无关。 |

---

## 3. 修复建议 (按优先级)

### 已修复/无需动作
1. **auxadc/adc-battery 链 (历史卡死根因)**: 已由提交 `a00567cbe` 修复，无需再改。上板验证 #91 (vermagic `-z1`) 已 9/10 进 shell。**确认刷入的内核确实含 a00567cbe 与 `CONFIG_LOCALVERSION="-z1"`** (下次抓 log 记录 boot.img 路径/哈希，当前 capture 头 `image=<not-supplied>` 无法追溯)。

### 建议动作
2. **消除 WDT 咬死重启 (当前"起不来"表象的候选根因，优先验证)**:
   - 短期诊断: initramfs 里喂狗 (`echo V > /dev/watchdog` 或启动喂狗进程)，观察是否还 ~20s 重启；不重启即证实 WDT。
   - 根治选项(选一): dts `&watchdog { status="disabled"; }`(mt6572-z1.dts watchdog@10007000) / `.config` `CONFIG_WATCHDOG_NOWAYOUT=y` / initramfs 持续喂狗。注意 CLAUDE.md 记的 `mediatek_disable_wdt_early()` 早停狗在本 log 未生效(WDT 7.13s 仍 enabled)，要么该改动没编进当前内核，要么 LK 复位后重开 WDT — 上板时确认。

3. **缩短 10s 静默窗口 (可选，纯观感/可诊断性)**:
   - 已确认本树支持 cmdline `deferred_probe_timeout=` (dd.c:274 `__setup`)。传 `deferred_probe_timeout=0` 会禁用超时 work(不再打 sync_state 消息，power domain 保持上电 — Z1 无运行时 PM 依赖，可接受)；或传小值如 `deferred_probe_timeout=1` 让 sync_state 提前。若想保留默认 10s 行为则不改。

4. **dts status 调整 (可选，清理用，不影响 boot)**:
   - `connectivity@18070000` / `wifi@180f0000` / `btif@1100e000` 由外部 .ko 绑定，initcall 阶段挂设备不 probe，压住 4 个 vcn regulator 的 sync_state(`sync_state() pending due to 18070000.connectivity` 5 条) — **良性**，不卡 boot。若想让日志干净且不依赖外部 ko 时省电，可临时置 `status="disabled"`，但正常流程保留 (insmod 前需 probe 节点)。
   - `adc-battery` (z1.dts:273-280) 保持 `status="okay"` — #91 已成功，勿改。

5. **修 initrd/rootfs 交接 (1/10 偶发 VFS panic)**:
   - 核对 dts `linux,initrd-start/end`(或 /chosen)与 LK 实际加载的 MTK ROOTFS payload 长度一致；`rdinit=/init` 已对。属独立问题，见 §2。

6. **抓 log 纪律**: `z1_capture_mainline.sh 1800` 跨过 10s 静默窗口，等到 `~ #`/shell 交互再停；capture 头传 boot.img 路径/哈希。

---

## 4. 报告缺失 / 局限

- **两份报告均完整存在**，无缺失。交叉抽查 (002746/124449/213931 三个 log) 与报告内容一致。
- 无法从 log 100% 区分的点: (a) 段1 ~19.7s 重启是 WDT 还是手动断电 (`image=<not-supplied>`)；(b) 永久 defer→CPU stall 的因果是强相关非铁证(#90 与 #91 间可能有其他构建差异)；(c) 卡死点(9.31s)的最终中断/硬件级根因需 DEBUG_LL 或对拍复现才能定死 — 建议在 #91 上临时 `adc-battery status="disabled"` 或 `echo 1 > /sys/bus/platform/drivers/.../bind` 对拍复现，确认 auxadc 链因果。

---

## 5. 关键证据文件索引

- 汇总输入: `mainline_recon/INITCALL_HANG_SCENE_20260814.md`、`mainline_recon/DEFERRED_PROBE_SOURCE_20260814.md`
- 历史卡死 log (#90): `mainline_recon/z1_boot_mainline_20260814_002746.log` (尾停 `efi_shutdown_init ... returned -19`，adc-battery 两次 517)
- 成功 log (#91): `mainline_recon/z1_boot_mainline_20260814_124449.log` (auxadc/adc-battery returned 0，过 9.31s 卡点)
- 场景 log (2 段全到 shell + WDT + sync_state): `mainline_recon/z1_boot_mainline_20260814_213931.log`
- 机制: `fork_linux/drivers/base/dd.c` (deferred_probe_initcall L345-376, timeout L261-322, `deferred_probe_timeout=` L274)
- 修复提交: `fork_linux/drivers/mfd/mt6397-core.c` L163-178, commit `a00567cbe`
- auxadc: `fork_linux/drivers/iio/adc/mt6323-auxadc.c` L249-283
- dts: `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts` (adc-battery L273-280, connectivity L175-195)
