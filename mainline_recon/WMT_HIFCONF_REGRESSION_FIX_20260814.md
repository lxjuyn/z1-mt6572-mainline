# WMT HIFCONF regression fix — 修复报告

> 日期: 2026-08-14
> 性质: 纯源码修复 + 重编 + 反汇编验证, 未碰硬件/未刷写
> 根因报告: `HW_CHECK_FAIL_ROOTCAUSE_20260814.md`
> 状态: 已修编 + 验证, 待下次上板实测

---

## 1. 改动

文件: `/tmp/connsys_build/conn_soc/linux/pri/wmt_exp.c`
函数: `mtk_wcn_wmt_func_ctrl()` (WMT_OPID_FUNC_ON / FUNC_OFF op 构造)

**删除**: `pOp->op.u4InfoBit = WMT_OP_HIF_BIT;` (上轮 hiffix 加入, 约 L108)
**替换**: 上方的误导性注释改为说明 "func op 不应带 HIF_BIT, HIF conf 由
launcher 的 HIF_CONF op (WMT_IOCTL_SET_STP_MODE) 提供" 的注释。

### 为什么删
- func op 带 HIF_BIT → `opfunc_pwr_on()` (wmt_core.c:897-898) 调
  `opfunc_hif_conf()` (wmt_core.c:877) → 无条件用 FUNC_ON op 的
  `au4OpData[0..2]`(=type 3/6) 覆盖 launcher 写好的 `wmtHifConf` → `hifType`
  变非法(3/6 ≠ BTIF=2) → `wmt_core_stp_init()` L669-673 跳过
  `MTKSTP_BTIF_MAND_MODE` → `f_mode=0` → `mtk_wcn_stp_send_data()` 返回 0 →
  hw_check `Tx REG_CMD fail!(0) len (0,20)` → func-on 失败。
- launcher 正常时它开机先发 HIF_CONF op(完整合法 conf, hifType=BTIF),
  wmtd 顺序处理, 本无竞态; FUNC_ON op 本就不该带 HIF_BIT。

### 改动后该段源码
```c
	pOp->op.opId = opId;
	pOp->op.au4OpData[0] = type;
	/* FUNC_ON/FUNC_OFF op must NOT carry WMT_OP_HIF_BIT. opfunc_pwr_on()
	 * (wmt_core.c) calls opfunc_hif_conf() whenever u4InfoBit has
	 * WMT_OP_HIF_BIT, and opfunc_hif_conf() unconditionally overwrites
	 * gMtkWmtCtx.wmtHifConf with this op's au4OpData[0..2] (= type 3/6),
	 * clobbering the legal hifType=BTIF(2) that the launcher already wrote
	 * via the WMT_IOCTL_SET_STP_MODE -> HIF_CONF op. That made
	 * wmt_core_stp_init() skip MTKSTP_BTIF_MAND_MODE, so f_mode stayed 0 and
	 * mtk_wcn_stp_send_data() returned 0 (hw_check Tx REG_CMD fail). HIF conf
	 * is provided by the launcher's HIF_CONF op, not by func ops.
	 * wmt_lib_get_free_op() zeroes the whole op, so u4InfoBit defaults to 0.
	 */
	if (WMTDRV_TYPE_WIFI == type)
```

## 2. 备份

`/home/<user>/claude/6572/bridge_backups/wmt_exp.c_hiffix_remove_20260814_154731.bak`
(改前原文件, 含 buggy 行)

## 3. 重编

```
cd /home/<user>/claude/6572/fork_linux
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- M=/tmp/connsys_build KBUILD_MODPOST_WARN=1 -j4 modules
```
- 0 编译错误; 只有与上轮相同的既有 warning (CFG_WMT_LTE_COEX_HANDLING undef /
  missing-prototypes / modpost section mismatch)。
- 仅 `conn_soc/mtk_stp_wmt_soc.o` 重链 (bt_soc / wifi_soc 不含 wmt_exp.c)。
- 日志: `/tmp/connsys_build/hifconf_fix_build.log` (归档内亦有)。

## 4. vermagic 验证 (modinfo)

```
filename:   .../conn_soc/mtk_stp_wmt_soc.ko
vermagic:   7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8
depends:    mtk_btif_drv
```
与旧 ko 完全一致 (make 未设 LOCALVERSION, CONFIG_LOCALVERSION="-z1" 生效, 尾缀
`+` 来自 setlocalversion)。

## 5. 删除验证 (反汇编对比, arm-linux-gnueabihf-objdump -d)

`mtk_wcn_wmt_func_ctrl` 构造 op 段:

| 旧 (hiffix 版) | 新 (本版) |
|---|---|
| `f840: str r6, [r4]` (opId) | `f840: str r6, [r4]` (opId) |
| `f844: mov r2, #1` (HIF_BIT) | (已删) |
| `f848: str r3, [r4, #152]` (timeoutValue) | `f844: str r3, [r4, #152]` (timeoutValue) |
| `f850: stmib r4, {r2, r5}` → [r4+4]=1, [r4+8]=type | `f84c: str r5, [r4, #8]` → 仅 au4OpData[0]=type |

- **新函数内已无任何写 offset #4 (u4InfoBit) 的指令**; 只剩 opId@0、
  au4OpData[0]@8、timeoutValue@152 三处写。
- 函数体其余 diff 全部是分支目标偏移平移 (函数短了 4 字节)。
- 附带铁证: 错误打印常量 `mov r2,#121` → `mov r2,#123` = 新源码里
  `WMT_ERR_FUNC("wake up failed...")` 的 `__LINE__`(现第 123 行, 注释变长),
  证明目标文件确由改后源码编译, 非陈旧 .o。

## 6. 归档

`/home/<user>/claude/6572/bridge_backups/products/products_connsys_hifconf_fix_20260814_154850/`
```
├── conn_soc/
│   ├── mtk_stp_wmt_soc.ko   (新修复版, 369696 B)
│   ├── mtk_stp_bt_soc.ko    (未变)
│   └── mtk_wmt_wifi_soc.ko  (未变)
├── wmt_exp.c                (改动后源码)
├── wmt_exp.c_hiffix_remove_20260814_154731.bak  (备份)
├── hifconf_fix_build.log    (编译日志)
└── VERIFY_HIFCONF_FIX.md    (验证记录)
```

## 7. 上板预期 (下次开机 dmesg 验证)

1. **不应再出现** `WMT-CORE: WMT HIF already exist. overwrite! old (2), new (3/6)`
   行 (该行只在 func op 带 HIF_BIT 触发 opfunc_hif_conf 覆盖时打印; 现 func op
   不再带 HIF_BIT, launcher 的 HIF_CONF op 保持 hifType=BTIF)。
2. **应看到** `get hwcode (chip id) (0x6572)` (hw_check 恢复成功, 读 0x80000008)。
3. 后续: `patch total num = [n]` → `WMT turn on WIFI success` → `wlan0` + MAC。
4. 前提: launcher 存活且已发 `WMT_IOCTL_SET_STP_MODE` (HIF_CONF op); 若 launcher
   缺席仍会报 "no hif info" → 用根因报告 §6.3 兜底 (SOC 默认 BTIF), 本次未做。

## 8. 附注

- 未 git commit (主会话统一提交)。
- 未碰硬件/未刷写/未改其它文件。
