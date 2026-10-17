# STP f_mode 生命周期 / hw_check 依赖分析（只读源码，2026-08-14）

分析对象：`/tmp/connsys_build/conn_soc/`（CONNSYS 源码树，未编译验证，仅静态阅读）。
结论先行：**hw_check 依赖 MTKSTP_BTIF_MAND_MODE；MAND mode 只在 `wmt_core_stp_init()` L669-673 里、且 `hifType==WMT_HIF_BTIF(2)` 时设置；sw_init 里的 FULL mode 设置（wmt_ic_soc.c:1031）是独立的，但被同一个 hifType 条件门控、且在 hw_check 之后执行，救不了 hw_check。**

---

## 1. 模式常量定义（stp_core.h L96-101）

```c
#define STP_MODE_BIT(x) (0x1UL << x)
#define MTKSTP_UART_FULL_MODE    STP_MODE_BIT(0)   // = 0x01
#define MTKSTP_UART_MAND_MODE    STP_MODE_BIT(1)   // = 0x02
#define MTKSTP_BTIF_FULL_MODE    STP_MODE_BIT(2)   // = 0x04
#define MTKSTP_BTIF_MAND_MODE    STP_MODE_BIT(3)   // = 0x08
#define MTKSTP_SDIO_MODE         STP_MODE_BIT(4)   // = 0x10
```

`f_mode` 字段在 `mtkstp_context_struct`（stp_core.h L254）：`UINT32 f_mode;`
stp_core.c L88-89 宏直接映射：
```c
#define STP_SUPPORT_PROTOCOL(x)     ((x).f_mode)
#define STP_SET_SUPPORT_PROTOCOL(x, v) ((x).f_mode = (v))
```

## 2. f_mode 的写点（全量）

源码中 **只有 3 处写 f_mode**：

| # | 位置 | 代码 | 写值 | 触发条件 |
|---|------|------|------|----------|
| 1 | stp_core.c:1230（`mtk_wcn_stp_init`） | `STP_SET_SUPPORT_PROTOCOL(stp_core_ctx, 0)` | **0（无任何 mode bit）** | STP OPEN 时（`WMT_CTRL_STP_OPEN` → `mtk_wcn_stp_init`），无条件清零 |
| 2 | stp_core.c:2826（`mtk_wcn_stp_set_mode()`） | `STP_SET_SUPPORT_PROTOCOL(stp_core_ctx, mode)` | 传入值（MAND=0x08 / FULL=0x04 / 等） | `wmt_core_ctrl(WMT_CTRL_STP_CONF, WMT_STP_CONF_MODE, value)` → `wmt_ctrl_stp_conf_ex`（wmt_ctrl.c L582-584）→ `mtk_wcn_stp_set_mode(value)` |
| 3 | 同 2 的调用点（所有 `WMT_STP_CONF_MODE` 下发处） | — | 见下表 | 见下表 |

`wmt_ctrl_stp_conf_ex()`（wmt_ctrl.c L569-592）是唯一把值灌进 f_mode 的入口：
```c
case WMT_STP_CONF_MODE:
    mtk_wcn_stp_set_mode(value);   // 直接覆盖 f_mode（不带 OR，整个位图覆盖）
    iRet = 0;
    break;
```
注意：是**整值覆盖**，不是 OR。所以每次设 MODE 都会清掉其它 mode bit。

## 3. `WMT_STP_CONF_MODE` 的全部下发点（谁在何时设 mode）

| 调用点 | 位置 | 设的值 | 触发时机 | hifType 门控？ |
|--------|------|--------|----------|----------------|
| A | wmt_core.c:669-673（`wmt_core_stp_init`） | `MTKSTP_BTIF_MAND_MODE`（0x08） | `opfunc_pwr_on` → `wmt_core_stp_init`；**在 `wmt_core_hw_check()` 之前** | **`if (WMT_HIF_BTIF == pctx->wmtHifConf.hifType)`** —— 被跳过则 MAND 永远不设 |
| B | wmt_ic_soc.c:1031-1033（`mtk_wcn_soc_sw_init`） | `MTKSTP_BTIF_FULL_MODE`（0x04） | `wmt_core_stp_init` 里 **hw_check 之后**、`sw_init` 回调里 | **`if (WMT_HIF_BTIF == pWmtHifConf->hifType)`（L1014）** —— 同样被 hifType 门控 |
| C | wmt_core.c:1435-1437 | `WMT_STP_CONF_MODE`（值见上下文，非 BTIF 流程） | `wmt_core_stp_deinit` 收尾 / 复位流程（非本卡点路径） | 否（不复用 hifType 判断） |

写点 B 是**独立于 wmt_core_stp_init 的模式设置路径**：它不读 `pctx->wmtInfoBit`，也不走 L669-673 的 if，而是直接在 `sw_init` 回调里再次 `wmt_core_ctrl(WMT_CTRL_STP_CONF, WMT_STP_CONF_MODE, MTKSTP_BTIF_FULL_MODE)`。但注意：**B 仍然被 `pWmtHifConf->hifType == WMT_HIF_BTIF` 门控（wmt_ic_soc.c:1014）**，且 B 收到的是 `&pctx->wmtHifConf`（wmt_core.c:699 传入）——同一个被（可能）污染的 hifType。

## 4. hw_check 依赖哪个 mode —— 机制链条

hw_check（wmt_core.c:781-860）读 CONN chip id：
```c
iret = wmt_core_reg_rw_raw(0, GEN_HCR, &chipid, GEN_HCR_MASK);  // L790 读 0x80000008
```
`wmt_core_reg_rw_raw`（L543-589）→ `wmt_core_tx(WMT_SET_REG_CMD, ...)`（L556）→ `wmt_ctrl_tx_ex`（wmt_ctrl.c:285-314）→ **`mtk_wcn_stp_send_data(pData, size, WMT_TASK_INDX)`（L314，非 raw）**。

`mtk_wcn_stp_send_data`（stp_core.c:2407-2646）对 f_mode 的分支结构：
```c
L2514  if (STP_NOT_ENABLE && type != WMT_TASK_INDX)          { /* no-op */ }
L2517  else if (STP_NOT_ENABLE && type == WMT_TASK_INDX)     { /* no-op（被注释掉）*/ }
L2522  else if ((mtk_wcn_stp_is_sdio_mode() || mtk_wcn_stp_is_btif_mand_mode())
               && STP_IS_ENABLE) {  // ★ MAND/SDIO：短头直接 sys_if_tx
            // 4 字节头 0x80 | type<<4 | len_hi | len_lo | 0，CRC 区填 0，直接 sys_if_tx
            // L2545 (*sys_if_tx)(&tx_buf[0], 4+length+2, &ret)
        }
L2558  else if (mtk_wcn_stp_is_btif_fullset_mode() && STP_IS_ENABLE) {  // FULL：完整 STP 帧
            // seq/ack/winspace/CRC16/重传定时器，stp_send_tx_queue
        }
        // ★ f_mode=0 时：上面 4 个分支全部不命中 → ret 保持 0 → 什么都没发出
```

**关键结论：f_mode=0 时 `mtk_wcn_stp_send_data` 走不到任何发送分支，直接返回 0。** `wmt_core_reg_rw_raw` 收到 `u4Res != sizeof(WMT_SET_REG_CMD)` → `"Tx REG_CMD fail!"`（L558）→ 返回 -2 → `wmt_core_hw_check` 返回 -2（L791-794）→ `wmt_core_stp_init` L684-687 返回 -4。这就是 CONNSYS func-on 卡在 hw_check 的直接机制。

RX 侧同理（stp_core.c:2239-2247，`mtk_wcn_stp_rx` 解析入口）：MAND/SDIO 走 `stp_parser_data_in_mand_mode`，FULL 走完整解析器；f_mode=0 时 RX 解析也无分支可走。

## 5. 完整生命周期 + 状态迁移表

### 关键函数链
- `launcher` = Linux 侧 `/dev/stpwmt` ioctl（wmt_dev.c）+ func 开关（wmt_exp.c）。
- op 派发表：wmt_core.c:233-262，`WMT_OPID_HIF_CONF=0 → opfunc_hif_conf`，`WMT_OPID_FUNC_ON=3 → opfunc_func_on`。

### 正确路径（无回归）
```
[launcher] WMT_IOCTL_SET_STP_MODE (wmt_dev.c:1875-1909)
   → wmt_lib_set_hif(arg)            wmt_lib.c:393-429
       STP_BTIF_FULL → pHif->hifType = WMT_HIF_BTIF(=2)   (L408-410)
   → op{opId=HIF_CONF, u4InfoBit=WMT_OP_HIF_BIT, au4OpData[0..]=&pHif}
   → opfunc_hif_conf (wmt_core.c:862-880)
       wmtInfoBit |= WMT_OP_HIF_BIT; memcpy(&wmtHifConf, au4OpData[0], sizeof)  // hifType=2

[launcher] mtk_wcn_wmt_func_on(type) (wmt_exp.c:84-139)
   → op{opId=FUNC_ON, au4OpData[0]=type, u4InfoBit=0（wmt_lib_get_free_op 清零，不带 HIF_BIT）}
   → opfunc_func_on (wmt_core.c:990)
       L1021 WMT 未 FUNC_ON → opfunc_pwr_on(pWmtOp)
          L897 if (u4InfoBit & WMT_OP_HIF_BIT)  // =0 → FALSE，不再覆写 hifType ★
          L904 HW_PWR_ON → L916 wmt_core_stp_init()
```

### f_mode 状态迁移表（正确路径）

| 步骤 | 位置 | f_mode 值 | 前置条件 | 说明 |
|------|------|-----------|----------|------|
| S0 | stp_core.c:1230 `mtk_wcn_stp_init`（STP OPEN） | **0** | 无 | 无条件清零 |
| S1 | wmt_core.c:669-673（wmt_core_stp_init） | **0x08 BTIF_MAND** | `wmtInfoBit & WMT_OP_HIF_BIT`（L647，HIF_CONF op 已设）+ `hifType==BTIF`（L669） | 芯片 bootloader 阶段的最小协议，短头发送 |
| S2 | L675-677 | 不变 0x08 | — | `STP_CONF_EN=1` 置 `f_enable`（send_data L2522 分支需要的 `STP_IS_ENABLE`） |
| S3 | L684 `wmt_core_hw_check()` | 0x08（生效中） | — | reg_rw_raw 读 GEN_HCR：走 L2522 MAND 分支 → 成功 |
| S4 | L699 `sw_init` = `mtk_wcn_soc_sw_init`（wmt_ic_soc.c:1014-1049） | **0x04 BTIF_FULL**（L1031-1033 覆盖） | `pWmtHifConf->hifType==BTIF`（L1014） | init_table_1_2 / init_table_4 脚本，10ms 等芯片切机制，L1034-1036 再置 EN=1；之后 init_table_5 及 patch 下载走 FULL 帧（L2558） |
| S5 | L709-711 | 不变 0x04 | — | `STP_CONF_RDY=1`，STP 就绪 |

（后置：func_off / stp_deinit / HW_RST 会走 C 写点 / STP_CLOSE 重新清零，非本卡点范围。）

### 回归路径（func_on op 错误携带 WMT_OP_HIF_BIT）
```
opfunc_func_on → opfunc_pwr_on
   L897 if (u4InfoBit & WMT_OP_HIF_BIT) → TRUE ★
   → opfunc_hif_conf(pWmtOp)  (wmt_core.c:897-898)
       L877 memcpy(&gMtkWmtCtx.wmtHifConf, &pWmtOp->au4OpData[0], sizeof(WMT_HIF_CONF))
       // au4OpData[0]=drvType（BT=0 / WIFI=3 / WMT=4），把整个 WMT_HIF_CONF 从 drvType 开始覆盖
       → hifType 被写成 0/3/4（BT=0 会误成 WMT_HIF_UART，WIFI=3=WMT_HIF_MAX，WMT=4 越界）
```
之后 `wmt_core_stp_init`：
- L669 `WMT_HIF_BTIF==hifType` → **FALSE → MAND mode 段整体跳过**（f_mode 停在 S0 的 0）
- L684 `hw_check` → send_data 全分支不命中 → 返回 0 → "Tx REG_CMD fail!" → hw_check 返 -2 → stp_init 返 -4 → func_on 失败并 assert。
- 即便 hw_check 侥幸通过，L699 sw_init 的 L1014 `hifType==BTIF` 也 FALSE → **FULL mode 也永远不会设**（f_mode 一直 0）→ patch 下载/后续 STP 流量全挂。

## 6. 任务问题 #4 的明确答复

> sw_init 是否有独立的 mode 设置（绕过 wmt_core_stp_init 的 hifType 判断）？

**有独立的设置（wmt_ic_soc.c:1031-1033 直接发 `WMT_STP_CONF_MODE=MTKSTP_BTIF_FULL_MODE`），但并没有绕过 hifType 判断**：
1. 它被 wmt_ic_soc.c:1014 `if (WMT_HIF_BTIF == pWmtHifConf->hifType)` 门控，收到的正是同一个 `&pctx->wmtHifConf`（wmt_core.c:699），hifType 被污染时该块同样整体跳过 → FULL 也不设。
2. 它在 `wmt_core_stp_init` 中位于 hw_check **之后**（L684 hw_check → L699 sw_init），时序上救不了 hw_check。

> 那 hw_check 为什么还需要 MAND mode？

- **时序**：hw_check 是 host 与 CONN 芯片第一次真实通信，此时芯片侧 STP 固件尚未加载，只能收 MAND 短头帧（L2526-2545 直接 `sys_if_tx`，无 seq/ack/winspace/CRC16）；FULL 帧协议要等 hw_check 认到 chip id、`sw_init` 跑完 init 脚本 + 10ms 机制切换后才可用（L2558 分支）。
- **f_mode=0 语义**：没有任何 mode bit → `mtk_wcn_stp_send_data` 四分支全不命中 → 静默返回 0 → 读 chip id 失败。MAND 不是"锦上添花"，是 hw_check 的**必经模式**。
- **hw_check 依赖的确切 mode**：`MTKSTP_BTIF_MAND_MODE`（0x08，走 L2522 分支；SDIO 芯片则依赖 `MTKSTP_SDIO_MODE`）。它**不依赖** FULL mode。

## 7. 修复方向（与现状核对）

当前 wmt_exp.c:100-110 已带注释并默认 `u4InfoBit=0`（`wmt_lib_get_free_op` 清零整个 op），即 func_on/func_off **不带 HIF_BIT** 的修复已落到源码；hifType=BTIF 由 launcher 的 `WMT_IOCTL_SET_STP_MODE → HIF_CONF op`（wmt_dev.c:1875-1909）保证。若上板仍卡 hw_check，按此链核对：
1. launcher 是否真的先发了 HIF_CONF op 且 `wmt_lib_set_hif` 收到 `STP_BTIF_FULL`（→ hifType=2）；
2. 任何 `opfunc_pwr_on` 的入口（func_on / pwr_on / sw_rst）传入的 op 是否误带 `WMT_OP_HIF_BIT`（wmt_core.c:897）；
3. `wmt_core_stp_init` L669 断点时 `gMtkWmtCtx.wmtHifConf.hifType` 是否仍是 2。

---
分析文件（只读）：`/tmp/connsys_build/conn_soc/core/stp_core.c`、`/tmp/connsys_build/conn_soc/core/stp_core.h`、`/tmp/connsys_build/conn_soc/core/include/stp_core.h`、`/tmp/connsys_build/conn_soc/core/wmt_core.c`、`/tmp/connsys_build/conn_soc/core/wmt_ctrl.c`、`/tmp/connsys_build/conn_soc/core/wmt_ic_soc.c`、`/tmp/connsys_build/conn_soc/core/wmt_lib.c`、`/tmp/connsys_build/conn_soc/core/include/wmt_core.h`、`/tmp/connsys_build/conn_soc/include/wmt_exp.h`、`/tmp/connsys_build/conn_soc/linux/pri/wmt_dev.c`、`/tmp/connsys_build/conn_soc/linux/pri/wmt_exp.c`、`/tmp/connsys_build/conn_soc/linux/include/osal.h`。未改动任何文件、未编译。
