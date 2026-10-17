# HW_CHECK fail (-2) 根因分析 — CONNSYS WiFi func-on 阻塞

> 日期: 2026-08-14
> 性质: 纯离线源码分析, 未碰硬件/未改文件
> 源码: `/tmp/connsys_build/conn_soc/` + `/tmp/connsys_build/btif/` + `/tmp/connsys_build/tools/launcher/`
> 结论置信度: 高 — 失败日志的每一个现象都能被源码路径逐行解释, 且与"上一轮修复"的改动直接因果对应。

---

## 1. 执行摘要（一句话根因）

**上一轮为修 "no hif info" 竞态在 `mtk_wcn_wmt_func_ctrl()` (wmt_exp.c:108) 给每个 FUNC_ON/FUNC_OFF op 无条件加的 `u4InfoBit = WMT_OP_HIF_BIT`, 导致 `opfunc_pwr_on()` (wmt_core.c:897-898) 每次都调用 `opfunc_hif_conf()`, 而 `opfunc_hif_conf()` (wmt_core.c:877) **无条件把 launcher 先前通过 WMT_IOCTL_SET_STP_MODE→HIF_CONF op 写好的 `gMtkWmtCtx.wmtHifConf` 用 FUNC_ON op 的 `au4OpData[0..2]` 覆盖掉**, 得到 `hifType = 驱动类型号(3=WIFI / 6=LPBK)` 这种非法值(合法 BTIF=2)。**

于是 `wmt_core_stp_init()` 里 `WMT_HIF_BTIF == wmtHifConf.hifType` 判断为假 → **STP 模式从未被设为 `MTKSTP_BTIF_MAND_MODE`** → `stp_core_ctx.f_mode` 保持 0 → `mtk_wcn_stp_send_data()` 的 MAND/SDIO 分支与 BTIF_FULL 分支**全部不匹配**, 直接返回 0 → `wmt_core_tx` 只得到 writtenSize=0 → 重试 3×360ms 仍为 0 → `wmt_core_reg_rw_raw` 报 `Tx REG_CMD fail!(0) len (0,20)` → hw_check 读 0x80000008 失败 → -2 → stp_init -4 → pwr_on fail。

**修复建议一句话**: `mtk_wcn_wmt_func_ctrl()` 不应给 func 类 op 带 `WMT_OP_HIF_BIT`(要么删掉该行, 要么同时把当前合法的 `wmt_lib_get_hif()` 配置原样拷进 `au4OpData[0..]` 让 `opfunc_hif_conf` 变成无损回写); 并确保 launcher 在 func-on 前已跑过 `WMT_IOCTL_SET_STP_MODE`(否则 `g_stp_if_type` 仍是 `STP_MAX_IF_TX`, STP 依然发不出去)。

---

## 2. func-on 完整时序链（每一步谁在做、依赖什么）

### 2.1 入口
- `echo 1 > /dev/wmtWifi` → `wmt_chrdev_wifi.c:386` `mtk_wcn_wmt_func_on(WMTDRV_TYPE_WIFI=3)`。
- (若 launcher 存活, 它开机时已跑过 `WMT_IOCTL_LPBK_POWER_CTRL` → `func_on(WMTDRV_TYPE_LPBK=6)` 做首次上电; 两路都会踩到同一 bug, 见 §5。)
- `wmt_exp.c:84-137 mtk_wcn_wmt_func_ctrl()`:
  - `pOp->op.au4OpData[0] = type` (3 或 6)
  - **`pOp->op.u4InfoBit = WMT_OP_HIF_BIT;` ← 本次回归源头 (wmt_exp.c:108)**
  - 丢进 active 队列, wmtd 线程 `wmt_lib.c:882` 调 `wmt_core_opid()` → `opfunc_func_on`。

### 2.2 opfunc_func_on → opfunc_pwr_on
- `wmt_core.c:1022`: WMT 非 FUNC_ON → `opfunc_pwr_on(pWmtOp)`。
- `wmt_core.c:897-898`:
  ```c
  if (pWmtOp->u4InfoBit & WMT_OP_HIF_BIT)
      opfunc_hif_conf(pWmtOp);
  ```
  → 因 fix, 条件为真, 进入 `opfunc_hif_conf`。
- `wmt_core.c:862-880 opfunc_hif_conf()`:
  ```c
  if (gMtkWmtCtx.wmtInfoBit & WMT_OP_HIF_BIT)
      WMT_ERR_FUNC("WMT HIF already exist. overwrite! old(%d), new(%d))", ...);  // L870, 日志应出现
  osal_memcpy(&gMtkWmtCtx.wmtHifConf, &pWmtOp->au4OpData[0], osal_sizeof(gMtkWmtCtx.wmtHifConf));  // L877
  ```
  - `WMT_HIF_CONF` = `{hifType; au4HifConf[4]; au4StrapConf[4]}` = 36B (wmt_core.h:159-163)。
  - 从 `au4OpData[0]` 拷 12 字节 → `hifType = au4OpData[0] = type`, `au4HifConf[0]=au4OpData[1]=0`, `au4HifConf[1]=au4OpData[2]=0`。
  - **结果 `hifType = 3(WIFI)/6(LPBK)`, 而非 `WMT_HIF_BTIF=2`。** 之前 launcher 通过 HIF_CONF op 写好的 `hifType=2` 被覆盖。
- `wmt_core.c:904 WMT_CTRL_HW_PWR_ON` → `wmt_ctrl.c:356-370 wmt_ctrl_hw_pwr_on` → `wmt_plat_pwr_ctrl(FUNC_ON)` 上电 CONNSYS。

### 2.3 wmt_core_stp_init (wmt_core.c:636-714)
1. L647 检查 `wmtInfoBit & WMT_OP_HIF_BIT` → 因 fix 已置位, 通过(这正是上轮 fix 想达成的效果, 但代价见上)。
2. L655 `WMT_CTRL_STP_OPEN` → `wmt_ctrl.c:456-487 wmt_ctrl_stp_open`:
   - `mtk_wcn_stp_open_btif()` → `stp_core.c:3184` → `stp_btif.c:77 mtk_wcn_consys_stp_btif_open()` → `mtk_wcn_btif_open()` + `mtk_wcn_stp_register_if_tx(STP_BTIF_IF_TX, ...)` (注册 `stp_btif_if_tx` 到 `stp_exp.c:69`)。
   - `mtk_wcn_stp_rxcb_register(mtk_wcn_stp_parser_data)` (BTIF RX → STP 解析器)。
3. L661-666 STP_CONF 先 EN=0/RDY=0。
4. **L669-673 (关键被跳过的一段)**:
   ```c
   if (WMT_HIF_BTIF == pctx->wmtHifConf.hifType) {   // 2 == 3/6 → FALSE → 跳过
       ctrlPa1 = WMT_STP_CONF_MODE;
       ctrlPa2 = MTKSTP_BTIF_MAND_MODE;              // stp_core.h:100, bit3
       iRet += wmt_core_ctrl(WMT_CTRL_STP_CONF, ...); // → mtk_wcn_stp_set_mode(mode)
   }
   ```
   因 hifType 被覆盖成 3/6, **这整段不执行**, `stp_core_ctx.f_mode` 不被设置(保持 mtk_wcn_stp_init 时的 0)。
5. L675-677 STP_CONF EN=1 → `mtk_wcn_stp_enable(1)` → `stp_core.c:2282` → `stp_rest_ctx_state()` (只清 rx/tx 缓冲、seq、parser 状态,**不清 f_mode**) + `f_enable=1`。
6. L684 `wmt_core_hw_check()` → 失败(见 §4)。

### 2.4 hw_check (wmt_core.c:781-860)
- L790 `wmt_core_reg_rw_raw(0, GEN_HCR, &chipid, GEN_HCR_MASK)` → 读 `0x80000008`(见 §3)。
- 失败返回 -2 → L686 stp_init 返回 -4 → L918 pwr_on fail。

### 2.5 依赖关系总结
| 步骤 | 依赖 | 本 bug 是否破坏 |
|---|---|---|
| STP_OPEN / BTIF open | launcher 先加载模块; `mtk_wcn_btif_open` 成功 | 否(会走到) |
| `g_stp_if_type = STP_BTIF_IF_TX` | launcher 的 `WMT_IOCTL_SET_STP_MODE` → `wmt_lib_set_hif()` (wmt_lib.c:393-415) | 依赖 launcher 存活; 本次测试 connsys-up.sh 已启动 |
| `wmtHifConf.hifType = BTIF` | launcher 的 HIF_CONF op (wmt_dev.c:1875-1907) | **被 fix 覆盖成 3/6** |
| STP f_mode = BTIF_MAND | `wmt_core_stp_init` L669-673 | **因 hifType 错被跳过** |
| REG_CMD 经 STP 发出 | f_mode + g_stp_if_type + BTIF 写 | **f_mode=0 → 直接 return 0** |

---

## 3. CONN MCU 固件什么时候下载？（问题2回答）

### 3.1 ROM patch — 在 hw_check **之后**, 由内核 `sw_init` 同步下载
- `wmt_core_stp_init()` (wmt_core.c:684-703) 顺序:
  1. `wmt_core_hw_check()` ← 现在死在这里
  2. `co_clock_ctrl`
  3. `p_ic_ops->sw_init()` = `mtk_wcn_soc_sw_init()` (wmt_ic_soc.c:980)
- `mtk_wcn_soc_sw_init()` 内的下载序列:
  - L1017 `init_table_1_2`(QUERY_STP, TEST-ONLY)
  - L1024 `init_table_4`(SET_STP 命令, 通知 fw 切 STP 参数)
  - L1031-1036 设 `MTKSTP_BTIF_FULL_MODE` + EN=1(此时才切到 FULL 模式!)
  - L1045 `init_table_5`(再 QUERY_STP 确认)
  - L1060 `mtk_wcn_soc_patch_info_prepare()` → `WMT_CTRL_PATCH_SEARCH` → `wmt_ctrl.c:489 wmt_ctrl_patch_search` → `wmt_ctrl_ul_cmd("srh_patch")` → **内核通过 `/dev/stpwmt` read 把 "srh_patch" 发给 launcher**; launcher `stp_uart_launcher.c:498-720 cmd_hdr_sch_patch` 扫 `/system/etc/firmware/ROMv1_patch*`, 再 `ioctl(WMT_IOCTL_SET_PATCH_NUM)` / `WMT_IOCTL_SET_PATCH_INFO` 把 patch 元数据登记回内核, 回写 "ok"。
  - L1069 `WMT_CTRL_GET_PATCH_NUM` 取 patch 数; L1085-1096 `mtk_wcn_soc_patch_dwn()` 逐片下载(每片 1000B, 经 `wmt_core_tx` 发 WMT_PATCH_CMD, 再 `init_table_3` WMT_RESET)。
- **结论**: ROM patch 下载完全在 hw_check 之后; hw_check 读 chip id 时 CONN MCU 处于**出厂 ROM 状态**, 必须由 ROM 里的 STP 引擎响应 WMT 寄存器读命令(opcode 0x08)。这是原厂设计(成功那轮已证明 ROM 能响应)。因此失败**不是**"固件没下所以读不到", 而是 host 侧 STP 传输根本没建立(见 §4)。

### 3.2 WIFI RAM code — 更晚, 在 wlan probe 里
- `opfunc_func_on` (wmt_core.c:1035) → `wmt_func_wifi_on()` (wmt_func.c:640-675) → `mtk_wcn_wlan_probe()` → wlan_gen2 驱动下载 `WIFI_RAM_CODE_*`。
- launcher 不参与 RAM code 下载; 它只提供 ROM patch 元数据。

### 3.3 hw_check 读的地址
- `wmt_ic.h:80-85`: `GEN_CONFG_BASE = 0x80000000`, `GEN_HCR = 0x80000000+0x8 = 0x80000008`(HW_CODE/chip id), `GEN_HCR_MASK = 0xFFFF`。
- `wmt_core.c:185-193 WMT_SET_REG_CMD[]` 20 字节: op=0x08, w/r=0x02(read), type=reg, addr=0x80000008(LE), value=0, mask=0xFFFFFFFF。

---

## 4. `Tx REG_CMD fail!(0) len (0,20)` 的确切机制（问题3回答）

### 4.1 返回值语义
- `wmt_core_reg_rw_raw` (wmt_core.c:556-559): `iRet = wmt_core_tx(...)`, 打印 `iRet=0, u4Res=0, sizeof=20`。
- `wmt_core_tx` (wmt_core.c:294-331): 调 `wmt_ctrl_tx_ex()`; `wmt_ctrl_tx_ex` (wmt_ctrl.c:285-326) **恒返回 0**(除非 size=0 或 STP 未 OPEN/EN), 把真实成败放在 `*writtenSize`。
- 故 `iRet=0 且 written=0` 表示: STP_OPEN/EN 两个 bit 都置位(通过了 wmt_ctrl.c:301 检查), 但 `mtk_wcn_stp_send_data()` 返回 0。

### 4.2 mtk_wcn_stp_send_data 为何返回 0
`stp_core.c:2407-2646 mtk_wcn_stp_send_data()`, `ret` 初值 0, 三个发送分支:
1. L2522 `(is_sdio_mode() || is_btif_mand_mode()) && STP_IS_ENABLE` — 需 `f_mode & MTKSTP_BTIF_MAND_MODE(bit3)`。
2. L2558 `is_btif_fullset_mode() && STP_IS_ENABLE` — 需 `f_mode & MTKSTP_BTIF_FULL_MODE(bit2)`。
3. 都不匹配 → 走到函数尾 `return ret;` → **ret=0**。

因 §2.3 第4步 `f_mode` 从未被设成 MAND/FULL(=0), **两个分支都不进**, 直接 return 0。这正是日志里**看不到**任何 STP 层报错("no winspace"/"stp send tx packet"/"stp btif write fail")的原因 — 数据根本没到 BTIF。

### 4.3 wmt_core_tx 重试
- `wmt_core.c:315-328`: written==0 → 打印 `retrying, wait for 360 ms` → `osal_sleep_ms(360)` → 重发, 共 3 次。每次都同样失败, 于是日志出现多次 "retrying"。
- 3 次后返回 iRet=0, written 仍 0 → `reg_rw_raw` 报 `len (0, 20)`。

### 4.4 STP tx 通道(BTIF)配置状态梳理
| 变量 | 默认值 | 谁设置 | 本 bug 时状态 |
|---|---|---|---|
| `g_stp_if_type` (stp_exp.c:70) | `STP_MAX_IF_TX` | 仅 `wmt_lib_set_hif()` (wmt_lib.c:404/407/410) ← launcher `WMT_IOCTL_SET_STP_MODE` | launcher 存活 → `STP_BTIF_IF_TX` ✓ |
| `stp_btif_if_tx` (stp_exp.c:69) | NULL | `mtk_wcn_consys_stp_btif_open()` (stp_btif.c:88) ← STP_OPEN | STP_OPEN 已跑 → 已注册 ✓ |
| `stpBtifId` (stp_btif.c:60) | 0 | `mtk_wcn_btif_open()` | STP_OPEN 已跑 → 非 0 ✓ |
| **`stp_core_ctx.f_mode`** | 0 | `mtk_wcn_stp_set_mode()` (stp_core.c:2824) ← `wmt_core_stp_init` L669-673 (需 hifType==BTIF) | **0 ✗ ← 根因** |

→ 即使 g_stp_if_type / BTIF 写通道都配好了, **只要 f_mode=0, STP 层就不调 sys_if_tx**, 一切白搭。

---

## 5. 对比成功路径（问题4回答）

### 成功那轮(修复前)
1. launcher `-m 3` → `wmt_lib_set_hif(0x23)` → `rWmtHifConf.hifType = WMT_HIF_BTIF(2)` + `g_stp_if_type = STP_BTIF_IF_TX`; 队列 HIF_CONF op(带完整 conf)。
2. wmtd 先处理 HIF_CONF op → `opfunc_hif_conf` → `wmtHifConf.hifType=2`(合法)。
3. FUNC_ON op **当时不带 u4InfoBit** → `opfunc_pwr_on` L897 条件为假 → **不调 opfunc_hif_conf** → 合法 conf 保留。
4. `wmt_core_stp_init` L669: hifType==BTIF → 设 `f_mode |= MTKSTP_BTIF_MAND_MODE`。
5. hw_check: REG_CMD 走 `stp_core.c:2522` MAND 分支 → `sys_if_tx`(BTIF) → CONN MCU ROM 响应 → chipid=0x6572 ✓。
6. sw_init: 下 ROM patch → `WMT turn on WIFI success` → wlan probe → RAM code → wlan0 + MAC。

### 失败这轮(修复后)
只有第 3 步不同: FUNC_ON op 被 fix 加上 `u4InfoBit=HIF_BIT` → `opfunc_pwr_on` 调 `opfunc_hif_conf` → **hifConf 被覆盖为 hifType=3/6** → 第 4 步 f_mode 不设 → hw_check 发不出。**硬件没有变化**(成功轮证明 CONN MCU ROM 和 BTIF 都工作正常), 是纯粹的 host 侧软件流程回归。

### "no hif info" 竞态的正解
上轮 fix 试图让 func_on 在 launcher 未就绪时也能过 `wmt_core_stp_init` L647 的检查。但:
- launcher 正常时, HIF_CONF op 先于 FUNC_ON op 入队, wmtd 顺序处理, 本无竞态;
- "no hif info" 只会在 launcher 没跑/没发 SET_STP_MODE 时出现;
- 正确做法是保证 launcher 存活(或内核给 SOC 设默认 BTIF), 而不是给 func op 塞 HIF_BIT。

---

## 6. 修复建议

### 6.1 首选(最小正确修复) — 不让 func op 破坏 hifConf
在 `wmt_exp.c mtk_wcn_wmt_func_ctrl()` (L84-137) 中, **FUNC_ON/FUNC_OFF op 不要携带 `WMT_OP_HIF_BIT`**(删除或条件化 L108)。这样:
- launcher 存活 → HIF_CONF op 已设好 `wmtInfoBit|HIF_BIT` 且 `hifType=BTIF` → L647 通过、L897 不覆盖 → 恢复成功路径。
- 恢复后若再次出现 "no hif info"(launcher 没跑), 用 6.3 兜底。

### 6.2 若坚持让 func op 自足 — 无损回写
在 `mtk_wcn_wmt_func_ctrl()` 设置 u4InfoBit 时, **同时把当前合法配置拷进 au4OpData**, 让 `opfunc_hif_conf` 变成 no-op:
```c
P_WMT_HIF_CONF pHif = wmt_lib_get_hif();
pOp->op.u4InfoBit = WMT_OP_HIF_BIT;
if (pHif && pHif->hifType < WMT_HIF_MAX)
    osal_memcpy(&pOp->op.au4OpData[0], pHif, sizeof(WMT_HIF_CONF));
```
注意: 若 launcher 从没跑, `pHif` 全 0 → hifType=0(UART), 依然错 → 仍需 6.3。

### 6.3 兜底 — SOC 默认 BTIF(处理 launcher 缺席)
- `wmt_core_stp_init()` L669 前加: 若 `hifType >= WMT_HIF_MAX` 且 `wmt_ic_ops_soc.icId` 已识别 → 默认按 `WMT_HIF_BTIF` 处理(设 MAND mode)。
- 同时 `wmt_lib_set_hif()`/模块加载时默认 `mtk_wcn_stp_set_if_tx_type(STP_BTIF_IF_TX)` + 注册 BTIF if_tx(否则 `g_stp_if_type` 仍 STP_MAX_IF_TX, 即使 f_mode 对了也发不出去)。

### 6.4 验证手段(下次上板)
1. dmesg 确认出现 `WMT-CORE: WMT HIF already exist. overwrite! old (2), new (3/6)` → 证明 §4 覆盖发生; 修复后该行不应再出现。
2. 修复后应看到 `get hwcode (chip id) (0x6572)` → `patch total num = [n]` → `WMT turn on WIFI success` → `wlan0`。
3. 可在 `mtk_wcn_stp_set_mode()` 里临时打印 f_mode, 确认 hw_check 前为 `0x8`(MAND)。

---

## 7. 关键符号/行号速查表

| 符号 | 位置 | 说明 |
|---|---|---|
| `mtk_wcn_wmt_func_ctrl` u4InfoBit | `linux/pri/wmt_exp.c:108` | **回归源头** |
| `opfunc_pwr_on` 调 hif_conf | `core/wmt_core.c:897-898` | 因 fix 条件变真 |
| `opfunc_hif_conf` 覆盖 | `core/wmt_core.c:877` | 用 au4OpData 覆盖 hifConf |
| `WMT_HIF_CONF` | `core/include/wmt_core.h:159-163` | hifType@0, au4HifConf@4, au4StrapConf@20 |
| `WMT_HIF_BTIF=2` | `core/include/wmt_core.h:100-105` | 合法值; 被覆盖为 3/6 |
| `wmt_core_stp_init` 设 MODE | `core/wmt_core.c:669-673` | 被跳过 → f_mode=0 |
| `mtk_wcn_stp_set_mode` | `core/stp_core.c:2824-2829` | 只写 f_mode |
| `MTKSTP_BTIF_MAND_MODE` | `core/include/stp_core.h:100` | bit3 |
| `mtk_wcn_stp_send_data` 分支 | `core/stp_core.c:2522/2558` | 两分支都不进 → ret=0 |
| `wmt_core_tx` 重试 | `core/wmt_core.c:315-328` | 3×360ms |
| `wmt_core_reg_rw_raw` 报错 | `core/wmt_core.c:556-559` | `Tx REG_CMD fail!(0) len (0,20)` |
| `wmt_core_hw_check` | `core/wmt_core.c:781-860` | L790 读 GEN_HCR; -2 |
| `GEN_HCR` | `core/include/wmt_ic.h:84` | 0x80000008 |
| `wmt_core_stp_init` 返回 -4 | `core/wmt_core.c:686-687` | hw_check fail |
| `opfunc_pwr_on` 报 -4 | `core/wmt_core.c:918` | stp_init fail |
| `mtk_wcn_soc_sw_init` | `core/wmt_ic_soc.c:980` | ROM patch 下载在 hw_check 之后 |
| `mtk_wcn_soc_patch_info_prepare` | `core/wmt_ic_soc.c:2009` | srh_patch → launcher ioctl |
| `g_stp_if_type` 默认值 | `linux/pri/stp_exp.c:70` | STP_MAX_IF_TX; 仅 launcher 可改 |
| `wmt_lib_set_hif` | `core/wmt_lib.c:393-415` | hifType + if_tx_type 唯一设置点 |
| launcher SOC 流 | `tools/launcher/stp_uart_launcher.c:1436-1467` | `-m 3` → STP_BTIF_FULL; SET_STP_MODE=0x23 |
| `WMT_IOCTL_LPBK_POWER_CTRL` | `linux/pri/wmt_dev.c:1926-1942` | 触发 LPBK=6 的 func_on(同 bug) |
| `echo 1 > /dev/wmtWifi` | `linux/pub/wmt_chrdev_wifi.c:386` | 触发 WIFI=3 的 func_on(同 bug) |
