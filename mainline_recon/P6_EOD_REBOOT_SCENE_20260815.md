# P6 EOD 截断 + shell 后 ~30.5s 重启 — 场景分析 (2026-08-15)

> 只读分析。对象: `mainline_recon/z1_boot_mainline_20260815_204801.log`（8 字节结构，含 2 次 boot，两者都在 ~30.55s 重启）。
> 对比: `z1_boot_mainline_20260815_200224.log`（19min 捕获，boot3 存活过 30s 并跑完 CONNSYS）、`z1_boot_mainline_20260815_171058.log`（12 boot，boot 存活到 678s）、MBR/EBR1 原始 dump、工厂 scatter。

---

## 0. TL;DR

1. **重启点**: 两次 boot 的最后一条内核行都是 `[ 30.554540] / [ 30.554538] regulator: Not disabling unused regulators`，随后 preloader 重新打印 F0…Jump to BL。板子 shell 空闲 ~29.5s 后重启，**总存活 ≈ 30.55s**，两次 boot 只差 2µs → 固定时长硬件复位。
2. **该 regulator 提示本身无害**: cmdline 带 `regulator_ignore_unused`，`regulator_init_complete` 的 30s delayed work 到点只 `pr_warn` 一行并 `return`，不碰任何 regulator。boot2 在该行打印后还能 `echo SHELL2` 回显 → 复位发生在 30.55s 之后，不是这条 printk 触发。
3. **p6 EOD 截断与重启无关**:
   - `p6 size 4292549119` 的单位是 **512B 扇区**（不是字节），= ~2.0 TiB，是 MTK EBR 对最后 userdata 分区惯用的"到 2TiB 盘尾"哨兵值（`0xFFFFFFFF − start_lba`）。eMMC 实际 3.64 GiB，内核按 `from+size > capacity` 把 p6 截到 EOD。
   - **同一行警告在所有历史 log 的每一次 boot 都出现**（含存活 678s 的 boot、跑通全部 CONNSYS 模块的 boot），截断只是把分区 size 封顶，不产生 I/O 错误/挂死/复位。
4. **重启最可能根因 = 硬件 WDT 饿死复位**（与 `BOOT_STABILITY_FIX_20260814.md` 记录的 ~19.7s TOPRGU WDT 复位同机制，本次 ~30.5s）。init 的喂狗逻辑依赖 `busybox watchdog -t 5 -T 10` 后台常驻，失败时 fallback `echo V` 是一次性喂 → 不常驻 → WDT 到点咬死。30.55s 的 regulator delayed work 只是"碰巧也是 30s 量级"的最后一条刷屏，**不是复位原因**。
5. 需上板验证: 确认喂狗常驻有效后看是否不再 30s 重启。

---

## 1. 日志结构与 boot 边界（byte offset 定位）

`z1_boot_mainline_20260815_204801.log` 共 8041 B，含 2 次完整 boot（分隔符是 `\r`，`wc -l` 的 48 行不反映真实结构，用 byte offset）：

| 事件 | byte offset | 说明 |
|---|---|---|
| preloader banner + Jump to BL | 0–421 | boot1 开始（preloader→LK） |
| `[ 0.000000] Linux version 7.0.0-rc7-z1+ #91` | 2455 | boot1 内核 banner |
| `[ 0.690686] mmcblk0: p6 size 4292549119 extends beyond EOD, truncated` | 3421 | boot1 p6 EOD（唯一分区异常） |
| `[init] uname: Linux version …` | 3516 | boot1 init（[init] 无内核时间戳） |
| `[init] feeding watchdog /dev/watchdog (WDT bite prevention)` | ~3520 | boot1 喂狗段 |
| `~ # [ 30.554540] regulator: Not disabling unused regulators` | 3783 | **boot1 最后一条内核行** |
| preloader banner + Jump to BL | 3963 | boot1 重启 |
| `[ 0.000000] Linux version … #91` | 5917 | boot2 内核 banner |
| `[ 0.680570] mmcblk0: p6 … EOD, truncated` | 6883 | boot2 p6 EOD |
| `[init] uname / feeding watchdog` | 6978 | boot2 init |
| `~ # [ 30.554538] regulator: Not disabling unused regulators` | 7245 | **boot2 最后一条内核行** |
| `~ # echo SHELL2` → `SHELL2` → `~ #` | 7331 | boot2 在 regulator 行之后仍能交互 |
| preloader banner + Jump to BL | 7487 | boot2 重启 |

cmdline（两次 boot 相同）:
```
console=tty0 console=ttyS0,115200n8 root=/dev/ram panic=10 loglevel=6
clk_ignore_unused regulator_ignore_unused oops=panic sysrq_always_enabled rdinit=/init
```

---

## 2. 重启点确认

**两次 boot 的"最后内核行"都精确卡在 30.55s**: `[30.554540]` / `[30.554538]`（差 2µs）。这证明:

- 30s delayed work 是**确定性**的（`regulator_init_complete` late_initcall + `schedule_delayed_work(…, 30000ms)`，见 `drivers/regulator/core.c:6889-6899`）。
- 内核在 30.5545s 时**完全正常**（workqueue 正常执行 → 无 panic 无挂死）。
- `panic=10` 排除: 若是 oops→panic，panic 会停掉 workqueue，30.55s 的 delayed work 不会执行。
- boot2 在 30.554538 之后还能 `echo SHELL2` 并回显 → **复位发生在该行之后的数秒内**，不是 printk 本身触发。

所以重启点 = **boot 后 ~30.55s（shell 空闲 ~29.5s），复位在 regulator 提示之后**，与任务描述一致。复位形态 = 全板复位回 preloader（不是内核 panic 打印 + 10s 倒计时）。

---

## 3. p6 EOD 完整上下文

### 3.1 磁盘/分区来源

- 内核报告: `mmcblk0: mmc0:0001 H4G1d 3.64 GiB`（Hynix 4GB eMMC，实测 3.64 GiB ≈ 3.91 GB，~7.66M 扇区）。
- 分区表是 **MBR + EBR（msdos 风格），不是 GPT**（`block/partitions/msdos.c`）。
- 内核解析输出: `mmcblk0: p1 < p5 p6 > p2 p3 p4` — `< >` 是 msdos.c 对扩展分区(E)的标记语法: p1=扩展分区占位，p5/p6=扩展区内逻辑分区，p2/p3/p4=其余主分区。

### 3.2 MBR/EBR dump 实测（`/home/<user>/claude/6572/1/MBR`、`EBR1`，零修改备份）

| 内核分区号 | 来源 | type | start (byte) | 声明 size | 判定 |
|---|---|---|---|---|---|
| p1 | MBR 表项1 | 0x05 扩展 | 0x80000 | 0xFFFFFFFF 扇区 | 内核只建 2 扇区占位（防 mkfs） |
| p2 | MBR 表项2 | 0x83 | 0x900000 | 0xA00000 = 10 MiB | ✅ 正常 |
| p3 | MBR 表项3 | 0x83 | 0x1300000 | 0xA00000 = 10 MiB | ✅ 正常 |
| p4 | MBR 表项4 | 0x83 | 0x3740000 | 0x38400000 = 900 MiB | ✅ 正常 |
| p5 | EBR1 表项1 | 0x83 | 0x3BB40000 | 0xE200000 = 226 MiB | ✅ 正常 |
| **p6** | EBR1 表项2 | 0x83 | 0x49E00000 | **4292549119 扇区 ≈ 2.0 TiB** | ❌ **超 EOD，内核截断** |

对应 MTK 工厂分区（scatter `1/MT6572_Android_scatter.txt`）: p2-p4 ≈ 前段中间分区，p5 ≈ cache 区，**p6 = usrdata（userdata，最后一个数据分区）**。

### 3.3 p6 超 EOD 的数字含义（关键澄清）

- 内核消息 `p6 size 4292549119` 的 `size` 是 **sector_t（512B 扇区）**（`block/partitions/core.c:533-550`）。
- **4292549119 扇区 × 512 = 2,197,785,148,928 B ≈ 2.0 TiB**，不是 4.29 GB。任务背景里"p6 声明 4.29GB"是**单位误读**（把扇区数当字节数）。
- 该值 = `0xFFFFFFFF − 2418176`（EBR1 表项2 的 rel_start）——MTK Flash 工具对最后 userdata 分区填"到 2TiB 可寻址盘尾"的哨兵值。eMMC 实际只有 3.64 GiB，内核在 `from + size > get_capacity()` 分支把 size 截为 `capacity − from`，log 落 `truncated`，p6 实际变成 `[0x49E00000, EOD)` ≈ 2.4 GiB。
- **这是 MTK 出厂分区表固有行为，不是新引入的坏分区。** 所有分区中只有 p6 一个超 EOD，且每次 boot 都报同一行，行为完全确定。

---

## 4. p6 EOD 截断是否导致 eMMC 异常 → preloader 重启？—— 否

证据（4 条独立链）：

1. **历史 log 里该警告 100% 出现**，且**含大量存活超过 30s 的 boot**:
   - `z1_boot_mainline_20260815_171058.log`: 12 次 boot，其中一次最后内核行 `[677.999] bluetooth: Unknown symbol` → boot 存活到 678s 才结束（p6 EOD 同样在每次 boot 打印）。
   - `z1_boot_mainline_20260815_200224.log` boot3: p6 EOD 后跑完 CONNSYS 5 模块 insmod + WiFi func-on + BT core insmod，全程 OK，捕获结束时仍存活（捕获跨 19 分钟）。
   - `z1_boot_emmc_full6_232325.log` / `z1_boot_mainline_20260814_210837.log`: 同样每次都打 p6 EOD，boot 正常到 shell/模块加载。
2. **截断语义无副作用**: 内核只是把分区 size 封顶到物理盘尾，不会产生 I/O 错误、挂死或复位；`/dev/mmcblk0p6` 在封顶范围内读写完全正常。
3. **复位瞬间无 eMMC 访问**: 30.55s 时板子空闲在 shell（无 mount / dd / fsck / 分区访问），p6 截断是 0.69s 时 partition scan 的一次性事件，与 30.55s 复位无时间关联。
4. **p6 EOD 与 30.55s 无因果通道**: 分区表解析在 `mmc_blk_probe` 阶段完成（0.69s），之后内核对该分区零操作。不存在"延迟 30s 触发的 eMMC 异常"。

> 结论: **p6 EOD 截断与 30.5s 重启不相关**，是长期存在的出厂表 + 正常内核截断行为。排查 30.5s 重启不应再碰 p6。

---

## 5. 重启根因推断（最可能 = WDT 饿死复位）

排除链:

- ❌ **不是 regulator late-cleanup 关 regulator**: cmdline `regulator_ignore_unused` + dts always-on（commit `37a481a21`）双保险，30.55s 的 delayed work 只打印不动作（`core.c:6843-6846`）。boot2 在该行后仍能交互。
- ❌ **不是 kernel panic**: `panic=10` 下 panic 会停 workqueue，30.5545s 的 delayed work 无法执行；且无 panic 文本、无 10s 倒计时。
- ❌ **不是 p6 EOD**（见 §4）。

指向:

- ✅ **硬件 WDT（TOPRGU/mtk_wdt）未持续喂 → 到点咬死复位**（复位形态 = 回 preloader，与"无标识复位"一致）。
- 机制（`drivers/watchdog/mtk_wdt.c`）: probe 时 `mtk_wdt_init()` 发现 WDT_MODE_EN 已置位（LK 遗留）→ `set_bit(WDOG_HW_RUNNING)` + `mtk_wdt_set_timeout(15s)`（dts `timeout-sec=<15>`，mt6572.dtsi:280-286）→ WDT 硬件开始倒数。此后**必须有人周期喂**。
- init 喂狗段（`tools/repack_ramdisk_connsys.sh:35-57` 注入）:
  ```sh
  if [ -e /dev/watchdog ]; then
    echo "[init] feeding watchdog /dev/watchdog (WDT bite prevention)"
    ($BB watchdog -t 5 -T 10 /dev/watchdog 2>/dev/null || $BB echo V > /dev/watchdog 2>/dev/null) &
  fi
  ```
  若 `busybox watchdog` applet 可用并常驻 → 每 5s 喂，永不咬。若 applet 失败 → fallback `echo V` **一次性喂** → WDT 继续倒数 → 到点复位。
- **30.55s 的确定性** = 固定计数: mtk_wdt 在 ~0.5s 处 arm（late_initcall 0.5545s 证明所有 initcall 已跑完），加 ~30s 超时（driver 默认 `WDT_MAX_TIMEOUT=31`，或 LK 遗留 30s 配置未被 15s 覆盖生效），一次喂后 ~30.55s 咬死。与历史 `BOOT_STABILITY_FIX_20260814.md` 记录的"TOPRGU WDT 饿死 ~19.7s 复位"同机制（那次 15s 超时、本次约 30s，取决于 arm 点/超时生效值）。
- **间歇性佐证**: `20260815_200224.log` boot3 存活过 30s 并跑完 CONNSYS → 同一镜像喂狗有时生效有时不生效（后台 subshell 随 init `exec getty` 的控制终端处理/竞态，或 busybox watchdog 打开节点时序）。这解释为什么部分 boot 存活、部分 30.5s 死。

> 定案需上板: 让喂狗**常驻可靠**（如 init 里 `while true; do watchdog -t 5 -T 10 /dev/watchdog; sleep 5; done &`，或先确认 `busybox watchdog` applet 确实可用且不退出），观察是否仍 ~30.5s 重启；若不再重启即坐实 WDT 根因。

---

## 6. 建议

1. **不再把 p6 EOD 当重启嫌疑**; 该行永远在，截断无副作用。
2. **修喂狗**: 改 init 为常驻循环喂（避免一次性 `echo V` fallback），并确认 busybox 含 `watchdog` applet（binary 已含 `WDIOC_SETOPTIONS`/`WDIOC_SETTIMEOUT` 字符串，大概率可用）。
3. **验证指标**: 连续 3 次 boot 存活 >60s（过 30.55s 危险点）；`dmesg | grep -c "Not disabling"` 确认 regulator 行仍在但不再复位。
4. 若喂狗常驻后仍 30.5s 死 → 抓 mtk_wdt 的 `dev_info "Watchdog enabled (timeout=…)"` 确认实际超时值，排查是否有第二路硬件 WDT/PMIC 复位（上板手段）。

---

## 7. 证据索引

- 主 log: `mainline_recon/z1_boot_mainline_20260815_204801.log`（boot1 tail @3783，boot2 tail @7245；Jump to BL @3963 / @7487）
- 对比存活 log: `mainline_recon/z1_boot_mainline_20260815_200224.log`（boot3 过 30s + CONNSYS 全过）、`mainline_recon/z1_boot_mainline_20260815_171058.log`（boot 存活 678s）
- 分区表 dump: `/home/<user>/claude/6572/1/MBR`、`/home/<user>/claude/6572/1/EBR1`、`/home/<user>/claude/6572/1/MT6572_Android_scatter.txt`
- 内核分区解析: `fork_linux/block/partitions/msdos.c`（扩展分区 `< >` 标记 + parse_extended）、`fork_linux/block/partitions/core.c:533-560`（EOD 截断分支）
- regulator delayed work: `fork_linux/drivers/regulator/core.c:6843-6846`（ignore_unused 只打印）、`:6889-6899`（30s schedule）
- WDT: `fork_linux/drivers/watchdog/mtk_wdt.c`（probe/init/set_timeout）、`fork_linux/arch/arm/boot/dts/mediatek/mt6572.dtsi:280-286`（timeout-sec=15）
- 喂狗注入: `fork_linux/../tools/repack_ramdisk_connsys.sh:35-57`；历史 WDT 记录: `mainline_recon/BOOT_STABILITY_FIX_20260814.md`
- 内核: `7.0.0-rc7-z1+ #91 SMP Fri Aug 14 01:46:36 CST 2026`（所有对比 log 同内核）
