# CONNSYS func-on 修复方案评估 — A/B/C/D（只读，2026-08-14）

> 板子关机，纯离线源码评估。对象：`/tmp/connsys_build/conn_soc/`（实际加载的
> `mtk_stp_wmt_soc.ko` 源码）+ `/tmp/connsys_build/tools/launcher/stp_uart_launcher.c`
> + 上板脚本。未改任何文件。行号以当前树为准（与 WMT_HIFCONF_REGRESSION_FIX /
> LAUNCHER_TIMING / HW_CHECK_FAIL_ROOTCAUSE / STP_FMODE_SOURCE 四份报告一致）。
>
> 一句话结论：
> **方案A（删回归行）是必须且已落树，但它只管"launcher 正常且不竞态"；它救不了
> launcher 缺席、也救不了 func-on 抢先于 SET_STP_MODE 的窗口。方案C（内核加载期
> 默认 BTIF：wmtInfoBit+hifType 默认 + g_stp_if_type 默认）把 STP 配置层完全从
> launcher 时序解耦，是把稳定性做扎实的推荐主改。方案D（脚本等 launcher 就绪再
> func-on）是零驱动风险的互补，负责自动化开机链。方案B 单独不足、且被 A+C 覆盖，
> 只作纯兜底可选。推荐组合：A + C + D。**

---

## 0. 基线事实（已逐行核实，全部基于当前树）

| 事实 | 位置 |
|---|---|
| FUNC_ON/FUNC_OFF op 构造：`pOp->op.opId=opId; au4OpData[0]=type;` **不设 u4InfoBit**（回归行已删，注释 L100-110 说明原因）；`wmt_lib_get_free_op()` memset 清零整个 op → `u4InfoBit` 恒 0 | `linux/pri/wmt_exp.c:84-139`；`core/wmt_lib.c:982-993` |
| `opfunc_pwr_on()` 仅在 `pWmtOp->u4InfoBit & WMT_OP_HIF_BIT` 时调 `opfunc_hif_conf()` | `core/wmt_core.c:897-898` |
| `opfunc_hif_conf()` 无条件 `memcpy(&wmtHifConf, &au4OpData[0], sizeof(WMT_HIF_CONF))`，会把 `hifType` 写成 op 的 `au4OpData[0]`(=驱动类型 3=WIFI/6=LPBK/0=BT) | `core/wmt_core.c:862-880`；`WMT_HIF_BTIF=2` `core/include/wmt_core.h:100-105` |
| STP 模式唯一写点（整值覆盖 f_mode）：`mtk_wcn_stp_set_mode()` | `core/stp_core.c:2824-2829`；f_mode 在 STP_OPEN 时清零 `stp_core.c:1230` |
| `wmt_core_stp_init()`：L647 先查 `wmtInfoBit & WMT_OP_HIF_BIT`（缺→"no hif info"+`osal_assert(0)`+return -1）；L669-673 仅当 `hifType==BTIF(2)` 才发 `MTKSTP_BTIF_MAND_MODE`；L684 才 `hw_check` | `core/wmt_core.c:636-714` |
| `hw_check` 的 REG_CMD 走 `mtk_wcn_stp_send_data()`，只有 f_mode&(MAND\|SDIO) 或 f_mode&FULL 才有分支发数据，**f_mode=0 时静默 return 0** → "Tx REG_CMD fail" | `core/stp_core.c:2407-2646`（MAND 分支 L2522，FULL 分支 L2558）；`core/wmt_core.c:781-860` |
| `g_stp_if_type` 静态初值 `STP_MAX_IF_TX`，**唯一改写点是 launcher 的 SET_STP_MODE → `wmt_lib_set_hif()`**；`mtk_wcn_sys_if_tx()` 按 g_stp_if_type 分发，`STP_MAX_IF_TX` 时 return -1 | `linux/pri/stp_exp.c:56/70/94-105/164-174`；`core/wmt_lib.c:393-429` |
| SET_STP_MODE ioctl 构造 HIF_CONF op（`u4InfoBit=WMT_OP_HIF_BIT`，`pSignal->timeoutValue=0`）→ **fire-and-forget**：`wmt_lib_put_act_op()` 在 `timeoutValue==0` 时入队即返回，不等 wmtd 处理完；wmtd 线程 FIFO 顺序处理 | `linux/pri/wmt_dev.c:1875-1909`；`core/wmt_lib.c:995-1074`（L1038）；`core/wmt_lib.c:828-905` |
| launcher SOC 流：`gStpMode=STP_BTIF_FULL(3)` 默认，`-m 3` 覆盖；`SET_PATCH_NAME` → `SET_STP_MODE 0x23`（fm=2<<4 \| 3）；随后 pwr-on 线程连发 LPBK func-on 做首次上电+patch 下载；主线程常驻 poll /dev/stpwmt 应答 `srh_patch` | `tools/launcher/stp_uart_launcher.c:1436-1467 / 1338-1379 / 1676-1735 / 498-720` |
| patch 元数据只有 launcher 能喂：`srh_patch` 经 `wmt_ctrl_ul_cmd` 等 **2s** 超时；launcher 缺席 → `wmt_ctrl_patch_search` -1 → `mtk_wcn_soc_patch_info_prepare` -1 → `sw_init` 返 -6 → func-on 失败 | `core/wmt_ctrl.c:372-425 / 489-497`；`core/wmt_ic_soc.c:2009-2018 / 1060` |
| BTIF if_tx 注册发生在 STP_OPEN（`wmt_core_stp_init` → `WMT_CTRL_STP_OPEN` → `mtk_wcn_consys_stp_btif_open()` → `mtk_wcn_stp_register_if_tx(BTIF,...)`），**本就不依赖 launcher**，只依赖 mtk_btif_drv.ko 先 probe | `core/wmt_ctrl.c:456-487`；`linux/pri/stp_btif.c:77-91` |
| 上板脚本现状：`z1_modprobe_connsys.sh:72-82` 后台起 launcher **无任何等待**；`connsys-up.sh:14-17` 起 launcher 后 `sleep 2`（脆弱）；wifi-up.sh 在 connsys-up 之后 `echo 1 > /dev/wmtWifi` | `scripts/z1_modprobe_connsys.sh`；`tools/connsys-up.sh`；`tools/wifi-up.sh` |
| `osal_assert` 只打印不 panic | `linux/include/osal.h:342-346` |

---

## 1. 方案A — 删 `wmt_exp.c` 的 `u4InfoBit=HIF_BIT` 回归行

**状态**：已在 `/tmp/connsys_build/conn_soc/linux/pri/wmt_exp.c` 落树（L100-110 注释 + 无赋值），
`mtk_stp_wmt_soc.ko` 已重链（369696 B，15:47）。备份 `bridge_backups/wmt_exp.c_hiffix_remove_20260814_154731.bak`，
归档 `bridge_backups/products/products_connsys_hifconf_fix_20260814_154850/`。反汇编验证无写 offset#4 指令。

**launcher 正常时够不够？→ 够。**
- launcher 启动期先发 `SET_STP_MODE 0x23` → `wmt_lib_set_hif` 置 `hifType=BTIF(2)` + `g_stp_if_type=BTIF` →
  HIF_CONF op 入队。func-on（`echo 1 > /dev/wmtWifi`）后发，wmtd FIFO 先处理 HIF_CONF → `wmtInfoBit|=HIF_BIT`、`wmtHifConf.hifType=2`。
- func-on op 现在 `u4InfoBit=0` → `opfunc_pwr_on` 不调 `opfunc_hif_conf` → 合法 hifType 保留 →
  `wmt_core_stp_init` L669 `hifType==BTIF` → `f_mode|=MTKSTP_BTIF_MAND_MODE(0x8)` → hw_check 走 MAND 分支成功。
- 这就是 2026-08-14 成功那轮（`WIFI_FUNC_ON_SUCCESS`）的完整路径。**A 是根因修复，不可省。**

**launcher 缺席（g_stp_if_type 仍 STP_MAX_IF_TX）会怎样？→ A 救不了，func-on 三层全挂：**
1. `wmtInfoBit=0`（HIF_BIT 唯一入口就是 launcher 的 HIF_CONF op）→ `wmt_core_stp_init` L647 `"no hif info"` → return -1 → `opfunc_pwr_on` 失败。非 panic（osal_assert 只打印），`WMT_PWRON_RTY_DFT=2` 重试后 func-on 返回失败。
2. 即便绕过 L647：`g_stp_if_type=STP_MAX_IF_TX` → `mtk_wcn_sys_if_tx()` 恒 -1 → STP 任何 TX 发不出。
3. 即便绕过以上：首次上电的 `sw_init` 里 `srh_patch` 无 launcher 应答 → 2s 超时 → `patch_info_prepare` -1 → `sw_init` -6 → func-on 仍失败。

**func-on 抢先于 launcher SET_STP_MODE 落地（竞态窗口）→ A 也救不了**（见 §4 D 的窗口描述）。

**结论**：A = 必要条件，充分条件只是"launcher 正常 + 无时序竞态"。它不改 launcher 缺席行为，也不解耦时序。

---

## 2. 方案B — `wmt_core_stp_init` 里 hifType 非法时默认按 BTIF

**代码可行性**：可行。把 `core/wmt_core.c:669` 条件从
```c
if (WMT_HIF_BTIF == pctx->wmtHifConf.hifType) {
```
扩为
```c
if ((WMT_HIF_BTIF == pctx->wmtHifConf.hifType) ||
    (WMT_HIF_MAX <= pctx->wmtHifConf.hifType)) {   /* 非法值(≥3)兜底按 BTIF */
```
- `WMT_HIF_MAX=3`；被回归覆盖出的 `hifType=3(WIFI)/6(LPBK)` 命中 → 会设 MAND mode，恰好挡住 A 修复前的那种覆写。
- 单文件、1 条件、只影响 L669-673 这一小段，不动其它逻辑。改动量最小。

**影响/局限（关键）**：
- **只修 f_mode，不修 `g_stp_if_type`**。就算 f_mode 对了，`g_stp_if_type=STP_MAX_IF_TX` 时 `sys_if_tx` 仍 -1，hw_check 照样发不出。所以 **B 单独不足**，必须配 C 的 g_stp_if_type 默认才闭环。
- **覆盖不到 hifType=0**：launcher 缺席时 `gMtkWmtCtx` 被 memset，`hifType=0`(=WMT_HIF_UART，是"合法"枚举)，B 的 `>=WMT_HIF_MAX` 判断不命中 → 仍失败。而且若回归再犯、且首个 func-on 是 BT(type=0)，`au4OpData[0]=0` 也会把 hifType 覆盖成 0，B 同样抓不到。
- 想让它覆盖 hifType=0，就得"凡是 UART/SDIO 以外的都按 BTIF"——那其实就退化成 C 了（在 SOC 上无条件默认 BTIF）。

**结论**：B 的命中集 = A 已防住的 3/6 覆写场景，与 A 冗余；对 launcher 缺席无帮助。可作为防"回归复发"的廉价深水区，但**不是必要项**。

---

## 3. 方案C — 模块加载时默认 BTIF（不依赖 launcher）

目标：让"STP 传输配置"（f_mode + g_stp_if_type + wmtInfoBit/hifType）在 insmod 时就固定为 BTIF，
launcher 的 SET_STP_MODE 变成"确认/覆盖"，而不是"唯一来源"。具体落点（都在 `mtk_stp_wmt_soc.ko` 单模块内，无跨模块符号问题）：

1. **wmtInfoBit + hifType 默认**（`core/wmt_core.c:268-282` `wmt_core_init`，memset 之后）：
   ```c
   /* MT6572 SOC: BTIF 是唯一 HIF，默认即配置好，不依赖 launcher 的 HIF_CONF op */
   gMtkWmtCtx.wmtInfoBit |= WMT_OP_HIF_BIT;
   gMtkWmtCtx.wmtHifConf.hifType = WMT_HIF_BTIF;
   ```
   → L647 检查通过、L669 命中、`wmt_ic_soc.c:1014` 的 FULL-mode 段也命中。launcher 稍后发 HIF_CONF op
   只是重复写同值（`opfunc_hif_conf` 会打一行 `WMT HIF already exist. overwrite! old(2),new(2)`，无害）。

2. **g_stp_if_type 默认**（`linux/pri/stp_exp.c:56/70`）：静态初值 `STP_MAX_IF_TX` → `STP_BTIF_IF_TX`
   （或 `wmt_lib_init` 里调 `mtk_wcn_stp_set_if_tx_type(STP_BTIF_IF_TX)`）。launcher 之后同样写同值，无害。

3. **"注册 BTIF if_tx"**：**不需要**在 load 时额外做。`stp_btif_if_tx` 的注册点在 STP_OPEN
   （`wmt_core_stp_init` → `mtk_wcn_consys_stp_btif_open()`，`stp_btif.c:88`），它只依赖
   `mtk_btif_drv.ko` 先 probe，**与 launcher 无关**。若在 load 时提前调 `mtk_wcn_btif_open()`，反而有
   副作用风险（要求 btif 已 probe、提前占用 wakeup 资源、时序未到芯片未上电）。任务描述里的"注册
   BTIF if_tx"应当理解为该项已天然满足，写进注释即可。

**效果**：
- func-on 与 launcher SET_STP_MODE 的**顺序完全解耦**：即使 func-on 先到（甚至 launcher 还没 open /dev/stpwmt），
  STP 层已正确，`wmt_core_stp_init` 照常跑。
- 残余的 launcher 依赖只剩 **patch 元数据**（`srh_patch`，发生在 sw_init 里，天然在 func-on 之后几百 ms +
  自带 2s 等待窗口）→ 时序宽容度从"毫秒级竞态"放宽到"秒级"，launcher 只要在 sw_init 前在线即可。
- launcher 首轮上电后若 launcher 被杀，STP 配置仍有效（BT 续用不受影响）。

**局限（必须讲清）**：
- **launcher 完全缺席时，首次 func-on 依然失败**：`sw_init` 的 `srh_patch` 无 launcher → 2s 超时 →
  `patch_info_prepare` -1 → sw_init -6。C 只让内核"STP 层自足"，不能让无 launcher 的 WiFi 工作
  （patch 文件名是 launcher 通过 ioctl 喂的，内核不自知）。所以 C 的目标是**消除时序依赖**，不是替代 launcher。
- **风险**：L647 的 "no hif info" 保护被旁路，真实误配会被默认值掩盖。此模块只 build 给 Z1（MT6572 唯一
  BTIF），编译期默认可接受。若将来此模块复用到 combo 芯片，需再加 `wmt_ic_ops_soc` 判断。

**改动量**：2 个文件、2-3 行。

---

## 4. 方案D — 脚本层：launcher 先启动 + 等 SET_STP_MODE 完成再 func-on

**现状**：`z1_modprobe_connsys.sh:72-82` 后台起 launcher 后立即返回，func-on（手动 echo）时序靠"人类慢"；
自动化链 `S99bt → bt-up.sh → connsys-up.sh` 用 `sleep 2`（`connsys-up.sh:14-17`），LAUNCHER_TIMING 已证明
`t3`（chipid query 可能 300ms×N 重试）可超 2s → 窗口 [t0, t3) 内 func-on 撞 "no hif info"。

**可行性注意**：SET_STP_MODE ioctl 是 **fire-and-forget**（`timeoutValue=0`，入队即返回），**无法直接观测
"完成"**。所以 D 的"等待"只能近似，用可观测就绪标志：
- **推荐标志 A**：`dmesg | grep 'WMT HIF info added'`（`opfunc_hif_conf` 用 `WMT_ERR_FUNC` 打印，ERR 级必现）——
  表示 HIF_CONF op 已被 wmtd 处理、`wmtInfoBit` 已置。这是"SET_STP_MODE 生效"的最直接证据。
- 备选标志 B：等 launcher 自身日志行 `stpmode[3]`（`/tmp/launcher.log`，紧邻 SET_STP_MODE ioctl）。
- **还需等上电完成**：launcher 的 pwr-on 线程会连发 LPBK func-on 做首次上电；外部 func-on 若撞进该窗口会
  触发 `opfunc_pwr_on` 的 `"already powered on"`/`"not ready"`。就绪标志：`dmesg | grep 'patch total num'`
  （sw_init 打过，`wmt_ic_soc.c` WMT_WARN 级）或 /dev/stpbt open 成功。
- busybox 环境：`dmesg`/`grep`/循环轮询 busybox 都有，脚本层可行。

**效果/局限**：零驱动风险，纯脚本。但**救不了 launcher 缺席**；且只能近似观测 SET_STP_MODE（严格说是
"HIF_CONF 已处理"而非"已完成"——对 func-on 而言前者就是正确门槛）。D 与 C 互补：C 把内核做成"不等也行"，
D 把自动化链做成"按序执行"。

---

## 5. 对比矩阵

| 方案 | 可靠性 | 改动量 | 对 launcher 依赖 | 风险 |
|---|---|---|---|---|
| **A**（删回归行，已落树） | launcher 正常+无竞态时高；竞态/缺席时仍失败 | 1 行删 + 注释；已重链 | 仍是**唯一** wmtInfoBit/hifType/g_stp_if_type 来源 | 低（回到历史成功路径） |
| **B**（hifType 非法默认 BTIF） | 低：只修 f_mode；不修 g_stp_if_type；覆盖不到 hifType=0；命中集≈A 已覆盖 | 1 文件 1-2 行 | 仍是唯一来源 | 低，但改了共享文件 `wmt_core.c` 的判断 |
| **C**（加载期默认 BTIF） | 高：STP 配置层完全时序解耦；launcher 缺席时 STP 也自足（首次 func-on 仍死在 patch 下载，需 launcher） | 2 文件 2-3 行 | 仅剩 patch 元数据（srh_patch） | 中低：旁路 "no hif info" 保护；Z1-only 可接受 |
| **D**（脚本等 launcher 就绪） | 中：确定性消除启动竞态；救不了缺席 | 1 脚本（轮询 dmesg） | 依然要求 launcher 存在 | 零（不动驱动） |

---

## 6. 推荐组合

**A + C + D**，理由：

1. **A（必须，已做）**：没有它，launcher 正常也会因 hifType 被覆写成 3/6 而死（回归）。这是根因修复，任何组合都不该去掉。
2. **C（推荐主改）**：把 STP 配置从"launcher 时序依赖"改成"内核默认"，2-3 行、单模块、低成本地消除最脆弱的一环；
   同时落实此前 `HW_CHECK_FAIL_ROOTCAUSE §6.3` 标为"待做"的 SOC 默认 BTIF 兜底（还补上它漏掉的 g_stp_if_type 默认）。
3. **D（推荐补，零风险）**：自动化开机链（S99bt/connsys-up）目前只有 `sleep 2`，改成"轮询 `WMT HIF info added`
   + `patch total num` 再放行 func-on"，把 C 留下的 srh_patch 秒级窗口也守住。
4. **B（可选，低优先）**：A 已防住 3/6 覆写，C 已让 hifType 恒为 BTIF，B 的命中集被 A+C 完全覆盖；仅当想要"防回归复发"
   的额外深水区才加，且不能替代 C 的 g_stf_if_type 部分。

**不建议的组合**：只 A（竞态/缺席仍挂）；A+B（g_stp_if_type 仍依赖 launcher，B 无增量）；只 D（内核无兜底，launcher
一死/一慢就挂）。

**上板验证点（C+D 落地后）**：
- 起 launcher 前（或 launcher 缺席）手动 `echo 1 > /dev/wmtWifi`：应见 `get hwcode (chip id) (0x6572)`（hw_check 过），
  再在 sw_init 的 `srh_patch` 处 2s 超时失败——证明 STP 层已不依赖 launcher，只剩 patch 依赖。
- launcher 正常 + 即刻 func-on：不再有 `WMT-CORE: no hif info!`；可看到 `WMT HIF already exist. overwrite! old(2),new(2)`（无害）。
- 自动化链：`connsys-up.sh` 改为等 dmesg `WMT HIF info added` + `patch total num` 后再跑 `stpbt-hci-test`。

---

## 7. 相关文件索引（绝对路径）

- 改动对象：`/tmp/connsys_build/conn_soc/linux/pri/wmt_exp.c`（A 已落）、`/tmp/connsys_build/conn_soc/core/wmt_core.c`（B/C-1）、`/tmp/connsys_build/conn_soc/linux/pri/stp_exp.c`（C-2）、`/home/<user>/claude/6572/scripts/z1_modprobe_connsys.sh`（D）
- 只读依据：`/tmp/connsys_build/conn_soc/core/{wmt_core.c,wmt_lib.c,wmt_ctrl.c,wmt_ic_soc.c,stp_core.c}`、`/tmp/connsys_build/conn_soc/linux/pri/{wmt_dev.c,stp_btif.c,stp_exp.c}`、`/tmp/connsys_build/tools/launcher/stp_uart_launcher.c`、`/tmp/connsys_build/tools/{connsys-up.sh,wifi-up.sh}`、`/home/<user>/claude/6572/scripts/z1_modprobe_connsys.sh`
- 前序报告：`/home/<user>/claude/6572/mainline_recon/{WMT_HIFCONF_REGRESSION_FIX_20260814,WMT_HIF_RACE_FIX_20260814,HW_CHECK_FAIL_ROOTCAUSE_20260814,STP_FMODE_SOURCE_20260814,LAUNCHER_TIMING_20260814,WIFI_FUNC_ON_SUCCESS_20260814}.md`
- 备份/归档：`/home/<user>/claude/6572/bridge_backups/wmt_exp.c_hiffix_remove_20260814_154731.bak`、`/home/<user>/claude/6572/bridge_backups/products/products_connsys_hifconf_fix_20260814_154850/`
