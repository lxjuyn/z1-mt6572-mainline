# WMT_init 每步耗时来源 + regulator_ignore_unused 影响 — 只读源码分析 (2026-08-15)

> 只读分析，未改任何文件。源码树: `bridge_backups/products/products_connsys_modules_20260812_022121/mt6572-connsys-modules/conn_soc/`（任务给的 `/tmp/connsys_build/` 不存在，本树是唯一的 conn_soc 全量源码 git 树，内带 `.o/.ko`，即实际编译树）。内核侧源码用 `/home/<user>/claude/6572/fork_linux`。

---

## 0. TL;DR

1. **WMT_init（module init，`wmt_dev.c:2346`）本身没有任何固定 sleep**。实测 35.9~38.8ms（历次 log 恒定），大头是 `mtk_wmt_probe` 的 **18.2ms**（`probe of 18070000.connectivity returned 0 after 18216 usecs`），其余是 `register_chrdev_region`/`cdev_add`/`class_create`/`device_create` sysfs 开销 + `WMT_SOC.cfg` 文件读。
2. **wmt_plat_init / mtk_wcn_consys_hw_init 路径内 0 个 sleep**。所有延时（`udelay(150)`/`udelay(10)`/`msleep(40)`/chipid 轮询 `msleep(20)`×10）**全部**在 `mtk_wcn_consys_hw_reg_ctrl(on)`（上电流程）里，且该流程**不在 module init 里**，由用户态 ioctl（`/dev/stpwmt`，`WMT_IOCTL_FUNC_ONOFF_CTRL`/`SET_STP_MODE` → `WMT_OPID_PWR_ON` op → wmtd 线程 → `opfunc_pwr_on`）触发。
3. **`regulator_ignore_unused` 对 WMT 的 regulator `get` 时序零影响**（`devm_regulator_get` 是纯 OF/查表）；只影响 boot 后 ~39.5s 的 regulator 晚清理是否发生，且对 WMT 后续上电的 `regulator_enable` 也只有 <1ms 级间接差异（MT6323 vcn 稳压器无 ramp_delay/enable_time/off_on_delay）。
4. **无 300ms 级固定等待**。最大可能等待是 chipid 轮询上限 **10×20ms=200ms**（仅当 `0x18070008` 冷上电读不到 `0x6572` 时出现，代码注释标"mt6572 上已知非致命"），外加固定的 `msleep(40)`。
5. 任务背景里"WMT_init 含 wmt_core_stp_init（STP_OPEN + hw_check）"**与源码不符**：`wmt_core_stp_init`（`wmt_core.c:636`）只在 `opfunc_pwr_on`（`wmt_core.c:882`，`WMT_OPID_PWR_ON` op 处理器）里被调用，由用户态 ioctl 触发，不在 module init 上下文里。"40ms"这个数更可能是上电流程末尾固定的 `msleep(40)`。

---

## 1. WMT_init（module init）全链分解

`conn_soc/linux/pri/wmt_dev.c:2346` `WMT_init()`：

| 步骤 | 位置 | 内容 | 耗时来源 |
|---|---|---|---|
| chrdev 准备 | wmt_dev.c:2357-2379 | `stp_drv_init()` + `register_chrdev_region` + `cdev_add` + `class_create("stpwmt")` + `device_create` | sysfs/类创建设备注册，ms 级 |
| **`wmt_lib_init()`** | wmt_dev.c:2400 / wmt_lib.c:165 | 见 §2 | 主导项（含 probe 18.2ms） |
| dbg/proc 注册 | wmt_dev.c:2405-2414 | `wmt_dev_dbg_setup` / `wmt_dev_proc_for_aee_setup` / `wmt_lib_register_thermal_ctrl_cb` / `wmt_dev_bgw_desense_init`（netlink socket） | proc/文件系统注册，µs-ms |
| work 初始化 | wmt_dev.c:2418-2419 | `osal_sleepable_lock_init` + `INIT_WORK(&gPwrOnOffWork)` | 内存操作，µs |

> 实测模块 init = 35.9~38.8ms 恒定（`WMT_SLOW_SCENE_20260815.md` 已核对 5 份 log，35.9/36.1/37.2/38.1/38.8ms）。**module init 内不含 STP_OPEN / hw_check / msleep(40)**——上电 op 是用户态发的，见 §5。

## 2. wmt_lib_init（wmt_lib.c:165）每步耗时来源

| 步骤 | 位置 | 内容 | 耗时 |
|---|---|---|---|
| `wmt_conf_read_file()` | L177 / wmt_conf.c:466 | 拼路径 `/system/etc/firmware/WMT_SOC.cfg`（wmt_conf.h:33-34），`wmt_dev_patch_get`（wmt_dev.c:1499）→ `wmt_dev_read_file`（wmt_dev.c:1437，filp_open+内核读）+ `wmt_conf_parse` | 纯文件读+解析。ramfs 上 µs-ms，**无 sleep/重试**；文件不存在则快速失败并置 `cfgExist=0` |
| `osal_thread_create()` | L189 / osal.c:321 | `kthread_create("mtk_wmtd")` | 建 task，µs-ms |
| `wmt_core_init()` | L198 / wmt_core.c:268 | memset + 4 项 `eDrvStatus` 循环 | 内存操作，亚 µs |
| lock/event/queue 初始化 | L205-238 | 4 个锁、2 个 op 队列（`RB_INIT` WMT_OP_BUF_SIZE）、event/signal | 全内存，µs |
| **`wmt_plat_init()`** | L241 / wmt_plat_alps.c:312 | 见 §3 | **~18.2ms（实测，probe 占大头）** |
| `osal_thread_run()` | L258 / osal.c:333 | `wake_up_process` | µs |
| 回调注册 + `mtk_wcn_wmt_system_state_reset()` | L265-276 / wmt_exp.c:434 | 4 个回调指针赋值 + 2 个 memset | µs |

## 3. wmt_plat_init（wmt_plat_alps.c:312）+ mtk_wcn_consys_hw_init（mtk_wcn_consys_hw.c:610）

`wmt_plat_init`：`wmt_plat_soc_co_clock_flag_set` + `mtk_wcn_cmb_stub_reg`（struct 存储）+ `wakeup_source_init`/`mutex_init` + **`mtk_wcn_consys_hw_init()`** + `spin_lock_init`。**全链无 sleep。**

`mtk_wcn_consys_hw_init` 细分：

| 步骤 | 内容 | 耗时 |
|---|---|---|
| `of_find_compatible_node("mediatek,mt6572-consys")` | DT 节点查找 | µs |
| `of_iomap(node,0)` mcu_base（DT reg[0]=0x18070000）+ `of_iomap(node,1)` topckgen_base（reg[1]=0x10000000） | ioremap ×2 | µs |
| `ioremap(gConEmiPhyBase+0x80000, 343K)` + `memset_io(0, 343K)` | EMI 共享区清零（`CONSYS_EMI_AP_PHY_BASE=0x80080000`，`CONSYS_EMI_MEM_SIZE=343*KBYTE`） | µs-ms |
| `platform_driver_register(&mtk_wmt_dev_drv)` → **同步跑 `mtk_wmt_probe`** | 见下 | **实测 18.2ms** |
| 返回 0 | | |

**`mtk_wmt_probe`（mtk_wcn_consys_hw.c:104）内容与耗时**：
- `pm_runtime_enable` — 快
- `devm_pinctrl_get` → "Wmt cannot find pinctrl"（Z1 无 consys pinctrl，**已知正常非致命**，跳 `set_pin_exit`）— 快
- `of_parse_phandle("mediatek,pwrap-regmap")` + `of_find_device_by_node(&pwrap)` + `dev_get_regmap` — 快
- `devm_clk_get_optional("consysbus")` — **DT consys 节点无 clocks 属性 → 返回 NULL**，快
- **`devm_regulator_get ×4`（vcn18/vcn28/vcn33_bt/vcn33_wifi）** — 纯查表/OF phandle 解析，**不使能、不碰硬件**；失败非致命（只打日志）。DT 里 4 个 supply 全有（`mt6572-z1.dts:191-194` 指向 `mt6323_vcn*_reg`，mt6323.dtsi 已注册）→ 正常成功。快
- `devm_reset_control_get("connsys")`（DT `resets=<&watchdog MT6572_TOPRGU_CONNSYS_SW_RST>`）— 快，失败才让 probe 返回错误（Z1 上成功）
- `devm_pinctrl_get`（consys_pinctrl）→ 失败置 NULL，"no consys pinctrl (ok on mt6572)"，非致命 — 快

**结论**：probe 源码里没有任何 `udelay/msleep/usleep/轮询`。实测 18.2ms 是 DT/OF 解析 + pinctrl 失败路径 + 4× regulator 解析 + reset/clk 解析 + driver-core/genpd attach 开销的累计，**不是单个固定等待**。（次要可能：`devm_regulator_get` 恰逢 MT6323 regulator 并发注册时等 `regulator_list_mutex`，µs-ms 级。）

## 4. 关键：真正带 sleep 的上电流程在哪里（不在 module init）

所有延时集中在 `mtk_wcn_consys_hw_reg_ctrl(1, co_clock_type)`（mtk_wcn_consys_hw.c:229）：

| 等待 | 位置 | 说明 |
|---|---|---|
| `udelay(150)` | L252 | VCN18 使能后固定 150µs |
| `regmap_update_bits(pmic_regmap, 0x41C, ...)` | L263/267 | PMIC_WRAP VCN28 SW/HW 控制切换，µs |
| `regulator_enable(reg_VCN18)` / `(reg_VCN28)` | L247/L272 | MT6323 无 ramp_delay/enable_time/off_on_delay → 单次 pwrap regmap 写，**<1ms**（见 §7） |
| `reset_control_reset(rstc)` | L281 | mtk_wdt reset 写寄存器，µs |
| `mtk_wcn_consys_power_on()` → `pm_runtime_get_sync` | L283/L197 | 无 runtime-pm ops → 仅引用计数（genpd CONN 域 `KEEP_DEFAULT_OFF` + boot 时 `sync_state() pending` 保持 on，无长等待） |
| `udelay(10)` | L286 | 固定 10µs |
| `clk_prepare_enable(clk_infra_conn_main)` | L288 | **clk 为 NULL（DT 无 "consysbus"）→ IS_ERR_OR_NULL 直接 -EINVAL，无等待** |
| **chipid 轮询** `while(retry-- > 0) msleep(20)` | L292-302 | 读 `mcu_base+0x8`（`CONSYS_CHIP_ID_OFFSET=0x8`，期待 0x6572）。命中即 break（0ms）；**读不到则 10×20ms=200ms**。注释标 "known non-fatal on mt6572, FW download boots the core" |
| **`msleep(40)`** | L316 | 固定 40ms，OSC_EN 置位后稳定等待 |

调用链：`wmt_plat_pwr_ctrl(FUNC_ON)`（wmt_plat_alps.c:450）← `wmt_ctrl_hw_pwr_on`（wmt_ctrl.c:356）← `WMT_CTRL_HW_PWR_ON` ← `opfunc_pwr_on`（wmt_core.c:882，`WMT_OPID_PWR_ON`）← **用户态 ioctl 投 op**（`wmt_dev.c:1848` `WMT_unlocked_ioctl`）。`opfunc_pwr_on` 内部才调 `wmt_core_stp_init`（wmt_core.c:916 → L636，STP_OPEN + `wmt_core_hw_check` L781，全为寄存器读/查表，无 sleep）。

**"40ms"来源**：上电流程末尾的固定 `msleep(40)`（若 chipid 冷读命中，上电 ≈ 40ms + 若干 µs；若超时则 200+40ms）。这与"WMT_init 40ms"的数量级吻合，但**该步属于上电流程，不属于 module init**。

## 5. 任务背景修正：STP_OPEN + hw_check 不在 module init 里

- `wmt_core_stp_init` 的唯二调用点：`opfunc_pwr_on`（wmt_core.c:916）和 `opfunc_sw_rst`（wmt_core.c:1821），都是 **wmtd 线程处理 op** 时跑的。
- module init 内 `wmt_lib_init` 只 `kthread_create` + `wake_up_process`，之后无任何 op 投递；`WMT_OPID_PWR_ON` 在整棵树只有 enum 定义（wmt_core.h:124），**没有内部投递点** → 上电/STP_OPEN 由用户态 ioctl 触发（`WMT_IOCTL_FUNC_ONOFF_CTRL` 或 `SET_STP_MODE` 走 `WMT_OPID_HIF_CONF` 后由用户态流程补 PWR_ON）。

## 6. regulator_ignore_unused 对 WMT 的影响（关键问题）

内核实现（fork_linux/drivers/regulator/core.c）：
- 全局 flag：`core.c:6841-6847` `__setup("regulator_ignore_unused", ...)`。
- 唯一消费点：`regulator_init_complete_work_function`（core.c:6852-6870）——设了 flag 就 `pr_warn("Not disabling unused regulators")` 并**整体跳过** `regulator_late_cleanup`；没设则在 `regulator_init_complete`（core.c:6872，`late_initcall_sync`）排的 **30s 延时 work**（≈39.5s）里，把所有"启用且零消费者"且非 always-on 的 regulator 逐个 `_regulator_do_disable`（这就是 `vusb/vmc/vmch: disabling` 日志的来历）。

对 WMT 的逐条判定：
1. **`devm_regulator_get`（probe 内）时序 —— 零影响**。get 是 OF phandle 解析 + 注册表查找，与该 flag 无任何交互，也不碰硬件。加不加 `regulator_ignore_unused`，probe 的 18.2ms 不变。
2. **上电流程 `regulator_enable` —— 只有 <1ms 级间接差异**。不设 flag 时，若 bootloader 已把 VCN18/VCN28 打开而 WMT 尚未使用，39.5s 晚清理会先关掉它们，之后 WMT 上电时 `regulator_enable` 要真实拉起 LDO；设 flag 后保持常开，`regulator_enable` 变纯引用计数。MT6323 vcn 稳压器（`mt6323-regulator.c:279/313`）`MT6323_LDO`/`MT6323_REG_FIXED` 宏（L63/L85）**均未设 ramp_delay/enable_time/off_on_delay** → 无论哪种路径差异都 <1ms，相对 `msleep(40)` 可忽略。
3. **结论**：`regulator_ignore_unused` **不改变 WMT 的任何时序**。它真正的效果是消掉 39.5s 的 `vusb/vmc/vmch: disabling` 日志——这恰好是 `WMT_SLOW_SCENE_20260815.md` 已定位的操作侧门控信号，加了这个 flag 会让该信号更彻底消失，**操作门控应改用 shell 提示符 + 固定等待 >40s**（该文档 §9 建议）。

## 7. 附录：关键常量与文件行号（供复核）

- 源码树：`bridge_backups/products/products_connsys_modules_20260812_022121/mt6572-connsys-modules/conn_soc/`（core/ wmt_lib.c / wmt_core.c / wmt_conf.c / wmt_ctrl.c；mt6572/ wmt_plat_alps.c / mtk_wcn_consys_hw.c；linux/pri/ wmt_dev.c wmt_exp.c；linux/pub/ osal.c）
- `CONSYS_CHIP_ID_OFFSET=0x8`、`CONSYS_EMI_AP_PHY_BASE=0x80080000`、`CONSYS_EMI_AP_PHY_OFFSET=0x80000`、`CONSYS_EMI_MEM_SIZE=343*KBYTE`（mt6572/include 头）
- WMT_SOC.cfg 路径：`/system/etc/firmware/`（wmt_conf.h:33-34）
- DT consys 节点：`fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts:175-195`（supply ×4 + pwrap-regmap + power-domains CONN + resets connsys）
- MT6323 regulator：`fork_linux/drivers/regulator/mt6323-regulator.c:279`（ldo_vcn28）、`:313`（ldo_vcn18），宏 L63/L85 无 delay 字段
- `regulator_ignore_unused`：`fork_linux/drivers/regulator/core.c:6841-6847`（flag），`:6852-6870`（work，唯一消费点），`:6872+`（30s 延时排程）
- 上板实测参照：`mainline_recon/WMT_SLOW_SCENE_20260815.md`（WMT_init 35.9ms 恒定、113s 慢是操作侧、39.5s 标记已被 commit 37a481a21 消除）

## 8. 回答任务四问（一句话版）

1. **wmt_lib_init 每步耗时**：`wmt_conf_read_file`=文件读（µs-ms，无 sleep）；`osal_thread_create`=kthread_create；`wmt_core_init`=memset；`wmt_plat_init`≈18.2ms（全部来自 probe 的 DT/OF/regulator 解析开销累计，**源码无 sleep**）。
2. **wmt_plat_init/hw_init 长 sleep**：**无**。0 个 udelay/msleep/usleep。
3. **regulator_ignore_unused 影响**：对 `devm_regulator_get` 时序**零影响**；只消掉 39.5s 晚清理及其日志，对 WMT 上电 `regulator_enable` 仅 <1ms 级间接差异（MT6323 无 delay 字段）。
4. **300ms 级等待**：**没有固定 300ms 等待**。最大可能 = chipid 轮询超时 10×20ms=**200ms**（仅冷上电读不到 0x6572 时）+ 固定 `msleep(40)`；`clk_prepare_enable` 因 clk=NULL 实际 0 等待。上电流程在用户态 ioctl 触发，不在 module init。
