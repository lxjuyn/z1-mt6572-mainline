# WMT insmod 慢到 113s — log 时序分析 (2026-08-15)

分析对象（只读）:
- 慢 log: `mainline_recon/z1_boot_mainline_20260815_140539.log`(1800s 捕获, 14:05:39→14:35:39, 内含 4 次 boot; 第 4 次 boot 跑 connsys 脚本)
- 成功对照: `z1_boot_mainline_20260814_124449.log` / `_141135.log` / `_202853.log` / `_145055.log` / `_210837.log`
- 内核: `7.0.0-rc7-z1+ #91 SMP Fri Aug 14 01:46:36`(各 log 同一内核)

---

## 0. TL;DR 结论

1. **WMT_init 本身完全不慢**: 113.259502s calling → 113.295401s returned, **35.9ms**。历次 log 全是 36~38ms, 零回归。
2. **不是 getty/shell 启动慢**: shell 提示符 ~9.5s 就绪(210837 log 证明 ~9.5s 即可即时交互), boot4 里 `echo SHELLOK` 立即回显。shell 早于 113s 很久就绪。
3. **不是内核在忙 / 不是 printk 缓冲**: boot4 内核时间线 **19.72s → 113.13s 之间有 93.4s 纯静默(0 条 printk)**, 是整段 boot 唯一异常空隙。内核空闲, 无任何可归因活动。
4. **connsys 脚本一旦开始执行就很快**: cfg80211(113.135) → btif(113.194) → WMT(113.260) → 下一模块(113.309), 全部挤在 **~175ms** 内, 脚本内无停顿。
5. **因此 113s 是"connsys 命令被实际执行"的时刻, 不是驱动慢**。慢点在命令分发前那 ~93s 空等。最可能操作侧原因: **39.5s 稳定性标记(`vusb/vmc/vmch: disabling`)被 commit `37a481a21`(2026-08-14, dts 改 regulator-always-on)消除**, 操作者/流程失去"系统已稳定"门控信号, 等到 ~113s 才发命令(详见 §7)。
6. **"WMT_init 后 shell 疑似卡死"在本 log 内是捕获超时假象**: 捕获跑满 1800s 在 boot4 的 113.3s 处恰好到点被 `capture_payload_end` 切断(reader_status=124)。`calling BT_init`(113.309)之后只剩 ~0.01s 捕获时间, **无法判定 mtk_stp_bt_soc 是否挂死**——不是可确认的回归(详见 §8)。

---

## 1. boot 结构与 boot4 定位

一次捕获 4 次 boot(`Booting Linux` 出现于行 23/1431/2845/4258), 前 3 次只做 shell echo 测试后重启, **第 4 次(行 4258~5686)才跑 connsys**。

| boot | 范围 | 最后内核时间戳 | shell 交互 |
|---|---|---|---|
| #1 | 行 23~1430 | 19.7s sync_state | 无(直接重启) |
| #2 | 行 1431~2844 | 19.7s sync_state | `echo SHELLOK`/`SHELLOK2` |
| #3 | 行 2845~4257 | 19.7s sync_state | `echo SHELLOK3`/`SHELLOK4` |
| **#4** | **行 4258~5686** | **113.3s** | `echo SHELLOK` → `sh /root/connsys/z1_modprobe_connsys.sh` |

注意: 4 个 boot 里 **全部 0 条 `vusb/vmc/vmch: disabling`**(20260814 的 boot 则每次都有, 见 §7)——这是与成功 log 最显著的差异。

---

## 2. boot4 精确时间线(WMT_init 定位)

| 内核时间 | 事件 | log 行 |
|---|---|---|
| 0.000000 | `Linux version #91` | 4259 |
| 9.517972 | `Run /init as init process` | 5644 |
| ~9.5 | `[init] welcome/uname/feeding watchdog` + `~ #` 提示符 | 5645-5649 |
| 19.670875~19.721866 | `mtk-power-controller sync_state() pending` ×5 | **boot4 最后一条内核输出** |
| (19.7~113.1 之间) | `echo SHELLOK` → `SHELLOK` → `~ #`; `sh /root/connsys/z1_modprobe_connsys.sh` → 脚本 echo `模块目录`/`加载 cfg80211...` | 5655-5666 |
| **113.135246** | `cfg80211: loading out-of-tree module` | **insmod cfg80211 进内核 module loader** |
| 113.171124 | cfg80211_init returned(18.5ms) | 5672 |
| 113.193944 | insmod btif → BTIF_init 113.194~113.233(31.6ms) | 5673 |
| **113.259502** | **`calling WMT_init [mtk_stp_wmt_soc] @ 66`** | 5675 |
| 113.269200 / 113.277980 | `Wmt cannot find pinctrl` E / `no consys pinctrl (ok on mt6572)` W | 已知正常 |
| 113.287294 | probe 18070000.connectivity returned(18.2ms) | 5678 |
| **113.295401** | **WMT_init returned 0 after 28402 usecs** | 5679 |
| 113.309434 | `calling BT_init... [mtk_stp_bt_soc]`(下一模块开始打印) | 5681 → 被 `capture_payload_end` 切断 |

**WMT_init 耗时** = 113.295401 − 113.259502 = **35.9ms**(initcall_debug 的 `28402 usecs` 用不同时钟基准)。历次对比: 36.1 / 38.8 / 37.2 / 38.1 / 35.9ms → 完全一致。

**boot4 内核时间戳空隙分析**(唯一异常):
```
gap=93.413s  prev=19.722  cur=113.135   ← 唯一大空隙(本报告主角)
gap=10.153s  prev=9.518   cur=19.671    ← 正常: init→shell→sync_state
gap=4.666s   prev=1.078   cur=5.744     ← 正常: 早期 mmc/initramfs
```

---

## 3. WMT_init 本身不慢(排除驱动回归)

- 5 个 log 的 WMT_init 耗时全部 35.9~38.8ms, 与本次是否慢完全无关。
- `probe of 18070000.connectivity returned 0 after 18216 usecs`(18.2ms)也历次一致。
- WMT 的 `cannot find pinctrl` / `no consys pinctrl (ok on mt6572)` 是已知正常路径(Z1 无 consys pinctrl), 非错误。

---

## 4. 关键: 19.7s→113.1s 之间内核在干嘛?——什么都没干(93.4s 静默)

- boot4 从 19.722s(sync_state)到 113.135s(cfg80211)之间 **0 条 printk**。整段只有这一个 93.4s 空洞。
- 无 printk 洪流、无 deferred probe、无 initcall/驱动/regulator 活动、无 panic 痕迹。内核处于完全空闲。
- 因此这 93s **不是内核在工作**; 也不是 printk 缓冲堆积(没有可缓冲的内容)。cmdline 是 `console=tty0 console=ttyS0,115200n8 root=/dev/ram panic=0 ignore_loglevel clk_ignore_unused rdinit=/init`——**没有 `regulator_ignore_unused`**, 排除该配置影响(§7 的机制是另一个)。
- 93s 只可能发生在"命令分发"层: 命令 ~113s 才被 shell 执行, 前面是空等或输入滞留。

---

## 5. 与成功 log 的 insmod 时序对比

| log | shell 就绪 | 脚本前最后内核输出 | cfg80211 insmod | WMT_init calling | WMT 耗时 |
|---|---|---|---|---|---|
| 124449 boot#1 | ~9.5s | 19.7s sync_state | **23.731s** | 23.850s | 36.1ms |
| 124449 boot#2 | ~9.5s | 39.5s `vusb: disabling` | **56.350s** | 56.472s | 36.1ms |
| 141135 | ~9.5s | 39.5s `vusb: disabling` | **44.845s** | 44.965s | 38.8ms |
| 202853 boot#2 | ~9.5s | 39.5s `vusb: disabling` | **53.836s** | 53.963s | 37.2ms |
| 145055 | ~9.5s | 39.5s `vusb: disabling` | **94.009s**(也慢) | 94.126s | 38.1ms |
| **140539 boot#4** | **~9.5s** | **19.7s sync_state(无 39.5s 标记)** | **113.135s**(最慢) | 113.260s | 35.9ms |

规律:
1. **WMT_init 耗时恒定 ~36ms**, 与快慢无关。
2. cfg80211→WMT 的脚本内间隔恒定 ~125ms, 脚本执行本身从不慢。
3. 历次差异只在 **"最后内核输出"→"cfg80211 insmod"的空隙**: 成功 boot 为 4~34s(操作者正常手速), 慢的 boot(145055 / 140539 boot4)为 54s / 93s。
4. 20260814 每次 boot 都有 39.5s 的 `vusb/vmc/vmch: disabling`; 20260815 四次 boot 全部 0 条(§7)。
5. 210837 log 佐证: 正常时 shell ~9.5s 就能即时交互(`echo SHELLOK7` 在 19.7s sync_state 之前就成功返回)——shell 从来不是瓶颈。

---

## 6. 判断: shell 启动慢 vs insmod 命令排队慢?

**两者都不是内核/驱动侧问题。**

- **shell 启动慢 —— 排除**: 所有 boot(含 boot4)的 `~ #` 都在 ~9.5s 出现; 210837 证明 ~9.5s 即可交互。
- **insmod 命令排队慢(内核侧) —— 排除**: 脚本一旦执行 175ms 全跑完; 内核 19.7~113.1s 完全空闲, 无排队。
- **真实情况**: connsys 命令 ~113s 才被 shell 执行。日志里 93s 窗口内没有任何内核时间戳可锚定操作者敲击时刻, 纯文本 UART log 上**无法区分"命令 ~113s 才敲"(H1)与"命令早敲了、被 console/tty 输入路径憋到 113s 才执行"(H2)**——两种字节形态完全相同。
  - 支持 H2 的证据: 任务前提"insmod 脚本在更早发起"若属实。
  - 支持 H1 的证据: 所有其它 log 的 shell 均即时响应, 无"命令被憋 93s"先例; boot4 日志顺序 `echo SHELLOK`→`SHELLOK`→`~ #`→`sh ...` 说明 shell 先执行完 `echo SHELLOK` 并打印新提示符、操作者随后才敲脚本, 即**无论哪条路径, 脚本实际执行都发生在 ~113s**。
- **无论 H1/H2, 结论一致**: 延迟在命令分发前的 ~93s 空等, 不在内核/WMT/脚本内。

---

## 7. 最可能操作侧根因: 39.5s 稳定性标记消失(commit 37a481a21)

机制(已核对源码):
- `regulator_init_complete`(late_initcall @ ~9.5s)在 `drivers/regulator/core.c:6894-6895` `schedule_delayed_work(..., 30000ms)` → ~39.5s 执行 `regulator_late_cleanup`, 对"启用且零消费者"的 vusb/vmc/vmch 打印 `vusb: disabling` / `vmc: disabling` / `vmch: disabling`(三个时间戳 39.511/39.514/39.517 是同一 delayed work 依次遍历)。
- commit `37a481a21`(2026-08-14 21:31 "fix: keep vmc/vmch/vusb always-on - stop ~39s preloader reboot loop")把 `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts:318-341` 的 vmc/vmch/vusb 全标成 `regulator-always-on` → `regulator_late_cleanup`(core.c:6811)直接 skip → **39.5s 标记 0 条输出**。commit message 明确: 之前该晚清理会在 ~39.5s 关掉零消费者的 vmc/vmch/vusb → 板子 ~39s 复位循环; always-on 后不再复位。
- 佐证: `BOOT_STABILITY_FIX_20260814.md` 的验收标准就是"`vusb/vmc/vmch: disabling` = 0 条 + 单 boot 存活 >60s"——团队自己拿这个标记当稳定性判据。修复后"正向信号"消失。

推断(操作侧, 非内核问题):
- 20260814 操作者/流程习惯**看到 39.5s 标记 = 系统已稳定过危险点 → 之后发 connsys 命令**(WMT 落点 45/54/56s; 124449 boot#1 在 19.7s 就发了, 属快路径)。
- 20260815 该标记消失, 操作者失去"稳定"信号, 直到 ~113s 才发命令 → WMT 落在 113.3s。
- 注意: 标记消失后各 boot 的等待时长并不固定(本 log 是 93s), 说明这是操作者的判断/超时逻辑, 不是内核定时。

> 次要可能(无法完全排除, 概率低): 命令早敲但 115200 无流控 + 偶发 RX overrun 丢字, 操作者 ~113s 重发。log 上无法与"命令就是 113s 发出的"区分(只在命令成功送达那次出现回显)。19.7~113s 无内核刷屏, printk 饿死 RX 不成立。若要实锤看 `/proc/tty/driver/serial` 的 `oe:` overrun 计数(上板手段)。

---

## 8. 附带: "WMT_init 后 shell 卡死" = 捕获超时假象(非可确认回归)

- log 最后一行 `[connsys] insmod mtk_s[  113.309434] ca` + `capture_payload_end` / `reader_status=124`。
- `reader_status=124` = `timeout` 到点; 捕获 max_seconds=1800, 14:05:39 起 → **14:35:39 恰好到点**。
- boot4 的内核时间 113.3s 恰好在捕获的最后 ~0.1s 内走到(捕获结束 ≈ boot4 第 113.3s), 所以 **`calling BT_init`(113.309)之后只有约 0.01s 捕获时间**, log 立刻被切断。
- 结论:**本 log 无法判定 mtk_stp_bt_soc 的 BT_init 是否挂死**——它只是刚开始打印就被捕获超时切掉了。这不是已证实的回归。
- 若操作者在 live 终端上确实看到 WMT_init 后无后续输出(超出捕获窗口), 那是另一个独立现象, 需**重启一次更长窗口的捕获**单独验证; 与本次 113s 延迟是两回事。20260814 同模块 8.5ms 正常加载(56.523→56.539), 是当前唯一正常参照。

---

## 9. 建议(只读结论之外)

1. **改操作门控**: 不再用"`vusb/vmc/vmch: disabling`"当 boot 完成/稳定信号(该信号已被 37a481a21 永久消除)。替代: 检测 shell `~ #` 提示符 + 固定等待 >40s(过 39.5s 危险点)后即发 connsys 命令, 可回到 ~60s 内完成 WMT。
2. **脚本首行打 uptime(一锤定音)**: `z1_modprobe_connsys.sh` 第一行加
   `echo "CONNSYS_START uptime=$(busybox cut -d' ' -f1 /proc/uptime)"`, 直接看到脚本实际执行秒数; 或交互前 `export PS1='\w# $(busybox cut -d" " -f1 /proc/uptime)> '` 让提示符显示 uptime。
3. **确认操作口**: init 同时拉了 ttyGS0(USB ACM)getty 与 console getty。若平时走 ttyGS0, UART log 只反映 ttyS0 console, 93s 静默可能是操作者在另一接口活动而 UART 无输出。
4. **如需实锤输入丢字**: 上板 `cat /proc/tty/driver/serial` 看 ttyS0 `oe:` overrun; `dmesg -n 1` 压 console 后重跑对比。
5. **BT_init 挂死(如确需单独验证)**: 重启捕获(加长 max_seconds)看 WMT 之后脚本是否走完 `OK: mtk_stp_bt_soc.ko`; 若仍挂, 抓 `cat /proc/<pid>/stack` 或 ftrace 定位 BT_init 卡点, 并对比 20260814 正常模块集与本次的差异。**不要在本 log 上断言挂死**。

---

## 10. 证据文件索引

- 慢 log: `mainline_recon/z1_boot_mainline_20260815_140539.log`(boot4 行 5655-5681; 捕获头尾行 1-14 / 5682-5688)
- 成功 log: `mainline_recon/z1_boot_mainline_20260814_124449.log`(行 1431-1454 boot#1, 3684-3705 boot#2)、`_141135.log`、`_202853.log`、`_145055.log`、`_210837.log`
- dts always-on: `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts:313-342`
- regulator 晚清理机制: `fork_linux/drivers/regulator/core.c:6788-6899`(`regulator_late_cleanup` skip always-on at 6811; 30s delayed work at 6894-6895)
- 修复 commit: `37a481a21`(2026-08-14 21:31:35 "fix: keep vmc/vmch/vusb always-on - stop ~39s preloader reboot loop")
- 验收标准: `mainline_recon/BOOT_STABILITY_FIX_20260814.md`、`BOOT_STABILITY_PLAN_20260814.md`
- 相关既有调研: `mainline_recon/SERIAL_SHELL_WEB_20260815.md`(针对"WMT 后 shell 无回显"的 printk/console 假设——本 log 无 printk 洪流, 该假设不适用)
