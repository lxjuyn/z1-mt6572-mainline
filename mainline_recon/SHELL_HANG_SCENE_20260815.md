# Z1 shell 卡死场景分析（2026-08-15 14:05-14:35 抓取）

> 只读分析。日志：`mainline_recon/z1_boot_mainline_20260815_140539.log`（405925 字节 / 5686 行）
> 抓取脚本：`z1_capture_mainline.sh`（被动读串口，脚本自身**不发任何命令**；命令由操作者另开终端敲入，经设备回显后被本脚本捕获）

---

## 0. 结论速览（VERDICT）

| 问题 | 结论 |
|---|---|
| log 尾部"无回显"是设备卡死吗？ | **不是。是抓取 1800s 窗口到期**。设备在 capture 结束瞬间仍活着、还在打 kernel initcall。 |
| reader_status=124 是什么？ | **抓取脚本 `timeout 1800` 正常到期**（124 是 GNU timeout 的标准返回码），不是错误。 |
| WMT_init 花了 113s？ | **误读。WMT_init initcall 只花 28.4ms**（28402 usecs）。`[113.…]` 是"内核时间轴上该命令的模块加载发生在第 113 秒"，不是 WMT_init 执行时长。 |
| 本次 113s vs 正常 ~40s 为什么慢？ | 详见 §4：shell 就绪([19.7])之后到 cfg80211 模块真正开始加载([113.1])之间有 **~93.4s 静默窗口**，期间内核无任何打印。该窗口是"操作者晚敲命令"还是"insmod 在内核侧被阻塞 ~93s"，本日志无法 100% 区分（串口静默不产生字节）；两者都不是永久卡死，shell 最终恢复并连打了 3 个模块。 |
| 疑似卡死期间设备有主动输出吗？ | **有大量**（§5）。modprobe 在最后 0.18s 连续吐出 cfg80211/BTIF/WMT 全部 initcall——设备活着、工作正常。 |

一句话：**"发命令无回显"= 抓取 1800s 窗口到期（capture_payload_end + reader_status=124），设备未卡死**；WMT_init 本身永远 ~30ms，"113s"是加载时点而非耗时。

---

## 1. 抓取元数据（证明"窗口到期"）

```
capture_start  = 2026-08-15T14:05:39+08:00
max_seconds    = 1800
capture_end    = 2026-08-15T14:35:39+08:00     ← 恰好 1800s 整
reader_status  = 124  (出现两次)
payload_bytes  = 405504
script_exit_status = 0
```

- `reader_status=124` = 抓取脚本里的 `setsid timeout --foreground 1800 tr -d '\000' < "$PORT" >&3` 在 1800s 整被 `timeout` 杀掉。124 是 GNU timeout 到期标准码。
- 出现两次是脚本正常结构：一次在主流程 `wait $CAP_PID` 后（`capture_payload_end / reader_status / payload_bytes`），一次在 EXIT trap 里（`capture_end / reader_status / script_exit_status`）。**两次都是 124 = 窗口正常走完**。
- 抓取脚本是被动只读，不注入任何命令 → 日志里的 `echo SHELLOK` 等全是操作者通过另一个终端敲入、被设备 shell 回显到串口的字节。

---

## 2. 时间线总览（本次抓取 = 4 次完整内核 boot）

| Boot | "Booting Linux" 字节 | Run/init 内核时间 | shell 就绪内核时间 | 操作者命令 | 结果 |
|---|---|---|---|---|---|
| 1 | 2409 | [9.504] | ~[19.7] | 无 | shell 就绪后设备被重启（无命令） |
| 2 | 103333 | [9.515] | ~[19.7] | `echo SHELLOK`, `echo SHELLOK2` | 均即时回显，随后设备被重启 |
| 3 | 204328 | [9.513] | ~[19.7] | `echo SHELLOK3`, `echo SHELLOK4` | 均即时回显，随后设备被重启 |
| 4 | 305397 | [9.518] | ~[19.7] | `echo SHELLOK`, `sh /root/connsys/z1_modprobe_connsys.sh` | SHELLOK 回显；modprobe 跑到第 4 个 insmod 时 capture 到期 |

每次 boot 内核启动到 shell 都是稳定的 ~19.7s（Run/init [9.5] + initramfs getty 起来 + [19.67] sync_state 打印）。boot1-3 之间无 panic/无 WDT 报错，设备是被重启（操作者在多次迭代测试）。

---

## 3. 每次 shell 响应 / 无回显的时间戳（点 1）

日志行无墙钟时间戳，只有 kernel printk 时间戳 + 字节偏移。以下用"内核相对时间 + 字节偏移 +（boot4 的）推断墙钟"表达。

### boot 2（字节 202176-202224）
- `echo SHELLOK` 键入：字节 202176 → `SHELLOK` 回显：字节 202190（差 14B ≈ 1.5ms 串流）
- `echo SHELLOK2` 键入：字节 202205 → `SHELLOK2` 回显：字节 202224
- 均在 shell 就绪([19.7])后立即完成；之后无新内核打印即进入 preloader（重启）。

### boot 3（字节 303165-303215）
- `echo SHELLOK3` → `SHELLOK3`：303165→303180
- `echo SHELLOK4` → `SHELLOK4`：303196→303215
- 同上，全部即时回显。

### boot 4（字节 404242-405789）——本次分析重点
| 事件 | 字节 | 内核时间 | 推断墙钟* |
|---|---|---|---|
| sync_state 打印（5 条，最后一条） | 404111 | [19.721866] | ≈14:34:05 |
| shell 回显 `~ # ` prompt | ~404234 | — | ≈14:34:05 |
| 操作者键入 `echo SHELLOK` | 404242 | — | ≈14:34:05 |
| shell 回显 `SHELLOK` | 404256 | — | ≈14:34:05 |
| 操作者键入 `oo`（回显）+ 新 prompt | 404266-404276 | — | ≈14:34:05 |
| 操作者键入 `sh /root/connsys/z1_modprobe_connsys.sh` | 404277 | — | ≈14:34:05 或 ≈14:35:38（见 §4） |
| 脚本 `[connsys] 模块目录` / `加载 cfg80211...` | 404318 / 404357 | — | 同上 |
| **cfg80211 模块开始加载** | 404387 | **[113.135246]** | ≈14:35:38.8 |
| BTIF insmod | 404862 | [113.193944] | ≈14:35:39 |
| **WMT_init 调用** | 405337 | **[113.259502]** | ≈14:35:39 |
| 第 4 个 insmod 的内核打印（被截断 "ca…"） | 405772 | [113.309434] | ≈14:35:39 |
| **capture_payload_end** | 405789 | — | 14:35:39 |

\* 推断墙钟：boot4 最后一条内核打印 [113.309] 被截断于 capture 结束时刻 → 设 14:35:39 ≈ boot4 [113.31]，倒推 boot4 起点 ≈ 14:33:46，shell 就绪 ≈ 14:34:05。

**无回显统计**：本次抓取中**没有任何"shell 就绪后命令无回显"的事件**。全部命令（boot2/3/4 共 7 条）都即时回显或产生输出。唯一的"无回显"时段 = boot1 之后到 boot2 shell 之间 / boot2→boot3 / boot3→boot4 的设备重启期间（操作者若在那时敲命令自然无回显），以及 boot4 modprobe 阶段的静默窗口（见 §4）。

---

## 4. WMT_init "113s" 之谜（点 3）

### 4.1 WMT_init 实际执行时长 = 28.4ms
```
[113.259502] calling  WMT_init+0x0/0x2fc [mtk_stp_wmt_soc] @ 66
[113.295401] initcall WMT_init+0x0/0x2fc [mtk_stp_wmt_soc] returned 0 after 28402 usecs
```
跨 5 份日志 WMT_init 均为 **28.5~30.6ms**（124449: 28569/29224，145055: 30631，152956: 30541，0815: 28402）。**"113s"绝不是 WMT_init 执行时间**。

### 4.2 "113s" = cfg80211 模块开始加载的内核时点，跨会话在 23.7~113.1s 之间剧烈漂移
各日志 cfg80211 开始加载的内核时点：`23.74 / 35.79 / 44.86 / 48.16 / 53.59 / 53.85 / 56.36 / 94.02 / 113.14`。
→ 所谓"正常 ~40s"只是常见区间中的一个典型值，不是固定内核开销。加载时点 = "操作者何时敲 modprobe" + "insmod 前的任何内核侧延迟"。

### 4.3 本次 boot4 的关键：shell 就绪([19.7])到 cfg80211 加载([113.1])之间有 ~93.4s 静默窗口
- [19.72]（sync_state 最后一条）到 [113.135]（cfg80211 第一条打印）之间，**内核零打印**，只夹着 276 字节的 shell/命令/脚本输出。
- 字节偏移与内核时间戳的关系表明这 276 字节在 [19.72]~[113.1] 之间被写入（静默不产生字节，无法仅靠文件位置判断 93.4s 落在"命令被敲之前"还是"insmod 执行之中"）。
- **两个候选解释，本日志无法 100% 区分**：
  - **A（操作者晚敲）**：操作者在 14:34:05 敲完 SHELLOK 后，等到 ~14:35:38（窗口最后一两秒）才敲 modprobe，随后 capture 到期。→ 无内核问题，纯操作节奏。
  - **B（insmod 内核侧阻塞 ~93s）**：操作者在 [19.7] 立即敲了 modprobe（紧跟在 SHELLOK / "oo" 探测之后，符合其在 boot2/3 连续敲命令的风格），`insmod cfg80211.ko` 的 `init_module` 在内核里被阻塞了 ~93s，[113.13] 才真正开始加载。→ 这是真实的模块加载延迟，但**依然不是永久卡死**（恢复后 3 个模块连续加载成功）。
- 两解释都不冲突的事实：① 恢复后 cfg80211/BTIF/WMT 三个 insmod 各只花 18.5/31.6/28.4ms，无整体内核变慢；② 全程无 WDT 复位（内核可调度、喂狗进程在跑）；③ modprobe 可见输出全部挤在最后 0.18s（[113.13]→[113.31]）。
- **佐证"命令在 shell 就绪后立即敲"的一面**：字节 404111([19.72])→404277(modprobe) 仅 166B，按当时 ~9.6KB/s 实时串流速率 ≈ 17ms；shell 回显是逐字符实时的，操作者在 boot2/3 也是连续敲。**佐证"操作者晚敲"的一面**：无法排除操作者在两处之间停手。

### 4.4 "WMT_init 前 60s 内核在做什么"——直接回答
**[53.1]~[113.1]（WMT_init 前 60s）内核零输出、零可观测活动**。排除项：
- **不是 printk 缓冲**：无任何积压打印被排空（若有积压，会以 ~9.6KB/s 持续吐到串口并被捕获；实测静默）。
- **不是 regulator_ignore_unused**：`regulator_init_complete` [9.478] 已完成；`PM: genpd: Disabling unused power domains` [9.463] 已跑；[19.67] 的 sync_state "pending" 只是 power-controller 等待 consys 消费者 probe（保持供电域常开），属正常惰性行为，不阻塞 CPU。
- 内核在静默窗口内无打印 = 要么空闲（等待 insmod 锁/输入），要么 insmod 在内核侧等待（场景 B）。cmdline 含 `clk_ignore_unused`，不含 `regulator_ignore_unused`。

---

## 5. 疑似卡死期间的设备主动输出（点 4）

capture 尾部（[113.135]→[113.309]，即操作者认为"没反应"的那一段）设备**主动连续输出**，全部非新命令触发：

```
[113.135] cfg80211: loading out-of-tree module taints kernel.
[113.145] calling  cfg80211_init … 
[113.154] probe of regulatory returned 0 after 71 usecs
[113.160] faux_driver regulatory: Direct firmware load for regulatory.db failed with error -2
[113.171] cfg80211: failed to load regulatory.db
[113.171] initcall cfg80211_init returned 0 after 18452 usecs
[113.194] calling  BTIF_init … / [113.201][113.210] MTK-BTIF[E]hal_btif_clk_get_and_prepare…
[113.220] probe of 1100e000.btif returned 0 after 18196 usecs
[113.233] initcall BTIF_init returned 0 after 31565 usecs
[113.259] calling  WMT_init …
[113.269] [WMT-CONSYS-HW][E]mtk_wmt_probe(114):Wmt cannot find pinctrl!
[113.278] [WMT-CONSYS-HW][W]mtk_wmt_probe:no consys pinctrl (ok on mt6572), ret=-19
[113.287] probe of 18070000.connectivity returned 0 after 18216 usecs
[113.295] initcall WMT_init returned 0 after 28402 usecs
[113.309] （第 4 个 insmod mtk_stp_bt_soc 的内核打印，被 capture 截断 "ca…"）
```
- 一个"卡死"的设备不可能连续打印 14+ 条 kernel initcall / probe 消息。
- capture 结束瞬间（[113.309] "ca…"）内核正发出新 initcall 打印 → **设备在 capture 到期时仍活着且在工作**。
- modprobe 脚本 `z1_modprobe_connsys.sh` 加载顺序 = cfg80211 → btif → wmt → bt → wifi；日志在**第 4 个 insmod（mtk_stp_bt_soc）进行中**被窗口截断，即脚本只是"没跑完"，不是"卡住"。

---

## 6. 证据表

| # | 证据 | 出处 |
|---|---|---|
| 1 | capture_start 14:05:39 / capture_end 14:35:39 恰差 1800s；reader_status=124 两次 | log L1,L3,L5682,L5684-L5685 |
| 2 | capture 结束瞬间有内核打印 `[113.309434] ca…` 被截断 | log L5681（原始字节：`[connsys] insmod mtk_s[  113.309434] cacapture_payload_end`） |
| 3 | 全部 7 条操作命令均即时回显 | L2828-2832, L4242-4247, L5655-5656 |
| 4 | WMT_init returned 0 after 28402 usecs（28.4ms） | L5679 |
| 5 | cfg80211 加载时点跨日志 23.74~113.14s 漂移 | 5 份 z1_boot_mainline_2026081*.log |
| 6 | [19.72]→[113.13] 之间内核零打印（~93.4s 静默窗口） | L5654 之后直接 L5661 |
| 7 | WMT_init 前 60s 无 regulator/printk/genpd 活动 | L5635-5661 |
| 8 | modprobe 在最后 0.18s 连打 3 模块成功 | L5661-5680 |

---

## 7. 建议（如需 100% 定案 93.4s 窗口归属）

1. **抓取侧加墙钟**：`z1_capture_mainline.sh` 的 `tr` 流每行前缀墙钟（或用 `ts` 命令），即可精确区分"操作者晚敲" vs "insmod 阻塞"。
2. **命令侧打时间戳**：操作者敲 modprobe 前先 `date`（busybox date 可用），让设备自己在串口上盖时间戳，直接落在抓取日志里。
3. **复测**：shell 就绪后立即敲 modprobe（紧跟 SHELLOK 不等待），若 cfg80211 仍延迟 ~90s 则确认 insmod 内核侧阻塞；若立即加载则确认本次是操作节奏。
4. 若确认是 insmod 阻塞：方向是查 `finit_module`/`kernel_read_file` 路径（cfg80211.ko 491KB 来自 initramfs RAM，读取应为瞬时；无 MODULE_SIG/MODVERSIONS，排除签名/CRC；留意是否有 request_module/usermodehelper 竞争或 module_mutex 等待）。
