# Z1 mainline boot 卡死点分析 — z1_boot_mainline_20260814_213931.log

> 只读分析，2026-08-14。Log 版本: kernel `7.0.0-rc7-z1+ #91 SMP`，cmdline 含 `ignore_loglevel clk_ignore_unused rdinit=/init`，console=ttyS0 115200。
> **结论先行：本 log 中不存在任何"卡在 initcall"的 boot 段。两段 boot 全部 578 个 initcall 都 completed，均到达 `Run /init` 并出交互 shell。**

---

## 1. 结论 (TL;DR)

- 本 log 共 **2 个完整 boot 段**，每段 **578 个 initcall，578 个全部 returned，0 个未闭合**（脚本逐行校验 calling/returned 配对）。
- 每段尾部都是 `Freeing unused kernel image (initmem)` → `Run /init as init process` → `[init] welcome` → busybox 提示符 `~ #`。
- 第 2 段在 shell 里 `echo SHELLOK / SHELLOK2 / SHELLNOW` 全部回显，**shell 完全可用**。
- 任务前提"log 尾部停在某个 initcall、shell 不出来"在本 log **不成立**。疑似把 `Run /init` 后约 10 秒的**内核控制台静默窗口**（9.5s → 19.67s）误判成了"卡在 9.x 秒的 initcall"。
- 本 log 唯一的异常是：**第 1 段在到达 shell 后约 19.7s 重启**（回到 preloader banner）—— 这是"板子反复重启/看起来没起来"的真正候选根因（详见 §6），**与 initcall 无关**。

---

## 2. Boot 段清单与尾部

段边界（`F0: 1028 0000` = preloader banner；`Booting Linux` = 内核起点）：

| 段 | 行号范围 | 内核起点行 | Run /init 行 | 是否到 shell | 段尾发生了什么 |
|---|---|---|---|---|---|
| Boot #1 | 13–1419 | 23 (`[0.000000] Booting Linux`) | 1410 (`[9.504290]`) | 是 (`~ #`，1414) | 1418 后约 19.72s → 1420 行 preloader banner（重启） |
| Boot #2 | 1420–2832 | 1430 | 2817 (`[9.514422]`) | 是（shell 交互成功） | 2826–2831 echo 全部回显 → 2832 `capture_payload_end`（正常结束捕获） |

校验：段1 calling=578 returned=578；段2 calling=578 returned=578；全 log 未闭合 calling = 0。

---

## 3. 每段"尾部最后输出 + 前后 10 行"

### 3.1 Boot #1 尾部（1410–1420，最后输出行 = 1418，之后是重启 banner）

```
[    9.470285] initcall regulator_init_complete+0x0/0x5c returned 0 after 5 usecs
[    9.478827] calling  of_platform_sync_state_init+0x0/0x10 @ 1
[    9.485901] initcall of_platform_sync_state_init+0x0/0x10 returned 0 after 10 usecs
[    9.496809] Freeing unused kernel image (initmem) memory: 1024K
[    9.504290] Run /init as init process          ← 最后一个内核 initcall 之后就是它
[init] welcome to the Z1 Linux bringup initramfs
[init] uname: Linux version 7.0.0-rc7-z1+ (...)
(空行)
~ # [   19.670879] mtk-power-controller ...: sync_state() pending due to 1000e000.smi
[   19.683702] ... sync_state() pending due to 10203000.iommu
[   19.696648] ... sync_state() pending due to 13010000.gpu
[   19.708995] ... sync_state() pending due to 14010000.larb
[   19.721859] ... sync_state() pending due to 18070000.connectivity
(空行)
F0: 1028 0000                                    ← 重启（preloader 第二轮 banner）
```

### 3.2 Boot #2 尾部（2806–2832，最后输出行 = 2831）

```
[    9.474215] calling  regulator_init_complete+0x0/0x5c @ 1
[    9.480496] initcall regulator_init_complete+0x0/0x5c returned 0 after 4 usecs
[    9.489031] calling  of_platform_sync_state_init+0x0/0x10 @ 1
[    9.496102] initcall of_platform_sync_state_init+0x0/0x10 returned 0 after 9 usecs
[    9.506937] Freeing unused kernel image (initmem) memory: 1024K
[    9.514422] Run /init as init process
[init] welcome to the Z1 Linux bringup initramfs
[init] uname: Linux version 7.0.0-rc7-z1+ (...)
(空行)
~ # [   19.670895] mtk-power-controller ...: sync_state() pending due to 1000e000.smi
...（同 #1 的 5 条 sync_state，19.67–19.72s）
echo SHELLOK
SHELLOK
~ # echo SHELLOK2
SHELLOK2
~ # echo SHELLNOW
SHELLNOW
~ # capture_payload_end
```

---

## 4. 任务点名的"疑似卡死 initcall"——全部 return 0

| initcall | Boot #1 | Boot #2 |
|---|---|---|
| `of_fdt_raw_init` | `[9.331754] returned 0 after 187 usecs` | `[9.342000] returned 0 after 219 usecs` |
| `xdp_metadata_init` | `[9.371799] returned 0 after 0 usecs` | `[9.382079] returned 0 after 0 usecs` |
| `regulator_init_complete` | `[9.470285] returned 0 after 5 usecs` | `[9.480496] returned 0 after 4 usecs` |
| `clk_disable_unused` | `[9.434622] returned 0 after 5153 usecs` | `[9.444898] returned 0 after 5152 usecs` |
| `genpd_power_off_unused` | `[9.455268] returned 0 after 5994 usecs` | `[9.465494] returned 0 after 5948 usecs` |
| `of_platform_sync_state_init`（最后一个 initcall） | `[9.485901] returned 0 after 10 usecs` | `[9.496102] returned 0 after 9 usecs` |

所有都 ≤ 6ms 返回，无任何一个停在 `calling`。这些名字之所以出现在"9.x 秒"附近，纯粹是因为它们位于 initcall 表的**最末尾**（late 段尾部），并非卡死。

---

## 5. 成功 boot 段对比（到 shell）

两段均完整走完 initcall，到 `Run /init` 前最后一行 initcall 都是 `of_platform_sync_state_init ... returned 0`：

- Boot #1：最后 initcall `[9.485901]` → `Freeing` `[9.496809]` → `Run /init` `[9.504290]`
- Boot #2：最后 initcall `[9.496102]` → `Freeing` `[9.506937]` → `Run /init` `[9.514422]`

两段 initcall 顺序与耗时几乎完全一致（例：`pty_init` 两段均 ~69ms、`mtk8250_platform_driver_init` 均 ~5.44s、`deferred_probe_initcall` 均 ~68ms、`Run /init` 均 9.50/9.51s），说明 boot 流程确定性强、无随机卡点。

---

## 6. 规律 / 设备 probe 相关性分析

### 6.1 "9.x 秒像卡住"的来源 —— 不是 initcall，是 10 秒静默窗口
`Run /init`（9.50s）之后，内核控制台**静默约 10.2 秒**，直到 `[19.670879]` 才再出日志（sync_state 消息 + shell 提示符）。这个空窗 = **fw_devlink 的 deferred-probe-timeout 默认 10s**（`deferred_probe_initcall` 之后内核调度 10s 超时工作，超时后重试 deferred probe 并触发 power-controller 的 sync_state）。若在 9.x 秒处掐断捕获，尾部正好停在 `of_fdt_raw_init / xdp_metadata_init / regulator_init_complete` 一带 → 看起来就是"卡在 initcall"。

### 6.2 设备 probe 机制完全健康（deferred probe 正常完成）
- 首次 probe 返回 `-517`(EPROBE_DEFER)、随后在 `deferred_probe_initcall` 里全部 probe 成功：`13000000.syscon`(mfg clk)、`1000f000.pwrap`、`1000f000.pwrap:mt6323`、`mt6323-regulator`(Chip ID=0x2023)、`mt6323-auxadc`、`adc-battery`。
- `deferred_probe_initcall` 两段均 `returned 0 after ~68ms`，无阻塞。

### 6.3 19.67s 的 5 条 `sync_state() pending` 是**良性**的，不是卡死
`mtk-power-controller 10006000.power-controller` 因这些 DT 消费方**从未 probe**（驱动未编进内核）而无法达成 sync_state，仅打印等待：
- `1000e000.smi`(SMI)、`10203000.iommu`(IOMMU)、`13010000.gpu`(Mali GPU)、`14010000.larb`(LARB)、`18070000.connectivity`(WiFi/connsys)

结果只是这几个 power domain 保持上电，不影响启动。两段都在同一时刻打印同 5 条，行为确定。

### 6.4 真正的"反复重启/起不来"候选根因 —— WDT（与 initcall 无关）
- 段1 到 shell 后，最后输出 `[19.721859]`，随后 ~20s 处回 preloader（行 1420）。
- `mtk-wdt 10007000.watchdog: Watchdog enabled (timeout=15 sec, nowayout=0)` 在 `[7.133764]`(段1)/`[7.142472]`(段2)。
- initramfs（busybox）未发现喂狗动作；`nowayout=0` 且无人 open /dev/watchdog 时 WDT 照跑。**WDT 复位窗口（7.1s+15s≈22s）与段1 实际重启点（~19.7s）同量级**（差值可能是计数起点早于 probe 打印行，或 LK 早停狗计数）。故"到达 shell 后 ~15–20s 被 WDT 咬重启"是段1 重启的最可能解释 —— 这也是用户侧"shell 老是不在/反复重启"现象的候选根因。
- 另一种可能：段1 是操作者手动断电重开（段2 便继续交互）。本 log 无法区分，因为 `image=<not-supplied>`（capture 头没记录刷的是哪个 boot.img）。

---

## 7. 输出汇总（任务第 5 点）

| 项 | 结论 |
|---|---|
| 卡死点分布 | **本 log 无任何 initcall 卡死点**。578/578 initcall 两段全部 returned。尾部一律到 `Run /init` + shell。 |
| 是否同一 initcall | 不适用（无卡死）。若只看"9.x 秒尾部最后一个 initcall"，两段都是 `of_platform_sync_state_init`（returned 0）。 |
| 是否设备 probe 阻塞 | 不适用。deferred probe 全部成功；`sync_state() pending` 5 条为良性等待（SMI/IOMMU/GPU/LARB/connsys 驱动未编）。 |
| 真实异常 | 段1 到 shell 后 ~19.7s 被重启（WDT 嫌疑最大，timeout=15s 无人喂狗）；另有一段 9.5→19.67s 的 10s 静默（fw_devlink deferred-probe-timeout，属正常）。 |

---

## 8. 建议（不改代码，仅诊断动作）

1. **抓 log 别在 9.x 秒断流**：`z1_capture_mainline.sh 1800` 足够跨过 10s 静默窗口；等看到 `~ #` 或 shell 交互再停。
2. **消除/验证 WDT 重启**：initramfs 里 `echo V > /dev/watchdog`（或启动喂狗进程）再观察段1 是否还 ~20s 重启；若喂狗后不再重启，即证实 WDT 咬死，考虑内核 `CONFIG_WATCHDOG_NOWAYOUT=y` 或 DTS 关 mtk-wdt。
3. **若确要解释"10s 空窗"**：给 initramfs `/init` 加逐行 echo/set -x，定位 init 脚本里哪一步慢（大概率是 udev/mdev 或某个 sleep）。
4. **日后若真出现 initcall 卡死**：用本报告的校验法——某行 `calling X+0x…` 之后无对应的 `initcall X+0x… returned` = 真卡在 X；若尾部停在 `of_fdt_raw_init / xdp_metadata_init / regulator_init_complete` 且其后无 `Freeing unused kernel image` / `Run /init`，那才是卡死证据。本 log 均不满足。
5. **记录刷入的 image**：capture 头部 `image=<not-supplied>`，下次抓 log 时传入 boot.img 路径/哈希，便于把"现象↔镜像"对上。
