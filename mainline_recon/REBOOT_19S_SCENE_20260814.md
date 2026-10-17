# REBOOT_19S_SCENE — z1_boot_mainline_20260814_213931.log 只读分析

- 日期: 2026-08-14 (log: `/home/<user>/claude/6572/mainline_recon/z1_boot_mainline_20260814_213931.log`, 2837 行, capture 21:39:31→21:49:09)
- 方法: 只读 grep/sed/diff 内核时间戳与 preloader dump, 无任何改动
- 本报告只写发现, 不改文件

## 0. 结论速览 (最重要的先讲)

1. **本 log 只捕获到 1 次真正的重启, 不是多次。**
   - 全 log 只有 2 个 `Jump to BL`(行 21 与 行 1428)。行 21 是 capture 开始时的**正常 preloader→LK 移交**(非重启); 行 1428 是**第一次内核 boot 之后的重启**。第二个 Jump to BL 之后没有第三个。
   - 第一段 boot (行 24–1428): boot 到 shell (9.5s) 后, 最后内核输出为 19.67–19.72s 的 5 条 `sync_state() pending` 消息, 随后 preloader dump + `Jump to BL` = **重启**。
   - 第二段 boot (行 1431–2837): 同样的内核、同样的 19.67s `sync_state() pending` 消息, **没有重启**。shell 持续响应操作员 echo 命令 (`SHELLOK/SHELLOK2/SHELLNOW`), capture 于 21:49:09 在 `~ #` 提示符处正常结束 (约 9 分钟 uptime)。

2. **19.7s 重启不是确定性内核逻辑。** 两段 boot 内核输出逐行对比几乎完全一致 (仅 eMMC CMD1 重试次数不同, 见 §3)。第一段重启、第二段同点存活 ⇒ 触发点在"两段 boot 间发生了变化的硬件状态", 不在内核代码路径本身。

3. **重启前最后 5 行 `sync_state() pending due to ...` 是良性 dev_info, 不是重启原因。** 它们是 `drivers/base/core.c` 在"10s deferred-probe 超时"到期时的 STRICT 模式诊断信息; 第二段存活 boot 在同一时刻打印了完全相同的内容。

4. **全程无 panic / Oops / BUG / WDT-reset / backtrace / "Restarting system" 消息。** cmdline 为 `panic=0`(内核 panic 不会 reboot)。

5. **最可能根因方向: 硬件看门狗 (TOPRGU WDT, 由 LK 在首段 boot 武装) 在 ~20s 触发。** 内核侧 mtk-wdt 两段 boot 行为完全一致 (7.13s 注册, timeout=15s, nowayout=0), init 脚本从不打开 `/dev/watchdog`, 若 WDT 真在跑则无人喂 → 约 22.1s 触发, 与首段 boot 在 19.7–22s 间重启相容; 但第二段 boot 过了 22s 也没重启 ⇒ WDT 硬件使能状态 (WDT_MODE_EN) 在两段 boot 之间不同。此 log 无法最终证实, 需复测/读复位原因寄存器 (见 §6)。

## 1. 每一次 Jump to BL 的前文与时间戳

### 1.1 Jump to BL #1 — 行 21 (capture 开始的正常移交, 非重启)
行 13–21 是 preloader dump + 移交, 前面没有任何内核输出:

```
F0: 1028 0000
F3: 0000 0000
V0: 0000 0000 [0001]
00: 0000 0000
BP: 0000 0041
G0: 0090 0003
T0: 0000 0123        ← preloader 计数器, 首段 boot
EC: 0000 0180 [0000]
Jump to BL
```
随后是 LK 的二进制花屏噪声 (含 `READY`), 然后行 24 进入第一段内核 boot。

### 1.2 Jump to BL #2 — 行 1428 (第一次内核 boot 之后的重启)
前置内核输出 (行 1414–1418, 内核时间戳):

```
~ # [   19.670879] mtk-power-controller 10006000.power-controller:power-controller: sync_state() pending due to 1000e000.smi
[   19.683702] mtk-power-controller 10006000.power-controller:power-controller: sync_state() pending due to 10203000.iommu
[   19.696648] mtk-power-controller 10006000.power-controller:power-controller: sync_state() pending due to 13010000.gpu
[   19.708995] mtk-power-controller 10006000.power-controller:power-controller: sync_state() pending due to 14010000.larb
[   19.721859] mtk-power-controller 10006000.power-controller:power-controller: sync_state() pending due to 18070000.connectivity
```
随后 preloader dump (行 1420–1427) 与第二次 `Jump to BL` (行 1428):

```
F0: 1028 0000        ← 与行 13 完全相同
F3: 0000 0000        ← 与行 14 完全相同
V0: 0000 0000 [0001]
00: 0000 0000
BP: 0000 0041
G0: 0090 0003
T0: 0000 0124        ← 计数器 0x123→0x124, 每次 preloader 递增 1
EC: 0000 0180 [0000]
Jump to BL
```

### 1.3 重启触发点时间戳汇总
| 触发点 | 行号 | 最后内核输出 (时间戳) | 说明 |
|---|---|---|---|
| (正常移交) | 21 | 无内核输出 (preloader dump) | capture 开始, 非重启 |
| **重启 #1** | **1428** | **sync_state() pending @ 19.670879 … 19.721859** | **本 capture 唯一的真正重启** |
| (重启 #2) | 无 | — | **未发生**; 第二段 boot 存活到 capture 结束 |

注意: 内核最后输出是 19.721859s, 但**实际复位瞬间未知**——preloader dump 不带内核时间戳。若按内核 WDT (7.133s 武装 + 15s timeout) 推算, 复位可能在 ~22.1s 才发生, 19.72s→F0 banner 之间有一段静默 (shell 空闲无输出)。任务描述里的"19.7s"实际上是最后一条内核消息的时间, 不一定是复位瞬间。

## 2. 重启分布: initcall 阶段 vs shell 后

- **initcall 阶段 (0–9.5s): 0 次重启。** 两段 boot 的 initcall 全部正常完成 (行 582–1408 / 1989–2815), 含 `genpd_power_off_unused` (9.44s) 与 `of_platform_sync_state_init` (9.48s)。
- **shell 后 (9.5s 起): 唯一一次重启。** 首段 boot `Run /init` 于 9.504290s (行 1410), shell 提示符 `~ #` 出现, 空闲 10s 后于 19.67s 打印 sync_state, 随即复位。
- **分布结论: 100% 属于"shell 后 ~19.7s"这一类。** 且该触发点在第二段 boot 未重现。

## 3. sync_state() pending 完整上下文 (scpsys)

### 3.1 消息来源与语义 (确定是良性)
- 来源: `drivers/base/core.c` `fw_devlink_dev_sync_state()` (约行 1786), `dev_info(sup, "sync_state() pending due to %s\n", ...)` — **dev_info 级别**。
- 触发路径: `driver_deferred_probe_timeout=10` (默认值, cmdline 无 `deferred_probe_timeout=` 参数) → 超时 work 于 19.67s 触发 → `fw_devlink_drivers_done()` → 对每个尚未 ACTIVE 的 consumer link 打印上述消息。
- 时间对账: `of_platform_sync_state_init` 在 9.485901s → +10s ≈ 19.49s; 打印在 19.670879s, 差 ~0.18s 为 workqueue 调度抖动, 吻合。
- **语义**: 处于 `FW_DEVLINK_SYNC_STATE_STRICT` 模式时, supplier (mtk-power-controller = scpsys genpd provider) 的 sync_state 因仍有 consumer 未 probe 而**被推迟, 不执行**。即 `of_genpd_sync_state()` (会 power-off 未用域) 在这里**不会**运行 ⇒ 不会关任何 power domain ⇒ **无副作用, 不可能是复位来源**。
- 证据: 第二段 boot 在完全相同的 19.670895s 打印了完全相同的内容, 设备存活。

### 3.2 5 个 pending consumer 是什么 (DT 引用)
| consumer | DT 节点 | power-domain |
|---|---|---|
| 1000e000.smi | smi@1000e000 (smi_common) | MT6572_POWER_DOMAIN_DIS |
| 10203000.iommu | iommu@10203000 (M4U) | MT6572_POWER_DOMAIN_DIS |
| 13010000.gpu | gpu@13010000 | MT6572_POWER_DOMAIN_MFG |
| 14010000.larb | larb@14010000 (larb0) | MT6572_POWER_DOMAIN_DIS |
| 18070000.connectivity | connectivity@18070000 (connsys) | MT6572_POWER_DOMAIN_CONN |

这 5 个节点在当前内核里**都没有匹配的 platform driver** (两段 boot 全程无它们的 probe 输出) ⇒ devlink 永不 ACTIVE ⇒ scpsys 的 sync_state 无限期 pending。
- 实际影响 (非崩溃): DIS/MFG 域不会被 sync_state 关掉 (保持 boot 时上电状态); CONN 域有 `MTK_SCPD_KEEP_DEFAULT_OFF` (mt6572-pm-domains.h) 本就默认关。这是"未用域永不关"的功能/功耗 note, 不是复位原因。

### 3.3 scpsys/power-controller/genpd 相关错误检查
- **无错误。** `probe of 10006000.power-controller returned 19 (EPROBE_DEFER)` 是首次 probe 的正常 defer; `scpsys_pm_domain_driver_init` 后 `probe of 10006000.power-controller:power-controller returned 0` (0.171s) 成功。
- `PM: genpd: Disabling unused power domains` (9.44s) 正常执行, 无 "Failed to power off/on domain"。
- `clk: Not disabling unused clocks` (9.43s, 因 cmdline `clk_ignore_unused`) — 正常。

## 4. WDT / reset / panic / Oops / BUG 检查

- **mtk-wdt**: `mtk-wdt 10007000.watchdog: Watchdog enabled (timeout=15 sec, nowayout=0)`
  - boot1 行 1143 @ 7.133764s; boot2 行 2550 @ 7.142472s — **两段完全一致**。
  - DT `watchdog@10007000` 有 `timeout-sec = <15>`; 有中断 (GIC SPI 126) ⇒ 驱动会设 `pretimeout=15` 并注册 "wdt_bark" IRQ (两段 boot 均无 IRQ 触发打印)。
  - init 脚本 (`initramfs/root/init`) **从不打开 `/dev/watchdog`** ⇒ 内核注册的 WDT 只有两种情况:
    - a) LK 留下 `WDT_MODE_EN=1` (`mtk_wdt_init` 置 `WDOG_HW_RUNNING` 并重新装载 timeout=15s 从 7.13s 起算) → 无人喂 → **~22.1s 触发**;
    - b) LK 未武装 (`WDT_MODE_EN=0`) → 内核不主动 start → WDT 不计数, 永不触发。
- **关键矛盾**: 内核 WDT 行为两段 boot 完全一致, 若按 (a) 则第二段 boot 也应在 ~22s 复位, 但它存活 ≥9 分钟 ⇒ **要么首段 boot 时 LK 武装了 WDT 而第二段没有 (WDT 硬件状态在两次 boot 之间不同), 要么首段复位不是 WDT 而是偶发硬件条件**。log 无法区分 (WDT_MODE_EN 状态未打印)。
- **无任何 panic / Oops / BUG / backtrace / "Kernel panic" / "Restarting system" / "watchdog: did not stop" / WDT_RST_STAT / bootreason 输出。** 唯一含 "stuck" 的行是 `mtk-msdc ... Z1 eMMC released from stuck-busy; pb1=ffff0009` (boot1 行 1185 @ 7.477s) — eMMC CMD1 stuck-busy 恢复, 已成功, 与复位时间 (19.7s+) 无关。
- **preloader dump 不显示复位原因**: F0/F3/V0/00/BP/G0/EC 两段 boot 值完全相同, 仅 T0 计数器递增 (0x123→0x124)。

### 4.1 两段 boot 的其余差异 (内容级 diff)
只读 diff (剥掉时间戳与 initcall 时长) 后, 两段 boot 唯一实质差异在 mtk-msdc eMMC 枚举:
- boot1 的 eMMC CMD1 从 stuck-busy 恢复后**重试次数明显更多** (ts_ns 从 7.52s 一路到 8.19s 多轮 CMD1 才拿到 OCR=40c00000); boot2 重试次数少。这是 boot1 (fresh 上电, eMMC 处于 LK 遗留的 stuck-busy) 与 boot2 (BOOTRST 已释放 eMMC) 的硬件状态差异, **发生在 7.5–8.2s, 早于复位时间, 不是复位触发者**, 但佐证"两次 boot 的硬件初始状态不同"。

## 5. 触发点时间分布 + 每种触发点前最后输出 + 是否同一根因

### 5.1 触发点时间分布
| 阶段 | 次数 | 时间 |
|---|---|---|
| initcall 阶段 (0–9.5s) | 0 | — |
| shell 后 (~19.7s) | **1** (仅首段 boot) | 最后内核输出 19.670879–19.721859s, 复位可能在 19.7s–~22.1s 间 |
| 第二段 boot 同点 | 0 (存活) | 19.670895s 打印同样 sync_state 后继续运行, 直到 21:49:09 capture 结束 |

### 5.2 每种触发点前的最后输出
唯一触发点 (首段 boot 复位) 前最后内核输出为 5 条 `sync_state() pending due to ...` (详见 §3), 均为 dev_info 级别诊断, 无错误/警告。

### 5.3 是否同一根因
- 本 capture **只有一次重启, 没有多次可比对**, 无法直接判断"多次是否同一根因"。
- 但关键旁证: **第二段 boot 在完全相同的 19.67s 打印了完全相同的 sync_state 消息且存活** ⇒ 该消息不是根因。
- 一致性最强的假设是**硬件 WDT 在首段 boot 约 20s 触发** (首段 boot 时 LK 武装的 WDT 未被内核接管/未被喂; 第二段 boot 因硬件状态不同未重现)。此假设与所有观察相容但 log 不能证实。备选: 首段 boot 的偶发硬件条件 (如 eMMC stuck-busy 恢复后的电源/时钟毛刺)。

## 6. 建议的下游验证动作 (仅建议, 未执行)
1. **重新上电复测**: 拔电池→装回→开机抓 600s。若"每次 fresh 上电的首段 boot 都在 ~20s 复位、后续 boot 存活" ⇒ 是 power-on / LK 武装 WDT 的状态问题, 非内核逻辑。
2. **读复位原因**: 在 LK 侧或内核 mtk-wdt probe 时打印 TOPRGU `WDT_RST_STAT` (复位原因寄存器); 或 `cat /sys/firmware/...` / mtkclient 读 bootreason。若显示 WDT 复位则锁定 §5.3 的 WDT 假设。
3. **给 WDT 喂狗验证**: 在 initramfs 里 `echo V > /dev/watchdog` (打开后内核 worker 自动喂), 看 19.7s 复位是否消失 —— 直接区分"WDT 饿死复位"与"其他硬件复位"。
4. **消除 sync_state 噪音 (可选)**: 5 个无驱动 consumer (smi/iommu/gpu/larb/connsys) 导致 scpsys sync_state 无限期 pending; 若后续确认不关域, 可在 dts 给这些节点加 `status="disabled"` 或换 `fw_devlink.sync_state=timeout` 消掉 19.67s 的噪音, 便于看真正的复位点。这不是本次复位的根因, 只是让 log 更干净。

---
(本报告只读分析了 `/home/<user>/claude/6572/mainline_recon/z1_boot_mainline_20260814_213931.log`, 未改任何文件。)
