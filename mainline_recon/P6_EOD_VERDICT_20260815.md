# P6 EOD + shell 后 ~30.5s preloader 重启 — 汇总判定 (2026-08-15)

> 汇总 `P6_EOD_REBOOT_SCENE_20260815.md`（log/场景级）+ `MMC_PART_SOURCE_20260815.md`（源码级）。
> 两份报告均完整、无缺失章节，证据链独立且结论一致。判定如下。

---

## 1. 确切根因（最可能）

**shell 后 ~30.5s preloader 重启 = 硬件 WDT（TOPRGU/mtk_wdt）饿死复位，与 p6 EOD 无关。**

判定依据（双报告一致）：

1. **确定性复位点**: 两次 boot 最后内核行都精确卡在 `[30.554540]/[30.554538]`（差 2µs）→ 固定时长的硬件倒数复位，非随机崩溃。
2. **内核 30.55s 时完全健康**: 该行是 `regulator_init_complete` 的 30s delayed work 到点 `pr_warn` 一行即返回；`panic=10` 下若 oops→panic 会停 workqueue，delayed work 不会执行 → 排除 panic。boot2 在该行之后还能 `echo SHELL2` 回显 → 复位发生在该行后数秒内。
3. **机制**: mtk_wdt probe 见 LK 遗留 `WDT_MODE_EN` → 硬件开始倒数；init 喂狗逻辑 `busybox watchdog -t 5 -T 10` 后台常驻（可用则 5s 一喂）失败时 fallback `echo V` **一次性喂** → 不常驻 → WDT 到点咬死。30.55s 固定计数 ≈ late_initcall 0.55s 处 arm + ~30s 超时。与历史 `BOOT_STABILITY_FIX_20260814.md` 的 ~19.7s TOPRGU WDT 复位同机制（那轮 15s 超时）。
4. **间歇性佐证**: `20260815_200224.log` boot3 存活过 30s 并跑完 CONNSYS → 同镜像喂狗有时生效有时不生效（后台 subshell 随 init exec getty 的终端/竞态），正好解释"部分 boot 存活、部分 30.5s 死"。

## 2. p6 EOD 截断与重启是否相关

**不相关。** 判定依据（4 条独立链）：

1. **单位澄清**: `p6 size 4292549119` 是 **512B 扇区 ≈ 2.0 TiB**，不是 4.29GB（背景"4.29GB"是单位误读）。它是 MTK EBR 生成器对最后 usrdata 分区填的"到 32-bit LBA 上限 0xFFFFFFFF"哨兵（`0xFFFFFFFF − rel_start(2418176) = 0xFFDB19FF`）。
2. **内核行为是静默截断非错误**: `block/partitions/core.c:549-563` 把 size 截为 `capacity − from` = 5215744 扇区 ≈ 2.487 GiB，分区照常创建；后续越界访问由 `bio_check_eod` 兜底 -EIO，**永不落到 MMC 控制器**。
3. **无因果通道**: 分区扫描是 0.69s 的一次性只读（只读 LBA 0 + LBA 1024，`parse_extended` 不沿 2TiB 声明读扇区）；30.55s 复位时板子空闲在 shell（无 eMMC 流量）。0.7s 事件与 30.55s 复位无任何持续活动桥接。
4. **历史佐证**: 同一行警告在每次 boot 都出现，含存活 **678s** 的 boot（`20260815_171058.log`）和跑完全部 CONNSYS 的 boot（`20260815_200224.log` boot3）。

> 结论：p6 EOD 是**出厂分区表固有行为 + 正常内核截断**，长期存在、无副作用，**不是 30.5s 重启的诱因，排查不再碰 p6。**

## 3. 修复建议

### 3.1 首选：修喂狗（重启根因）
1. 改 init 为**常驻循环喂狗**，去掉一次性 `echo V` fallback 语义：
   ```sh
   # tools/repack_ramdisk_connsys.sh:35-57 替换为
   if [ -e /dev/watchdog ]; then
     echo "[init] feeding watchdog /dev/watchdog (WDT bite prevention, persistent)"
     ($BB watchdog -t 5 -T 10 /dev/watchdog &)  # 失败时 fallback 也是循环喂
     # fallback:
     (while true; do $BB echo V > /dev/watchdog 2>/dev/null; sleep 5; done &)
   fi
   ```
2. 先确认 `busybox watchdog` applet 可用且**不退出**（binary 已含 `WDIOC_SETOPTIONS`/`WDIOC_SETTIMEOUT` 字符串，大概率可用）。
3. 验证指标：连续 **3 次 boot 存活 >60s**（过 30.55s 危险点即视为坐实 WDT 根因）；`dmesg | grep -c "Not disabling"` 确认 regulator 行仍在但不再复位。
4. 若常驻喂狗后仍 30.5s 死 → 上板抓 mtk_wdt `dev_info "Watchdog enabled (timeout=…)"` 确认实际超时值，排查第二路硬件 WDT / PMIC 复位。

### 3.2 可选（清理，非必改）
- **分区表**: 把 EBR1 p6 size 从哨兵 `0xFFDB19FF` 改为实际 `5215744`（或按物理尾 7634944 − 2419200）→ 消除每 boot 一行告警。仅治标不治本，不影响 30.5s 重启；改动需重写 EBR1 并注意 MTK 工具可能回写哨兵。**低优先级。**
- **cmdline**: `regulator_ignore_unused` 保留（无害，已确认 30.55s 行只打印不动作）；不必改。
- **驱动**: 无驱动 bug。WDT 超时可在 dts `timeout-sec` 调整，但喂狗才是正解。

### 3.3 后续上板定案动作
刷入新 ramdisk（常驻喂狗）→ 拔 download 线 → C3 桥 921600 抓 log ≥ 60s → 若不再 30.5s 重启即闭环。

## 4. 已知不一致（记录备查，不影响判定）
- **p6 绝对起点**: 场景报告表写 `0x49E00000`；源码报告经二进制 dump 自洽校验为 **`0x49D40000`**（p5 起点 0x3BB40000 + p5 size 0xE200000 = 0x49D40000，p5/p6 首尾相接）。**以源码报告的 0x49D40000 为准**。
- 两份报告对"超时值"表述不同（driver 默认 WDT_MAX_TIMEOUT=31 vs LK 遗留 30s），但都指向 ~30s 量级固定计数，不影响根因结论。

## 5. 报告缺失情况

无。两份报告均完整（场景报告 153 行、源码报告 225 行），章节齐全，证据索引互指，无缺失/截断内容。

---

### 依赖文档
- `/home/<user>/claude/6572/mainline_recon/P6_EOD_REBOOT_SCENE_20260815.md`
- `/home/<user>/claude/6572/mainline_recon/MMC_PART_SOURCE_20260815.md`
- 佐证: `/home/<user>/claude/6572/mainline_recon/BOOT_STABILITY_FIX_20260814.md`、`z1_boot_mainline_20260815_204801.log`、`z1_boot_mainline_20260815_171058.log`、`z1_boot_mainline_20260815_200224.log`
- 分区 dump: `/home/<user>/claude/6572/1/{MBR,EBR1,MT6572_Android_scatter.txt}`
