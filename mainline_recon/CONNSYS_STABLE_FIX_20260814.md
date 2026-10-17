# CONNSYS func-on 稳定性修复 C+D 落地报告 — 2026-08-14

> 板子关机，纯源码/脚本改动 + 离线重编，未碰硬件。依据
> `FUNC_ON_FIX_OPTIONS_20260814.md` 推荐组合 **A + C + D**。方案 A（删 `wmt_exp.c`
> 的 `u4InfoBit=HIF_BIT` 回归行）已在此前落树；本报告记录 **方案 C（内核默认 BTIF）+
> 方案 D（脚本等 launcher 就绪）** 的落地与验证。
>
> 一句话结论：**C+D 已改完并重链，`mtk_stp_wmt_soc.ko` vermagic=`7.0.0-rc7-z1+`，
> 二进制反汇编确认两处默认值已进机器码；脚本语法通过。A+C+D 三方案现全部在树。**

---

## 1. 改动清单

### 方案 C-1 — `wmt_core_init()` 默认 BTIF 配置（`conn_soc/core/wmt_core.c`）

位置：`wmt_core_init()`（约 L268）内，`osal_memset(&gMtkWmtCtx,...)` 之后、
`return 0` 之前。改动 2 行 + 注释：

```c
/* MT6572 SOC: BTIF is the only HIF ... */
gMtkWmtCtx.wmtInfoBit |= WMT_OP_HIF_BIT;
gMtkWmtCtx.wmtHifConf.hifType = WMT_HIF_BTIF;
```

- 常量确认：`WMT_OP_HIF_BIT = WMT_OP_BIT(0)`（`linux/include/osal.h:61`）；
  `WMT_HIF_BTIF = 2`（`core/include/wmt_core.h:103`）。
- 效果：`wmt_core_stp_init()` 的 L647 "no hif info" 检查（`wmtInfoBit & WMT_OP_HIF_BIT`）
  在 launcher 缺席/迟到时也通过；L669 `hifType==BTIF` 分支命中 → `f_mode |= MTKSTP_BTIF_MAND_MODE`。
- launcher 稍后发 SET_STP_MODE 的 HIF_CONF op 只是重复写同值，`opfunc_hif_conf()` 打印
  `WMT HIF already exist. overwrite! old(2),new(2)`（无害，见 `wmt_core.c:870`）。

### 方案 C-2 — `g_stp_if_type` 默认 BTIF（`conn_soc/linux/pri/stp_exp.c`）

位置：`stp_exp.c:71` 静态初始化。改动 1 行 + 注释：

```c
static ENUM_STP_TX_IF_TYPE g_stp_if_type = STP_BTIF_IF_TX;   /* 原 STP_MAX_IF_TX */
```

- 常量确认：`STP_BTIF_IF_TX = 2`（`include/stp_exp.h:87`）。
- 选择说明：任务给了"改静态初值"或"在 `wmt_lib_init` 调 `mtk_wcn_stp_set_if_tx_type()`"
  两个选项。选**改静态初值**——最小改动（1 行、不新增跨文件调用），且确定性最强
  （模块加载即生效，先于任何代码）；launcher 之后经 `wmt_lib_set_hif()` 写同值，
  无冲突。
- 效果：`mtk_wcn_sys_if_tx()`（`stp_exp.c:94-105`）在 launcher 缺席时也分发到
  `stp_btif_if_tx`（STP_OPEN 时由 `mtk_wcn_stp_register_if_tx(BTIF,...)` 注册，
  只依赖 `mtk_btif_drv.ko` 先 probe，与 launcher 无关），STP 任何 TX 不再恒 -1。

### 方案 D — 脚本等 launcher 就绪（`/home/<user>/claude/6572/scripts/z1_modprobe_connsys.sh`）

- 在"启动 launcher"段（原 L69-82）之后新增 "6b. 等 launcher 就绪" 逻辑。
- 用 `LAUNCHER_PRESENT` 标记 launcher 二进制是否存在；仅存在时才等待。
- 等两个 dmesg 就绪标志（busybox 兼容：`while` + `sleep 1` + `dmesg | grep -q`）：
  1. `WMT HIF info added`（`wmt_core.c:874` `opfunc_hif_conf` 处理完 HIF_CONF op 必打，
     ERR 级）——表示 SET_STP_MODE 已生效、`wmtInfoBit` 已置；最多轮询 20s。
  2. `patch total num`（`wmt_ic_soc.c:1071` `sw_init` 里 WARN 级）——表示 launcher
     首次上电（LPBK func-on + patch 下载）已完成；最多轮询 15s。
- 等待超时只打警告不阻塞后续流程：内核已有 C 方案默认 BTIF 兜底，func-on 不会因
  竞态窗口撞 "no hif info"。
- `bash -n` 语法验证通过。

## 2. 编译验证

命令（不改 LOCALVERSION，vermagic 保持 `7.0.0-rc7-z1+`）：

```
cd /home/<user>/claude/6572/fork_linux
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- M=/tmp/connsys_build KBUILD_MODPOST_WARN=1 -j4 modules
```

结果：`make rc=0`；`wmt_core.o`、`stp_exp.o` 重编，`mtk_stp_wmt_soc.ko` 重链，无编译错误
（仅既有 pre-existing warnings：`CFG_WMT_LTE_COEX_HANDLING` 未定义、modpost 缺
`MODULE_DESCRIPTION` 等，均非本次引入）。

```bash
$ modinfo mtk_stp_wmt_soc.ko
vermagic:   7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8   ← 正确
depends:    mtk_btif_drv
name:       mtk_stp_wmt_soc
```

二进制反汇编确认（不是只编译过，是默认值真的进了机器码）：

```asm
; wmt_core_init()  → gMtkWmtCtx 偏移核对：eDrvStatus[8] 占 0x20 → wmtInfoBit@0x20, wmtHifConf.hifType@0x24
str r2, [r4, #36]   ; r2=2 → wmtHifConf.hifType = WMT_HIF_BTIF(2)
orr r3, r3, #1
str r3, [r4, #32]   ; wmtInfoBit |= WMT_OP_HIF_BIT(1)

; stp_exp.o .data: 02000000  → g_stp_if_type 静态初值 = 2 (STP_BTIF_IF_TX)，原为 3 (STP_MAX_IF_TX)
```

方案 A 复核仍在树：`wmt_exp.c` 的 FUNC_ON/FUNC_OFF op 不再设 `u4InfoBit`（注释 L100-110
说明根因），`wmt_lib_get_free_op()` memset 保证 `u4InfoBit` 恒 0 → `opfunc_pwr_on()` 不再
调 `opfunc_hif_conf()` 覆写合法 hifType。

## 3. 备份与归档

- 改前备份（`bridge_backups/`，时间戳 20260814_161550）：
  - `wmt_core.c_connsys_c_20260814_161550.bak`
  - `stp_exp.c_connsys_c_20260814_161550.bak`
  - `z1_modprobe_connsys.sh_connsys_d_20260814_161550.bak`
- 产物归档：`bridge_backups/products/products_connsys_stable_20260814_161743/`
  - `mtk_stp_wmt_soc.ko`（369696 B，vermagic `7.0.0-rc7-z1+`）
  - 改动源：`wmt_core.c` / `stp_exp.c` / `z1_modprobe_connsys.sh`
  - 备份 3 件 + 编译日志 `connsys_cd_fix_build.log`

## 4. 上板预期（C+D 落地后）

1. **launcher 缺席/迟到时手动 `echo 1 > /dev/wmtWifi`**：不再见 `WMT-CORE: no hif info!`
   （L647 因默认 `wmtInfoBit|=HIF_BIT` 通过）；hw_check 走 MAND 分支（`f_mode|=0x8`）
   应见 `get hwcode (chip id) (0x6572)`；随后 sw_init 的 `srh_patch` 无 launcher 应答
   → 2s 超时 → `patch_info_prepare` -1 → sw_init -6 → func-on 失败。**这证明 STP 配置层
   已完全自足，残余失败仅限 patch 元数据（launcher 的职责）。**
2. **launcher 正常 + 即刻 func-on**：不再撞竞态窗口；可能看到
   `WMT HIF already exist. overwrite! old(2),new(2)`（无害，默认值与 launcher 同值）。
3. **自动化链**：`z1_modprobe_connsys.sh` 加载完成后，launcher 已完成 SET_STP_MODE +
   首次上电（等到了两个 dmesg 标志），外部 func-on 可立即执行，不撞
   "already powered on"/"not ready"。

## 5. 局限 / 风险（与方案评估报告一致）

- C 只消除"时序依赖"，**不能替代 launcher**：launcher 完全缺席时首次 func-on 仍死在
  `srh_patch` 2s 超时（patch 文件名由 launcher ioctl 喂，内核不自知）。
- C 旁路了 L647 "no hif info" 保护：本模块只 build 给 Z1（MT6572 唯一 BTIF），编译期
  默认可接受；若复用 combo 芯片需加 `wmt_ic_ops_soc` 判断。
- D 的等待是近似观测（SET_STP_MODE 是 fire-and-forget，"WMT HIF info added" 表示
  HIF_CONF 已处理而非整个 SET_STP_MODE 完成——对 func-on 而言前者就是正确门槛）。
- 待上板实测项：BT func-on / WiFi func-on 各一轮、launcher 缺席一轮、自动化链一轮。

## 6. 相关文件

- 改动：`/tmp/connsys_build/conn_soc/core/wmt_core.c`、
  `/tmp/connsys_build/conn_soc/linux/pri/stp_exp.c`、
  `/home/<user>/claude/6572/scripts/z1_modprobe_connsys.sh`
- 产物：`/home/<user>/claude/6572/bridge_backups/products/products_connsys_stable_20260814_161743/`
- 备份：`/home/<user>/claude/6572/bridge_backups/*_connsys_c_20260814_161550.bak`、
  `/home/<user>/claude/6572/bridge_backups/z1_modprobe_connsys.sh_connsys_d_20260814_161550.bak`
- 依据：`/home/<user>/claude/6572/mainline_recon/FUNC_ON_FIX_OPTIONS_20260814.md`
