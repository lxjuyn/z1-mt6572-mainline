# REBOOT_19S_VERDICT — 19.7s 重启最终判定（2026-08-14）

> 汇总三份只读报告：
> - `REBOOT_19S_SCENE_20260814.md`（boot log `z1_boot_mainline_20260814_213931.log` 行为分析）
> - `PM_DOMAIN_SOURCE_20260814.md`（sync_state/genpd 关电源码级分析）
> - `EMMC_TIMING_AUDIT_20260814.md`（eMMC 时序 + 39.5s regulator 重启的根因与修复记录）
> 本判定只读汇总 + 结论，未改任何文件/源码/硬件。

---

## 0. 结论速览（TL;DR）

1. **19.7s 重启不是确定性内核逻辑，也不是 sync_state/genpd 关电。** 证据：同一镜像的 boot#2 在完全相同的 19.67s 打印完全相同的 5 条 `sync_state() pending` 消息后**存活 9+ 分钟**（shell 交互 echo 全应答到 capture 结束）。同一内核代码、同一时序、一次复位一次不复位 ⇒ 触发点在两段 boot 之间变化的**硬件状态**，不在这条软件路径上。
2. **最可能根因 = 首段冷启动时 LK 武装的硬件 TOPRGU WDT 在 ~20s 触发。** 内核 mtk-wdt 两段 boot 行为完全一致（7.13s 注册、timeout=15s、nowayout=0），init 从不打开 `/dev/watchdog`；若首段 boot LK 遗留 `WDT_MODE_EN=1` 则无人喂 → ~22.1s 咬死，与首段 boot 在 19.7–22s 间复位相容；第二段 boot 因 WDT_MODE_EN 硬件状态不同未重现。**log 无法最终证实**（WDT_MODE_EN 状态未打印），需读复位原因寄存器/喂狗实验区分。
3. **与已修复的 39s regulator 重启是不同源。** 39s 是软件确定性掉电（零消费者 regulator late-cleanup 关 `vusb/vmc/vmch` → 板复位，每 boot 100% 复现，`37a481a21` always-on 已修，修后 log 0 条 `: disabling`）；19.7s 是偶发一次性复位（仅首段冷启动，不复发），复位前无任何软件掉电消息。**39s 修复不会消除 19.7s 复位。**
4. **修复建议：不要给 power domain 加 always-on（无效且有害）；优先做三类实验锁定 WDT 假设**（读 WDT_RST_STAT / 喂狗 / 拔电池复测）。可选加 `fw_devlink.sync_state=timeout` 或给 5 个无驱动 consumer 节点 `status="disabled"` 消噪，但这不是根因。

---

## 1. 19.7s 重启的确切根因（或最可能根因）

### 1.1 事实（两报告共同确认）
- `213931` log 全 log **只有 1 次真重启**：首段 boot（capture 开始时的 preloader→LK 移交不算）之后复位；随后第二段 boot 存活到 capture 结束（约 9 分钟）。
- 复位前最后内核输出是 **5 条 dev_info 级 `sync_state() pending due to ...`**（`drivers/base/core.c` `fw_devlink_dev_sync_state`，19.670879–19.721859s），无任何 panic / Oops / BUG / backtrace / WDT-reset / "Restarting system"。
- cmdline `panic=0`（内核 panic 不会 reboot）；`preloader dump` 不显示复位原因（F0/F3/V0/00/BP/G0/EC 两段相同，仅 T0 计数器 0x123→0x124 递增 = 复位发生过）。
- 内核实际复位瞬间未知（最后内核输出 19.72s，preloader banner 前有一段静默），可能在 19.7s–~22.1s 之间。

### 1.2 已排除的候选（源码级 + 实证级排除）
| 候选 | 排除依据 |
|---|---|
| **sync_state() pending 消息本身** | 是 STRICT 模式下 fw_devlink 的纯诊断 dev_info，`of_genpd_sync_state()` 永不执行，`stay_on` 永不清除，**没有任何 power domain 被软件关电**（`PM_DOMAIN_SOURCE` §2/§3）。boot#2 同刻打印同内容且存活。 |
| **genpd power-off（`genpd_power_off_unused`）** | DIS/MFG 被 `stay_on=true` 挡住不关；CONN 软件 status 本为 OFF，`genpd_power_off` 首行即 return，**scpsys_power_off（MTCMOS 关电序列）对任何域都不会被调用**。即便真关 MTCMOS 也只影响该域模块，不会复位运行中的 CPU——只有 WDT 能复位 SoC（`PM_DOMAIN_SOURCE` §3/§5）。 |
| **关 CONNSYS 域** | in-tree genpd 根本不碰 CONN 电源（`MTK_SCPD_KEEP_DEFAULT_OFF` + 软件 status OFF）；CONN 上下电由 out-of-tree .ko 决定，其软件复位走 TOPRGU SW_RST 不是 genpd 关电。 |
| **eMMC 时序** | eMMC 枚举 8.1s 完成、恒定 0.9s、0 错误、0 `-110`；复位发生在 eMMC 完成后 14s，期间 0 条 mmc 输出。历史 eMMC bootloop（CMD1 resp=0 stuck-busy）已被 BOOTRST+PB1+400kHz 三项修复根治，当前零复发。 |
| **WDT 软件侧 15s 超时** | `mediatek_disable_wdt_early()`（machine init 早停狗）已清 WDT_MODE_EN；`mtk_wdt_init` 只在 HW 已跑时才置 `WDOG_HW_RUNNING`；`watchdog_register_device` 不主动 start；boot#2 存活 9min 佐证内核侧 WDT 默认不咬人。 |

### 1.3 最可能根因（按置信度排序）
1. **硬件 TOPRGU WDT 在首段冷启动 ~20s 触发**（最可能）：首段 boot 时 LK 遗留 `WDT_MODE_EN=1`，内核 WDT 重装载 15s 从 7.13s 起算 → ~22.1s 咬死，与复位窗口相容；第二段 boot WDT_MODE_EN 硬件状态不同（未武装）故存活。此假设与所有观察相容，但**此 log 无法最终证实**（WDT_MODE_EN 未打印）。
2. **偶发硬件条件**（次可能）：两段 boot 硬件初始状态不同（boot#1 fresh 上电 eMMC 处 stuck-busy 重试多、boot#2 BOOTRST 已释放），首段 boot 可能有电源/时钟毛刺或 operator 动作触发一次性复位。
3. 二者不可简单区分——**首段 boot 的 eMMC stuck-busy 恢复 + LK WDT 遗留状态都只存在于 fresh 上电的首段 boot**，这本身是"两段 boot 硬件状态不同"的一致性解释。

### 1.4 判定
**确切根因无法从本 log 单方面坐实；最可能根因 = 首段冷启动时 LK 武装的硬件 TOPRGU WDT 在 ~20s 触发（WDT 饿死复位）。** 非内核逻辑、非 sync_state/genpd、非 eMMC、非 regulator。需 §3 的喂狗/读寄存器实验锁定。

---

## 2. 与已修复的 39s regulator 重启的关系：**不同源**

| 维度 | 39s 重启（已修） | 19.7s 重启（本次） |
|---|---|---|
| 复现性 | **确定性**：`210837` 每 boot 100% 复现 | **偶发**：仅首段 boot 1 次，boot#2 不复发 |
| 复位前最后输出 | `[39.51x] vusb: disabling` → `vmc: disabling` → `vmch: disabling` → `Jump to BL`（软件主动掉电消息） | 5 条 `sync_state() pending`（dev_info 诊断，无掉电） |
| 触发机制 | 软件：`regulator_late_cleanup` 把零消费者 regulator（eMMC 卡电 vmc/vmch + USB PHY vusb）掉电 → 板复位 | 疑似硬件：LK 武装的 TOPRGU WDT 饿死复位（未证实） |
| 修复 | `37a481a21`（2026-08-14 21:31）dts 四组 `regulator-always-on`；修后 log 0 条 `: disabling` | 未修，需实验锁定 |
| 是否同一根因 | — | **否，不同源**：触发时间、复现性、复位前软件行为、修复手段全不同 |

关键佐证：39s 修复后（`213931`）0 条 `: disabling`，但首段 boot 仍在 19.7s 复位——**39s 根因修掉了，19.7s 复位照样发生**，二者互不相关。19.7s 复位属于 `EMMC_TIMING_AUDIT` §0.5 记录的"一次性复位/0 输出启动"残余不稳定类别，与 39s regulator 完全无关。

---

## 3. 修复建议

### 3.1 锁定根因的三类实验（按优先级，互斥可并做）
1. **喂狗区分 WDT（最直接、零编译）**：在 initramfs init 脚本加 `echo V > /dev/watchdog`（打开后内核 worker 自动喂）。若 19.7s 复位消失 ⇒ 锁定"WDT 饿死复位"；若仍复位 ⇒ 是其他硬件复位。
2. **读复位原因寄存器**：在 LK 侧或内核 mtk-wdt probe 时打印 TOPRGU `WDT_RST_STAT`（或 mtkclient 读 bootreason）。显示 WDT 复位则坐实 WDT 假设。
3. **拔电池复测**：拔电池→装回→开机抓 600s。若"每次 fresh 上电的首段 boot 都在 ~20s 复位、后续 boot 存活" ⇒ 是 power-on / LK 武装 WDT 的状态问题，与内核无关。

### 3.2 不建议：dts 给 power domain 加 always-on
- **无效**：两报告已源码级证明当前配置下**没有任何域被软件关电**（STRICT 模式 `of_genpd_sync_state` 永不执行；`genpd_power_off_unused` 被 `stay_on`/软件 OFF 双重挡住；`scpsys_power_off` 对任何域都不会被调用）。加 `GENPD_FLAG_ALWAYS_ON`/dts always-on 对 19.7s 复位是无操作的。
- **有害**：DIS/MFG 域真保持 always-on 会推高功耗；CONN 域若未来要接 WiFi/BT 消费者，`MTK_SCPD_KEEP_DEFAULT_OFF` + STRICT pending 组合已使 CONN 永不自动上电，需 out-of-tree 模块显式 `pm_runtime_get_sync`（`mtk6572-connsys.c` probe 已如此），再加 always-on 会让 CONN 域状态与 KEEP_DEFAULT_OFF 语义冲突。
- **与 regulator always-on 是两码事**：39s 修的是 regulator（`mt6323.dtsi` vmc/vmch/vusb/vemc3v3），不是 power domain；power domain 无需也不应照搬。

### 3.3 可选的消噪（非根因，仅为让 log 干净）
- **cmdline**：加 `fw_devlink.sync_state=timeout`，让 5 个无驱动 consumer（smi@1000e000 / iommu@10203000 / gpu@13010000 / larb@14010000 / connectivity@18070000）的 pending 噪音在超时后清掉，便于看清真正的复位点。注意若未来接 WiFi/BT，STRICT→timeout 会让 sync_state 照常执行（此时各域已 stay_on，行为等价），仍不关域。
- **dts**：给这 5 个无驱动节点加 `status="disabled"` 也能消 pending 噪音；但 connectivity 节点是未来 WiFi/BT 的目标，disabled 需在接驱动时再开，别当默认方案。

### 3.4 不建议：改驱动
- mtk-wdt / mtk-pm-domains / mtk-sd 当前行为均无 bug；19.7s 复位不在这条路径上。等 §3.1 实验出结果再决定是否需要动 LK 或 WDT 驱动。

### 3.5 附带提醒（39s 修复的验证仍未完成）
- `37a481a21`（vmc/vmch/vusb always-on）**修后镜像已用于 213931 但上板未做长时间确认**；`EMMC_TIMING_AUDIT` §6.1 建议的"单 boot 存活 >60s + 0 条 `: disabling`"判据已由 boot#2（9 分钟）满足，可视为基本通过，建议保持此 fix 并确认 eMMC 卡电不掉（若 39s 复位复发优先查这四组 always-on 是否被后续编译覆盖）。

---

## 4. 报告状态与不确定性

- **两份指定报告均存在且完整**（`REBOOT_19S_SCENE_20260814.md` 146 行 / `PM_DOMAIN_SOURCE_20260814.md` 121 行），无缺失。附加参考 `EMMC_TIMING_AUDIT_20260814.md`（153 行）提供了 39s 根因与修复记录，本判定已纳入。
- **未消除的不确定性**（两报告一致声明）：
  1. WDT_MODE_EN 硬件状态未打印，log 无法区分"WDT 饿死复位" vs "偶发硬件复位"。
  2. 实际复位瞬间未知（19.72s 最后内核输出 ~ 22.1s 推测 WDT 触发点之间有一段静默）。
  3. preloader dump 不显示复位原因，且无 preloader 源码可进一步解码。
- 这些不确定性只能靠 §3.1 的喂狗/读寄存器/拔电池实验消除，log 层面已分析到尽头。

---
*判定基于三份只读报告汇总，未改任何文件；所有"建议"均未执行，待上板验证。*
