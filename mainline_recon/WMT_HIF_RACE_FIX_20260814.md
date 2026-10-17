# WMT HIF INFO 竞态修复 — mtk_wcn_wmt_func_ctrl 显式设置 u4InfoBit

日期: 2026-08-14
范围: 仅源码 + 重编 mtk_stp_wmt_soc.ko，未碰硬件/固件/刷写，未 git commit。
影响模块: `conn_soc` → `mtk_stp_wmt_soc.ko`（wmt_exp.c 改动，wmt_core/wmt_lib 未改）。

## 1. 故障现象

WiFi func-on 偶发失败，log:
```
[WMT-CORE][E]wmt_core_stp_init(648):WMT-CORE: no hif info!
[WMT-CORE][E]opfunc_pwr_on(918):WMT-CORE: wmt_core_stp_init fail (-1)
```

## 2. 根因（已逐行确认）

HIF 信息（`gMtkWmtCtx.wmtInfoBit |= WMT_OP_HIF_BIT` + `wmtHifConf`）的唯一注册点是
`opfunc_hif_conf()`（`conn_soc/core/wmt_core.c:862`），而它只在两种情况下被调用：

1. `WMT_OPID_HIF_CONF` op 走派发表（`wmt_core.c:234`，由用户态 `WMT_IOCTL_SET_STP_MODE` ioctl 构造，wmt_dev.c:1896/1901）；
2. `opfunc_pwr_on()`（`wmt_core.c:897`）：`if (pWmtOp->u4InfoBit & WMT_OP_HIF_BIT) opfunc_hif_conf(pWmtOp);`

`mtk_wcn_wmt_func_ctrl()`（`conn_soc/linux/pri/wmt_exp.c:84`）组装 FUNC_ON/FUNC_OFF op 时只写了
```c
pOp->op.opId = opId;
pOp->op.au4OpData[0] = type;
```
**从不设置 `pOp->op.u4InfoBit`**。

关键连带事实：`wmt_lib_get_free_op()`（`conn_soc/core/wmt_lib.c:982`）在返回 op 前
`osal_memset(&pOp->op, 0, osal_sizeof(pOp->op))` 清零（含 u4InfoBit）。因此在本源码（以及线上 .ko 编译态）下
`u4InfoBit` 恒为 0 → `opfunc_pwr_on` 永不调 `opfunc_hif_conf` → `gMtkWmtCtx.wmtInfoBit` 的
HIF_BIT 只能靠 `WMT_IOCTL_SET_STP_MODE`（launcher）先注册。

于是 func-on 成功与否完全取决于 launcher 的 SET_STP_MODE 与 WiFi func_on 的先后时序：
- launcher 先跑（HIF_BIT 已入 `gMtkWmtCtx.wmtInfoBit`）→ 成功；
- func_on 先跑（HIF_BIT 未注册）→ `wmt_core_stp_init()` 在 `wmt_core.c:647`
  `if (!(pctx->wmtInfoBit & WMT_OP_HIF_BIT)) { "no hif info"; return -1; }` → 失败。

即确定性竞态（时序窗口），不是偶发随机。旧 log 之所以"偶发成功"，是历史 .ko 无 memset 时
free 队列残留的 `u4InfoBit=HIF_BIT` 恰好被复用进 func_on op（纯竞态靠残留碰运气）。

## 3. 修复（最小正确改动）

在 `mtk_wcn_wmt_func_ctrl()` 组装 op 时显式携带 HIF_BIT（不再依赖残留）：

```c
pOp->op.opId = opId;
pOp->op.au4OpData[0] = type;
pOp->op.u4InfoBit = WMT_OP_HIF_BIT;   /* + 注释说明竞态根因 */
```

- `WMT_OP_HIF_BIT` 定义于 `conn_soc/linux/include/osal.h:61`（`WMT_OP_BIT(0) = 0x1`），
  `wmt_exp.c` 经 `include/wmt_exp.h → osal.h` 可见。
- 对所有 FUNC_ON/FUNC_OFF 都设置（`opfunc_pwr_off` 不读 u4InfoBit，FUNC_OFF 带上是无害对称）。
- 为什么对 FUNC_ON 正确（HIF 是 WiFi 的传输）：`opfunc_func_on`（wmt_core.c:990）→
  `opfunc_pwr_on` → 读 `pWmtOp->u4InfoBit` 决定是否注册 HIF。WiFi 走 SDIO HIF，必须带 HIF_BIT。
- 为什么覆盖无害：`opfunc_hif_conf` 会把 `au4OpData[0]`（=type）拷进 `wmtHifConf.hifType`。
  `wmtHifConf.hifType` 只在 `wmt_core_stp_init`(669) 与 `mtk_wcn_soc_sw_init`(1014) 里
  `== WMT_HIF_BTIF(2)` 判断（BTIF 专属 init）。WiFi type=3 ≠ 2 → 两处都跳过 BTIF 分支，
  与 SDIO 行为一致；实际 STP 传输由 `g_stp_if_type`（`mtk_wcn_stp_set_if_tx_type`）决定，不走 wmtHifConf。
- 备选方案评估：在 `wmt_lib_get_free_op()` memset —— **该 memset 本树已存在**，
  残留路径已被封死，剩余缺口就是 func_ctrl 不显式设置；因此只补 func_ctrl 一行即可，
  不需要再动 wmt_lib.c / wmt_core.c。

## 4. 改动文件

- `/tmp/connsys_build/conn_soc/linux/pri/wmt_exp.c`（唯一改动，`mtk_wcn_wmt_func_ctrl` +8 行含注释）
- 未改动: `wmt_core.c`、`wmt_lib.c`、wlan/、btif/。

## 5. 编译验证

命令（kernel 树根 `fork_linux` 下执行）:
```
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- M=/tmp/connsys_build KBUILD_MODPOST_WARN=1 -j4 modules
```
- exit=0；`wmt_exp.o` 重编、`mtk_stp_wmt_soc.ko` 重链（无新增 error，仅有既有 Wundef/modpost 警告）。
- 反汇编确认修复已进二进制（`mtk_wcn_wmt_func_ctrl`）：
  `str r6,[r4]`（opId @+0）→ `mov r2,#1; stmib r4,{r2,r5}`（u4InfoBit=1 @+4, au4OpData[0]=type @+8）。
- 三个 conn_soc 模块 vermagic 全为 `7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8`（未设 LOCALVERSION，与内核一致）。
- md5: `mtk_stp_wmt_soc.ko = b0c3fb59358f1ea03829ed7e3b1994d4`（改动前 de0c0e98…）。

## 6. 产物归档

`/home/<user>/claude/6572/bridge_backups/products/products_connsys_hiffix_20260814_1515/`
- `conn_soc/mtk_stp_wmt_soc.ko`（新, 2.0MB 级, vermagic 7.0.0-rc7-z1+）
- `conn_soc/mtk_stp_bt_soc.ko`、`conn_soc/mtk_wmt_wifi_soc.ko`（同批次未改动）
- `src_backup/wmt_exp.c_hiffix_fixed.c`（改动后源码）
- `src_backup/wmt_exp.c_hiffix_pre_20260814_151108.bak`（改动前源码）
- `hiffix_build.log`

另: 独立备份 `/home/<user>/claude/6572/bridge_backups/wmt_exp.c_hiffix_pre_20260814_151108.bak`

## 7. 部署提示（下次上板）

- 仅需替换 `mtk_stp_wmt_soc.ko`（insmod 顺序依赖 mtk_btif_drv，depends 见 modinfo）。
- 上板验证点：log 应出现 `WMT-CORE: WMT HIF info added`（opfunc_hif_conf 首注册）或
  `WMT HIF already exist. overwrite!`（launcher 已先注册），随后 func_on 不再报
  `no hif info`。
- 说明: 本次只修 "no hif info" 竞态。STP 实际传输（`g_stp_if_type`=SDIO/UART）仍需
  launcher/平台层把 `mtk_wcn_stp_set_if_tx_type` 配好（独立事项，见
  CONNSYS_FW_READINESS_20260814.md 的 mtk_stp_launcher 缺失项）。
