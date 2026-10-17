# WMT insmod 慢 (40s→113s) — 汇总判定 (2026-08-15)

汇总自:
1. `mainline_recon/WMT_SLOW_SCENE_20260815.md`(log 时序分析, 只读)
2. `mainline_recon/WMT_INIT_SOURCE_20260815.md`(源码逐行分析, 只读)

两份报告均存在且完整, 结论一致, 无矛盾。本文件为汇总判定。

---

## 1. WMT insmod 慢(40s→113s)的确切原因

**驱动侧零问题, 慢在"命令分发前 ~93s 空等"。**

- **WMT_init(module init)本身完全不慢**: 慢 log 里 113.259502s calling → 113.295401s returned, **35.9ms**。历次 5 份 log 全部 35.9~38.8ms, 零回归。
- **源码无 sleep**: WMT_init 全链(wmt_dev.c:2346)→ wmt_lib_init → wmt_plat_init → mtk_wcn_consys_hw_init → mtk_wmt_probe。**probe 18.2ms 是 DT/OF 解析 + pinctrl 失败路径 + 4× devm_regulator_get(纯查表不碰硬件) + reset/clk 解析 + driver-core 累计**, 源码内 0 个 udelay/msleep/usleep。
- **无 300ms 级固定等待**: 最大可能等待 = chipid 轮询上限 10×20ms=**200ms**(仅冷上电读不到 0x6572 时) + 固定 `msleep(40)`, 但**全在用户态 ioctl 触发的上电流程**(`/dev/stpwmt` → wmtd 线程 → opfunc_pwr_on → mtk_wcn_consys_hw_reg_ctrl), **不在 module init 内**。
- **113s 是命令被 shell 实际执行的时刻**: 慢 log boot4 中, cfg80211(113.135)→btif(113.194)→WMT(113.260)→下一模块(113.309), 脚本一旦开始 175ms 全跑完, 脚本内无停顿。
- **唯一的异常空隙在内核侧是 93.4s 纯静默**: boot4 内核时间线 19.72s(sync_state)→113.13s(cfg80211)之间 0 条 printk, 内核完全空闲, 无 deferred probe / printk 洪流 / panic。延迟只能发生在命令分发层。
- **最可能操作侧根因**: 39.5s 稳定性标记(`vusb/vmc/vmch: disabling`)被 commit **37a481a21**(2026-08-14 21:31, dts 改 vmc/vmch/vusb `regulator-always-on`)消除。20260814 每次 boot 都有该标记、WMT 落在 45/54/56s; 20260815 四次 boot 全部 0 条标记、WMT 落在 113.3s。操作者/流程失去"系统已稳定"门控信号, 等到 ~113s 才发命令。等待时长不固定(145055 log 也慢, 54s), 符合操作者判断逻辑而非内核定时。
- 次要可能(概率低, 无法完全排除): 命令早敲但 115200 无流控 + 偶发 RX overrun 丢字, 操作者 ~113s 重发。log 上无法与"命令就是 113s 发出"区分。

## 2. 与 regulator_ignore_unused / cmdline 的关系

- **cmdline 不含 regulator_ignore_unused**: 当前 cmdline = `console=tty0 console=ttyS0,115200n8 root=/dev/ram panic=0 ignore_loglevel clk_ignore_unused rdinit=/init`。
- **对 WMT 时序: regulator_ignore_unused 零影响**。
  - `devm_regulator_get`(probe 内)是纯 OF/phandle 查表, 与该 flag 无交互、不碰硬件, probe 18.2ms 不变。
  - 上电流程 `regulator_enable` 只差 <1ms(MT6323 vcn 稳压器无 ramp_delay/enable_time/off_on_delay)。
- **它真正的效果是消掉 39.5s 的 `vusb/vmc/vmch: disabling` 日志**——与 commit 37a481a21 的 always-on 改动**作用在同一信号上**。加了它会比现状更彻底地消除该门控信号。
- **注意区分**: 39.5s 标记消失的直接原因是 **37a481a21(dts always-on)**, 不是 regulator_ignore_unused(它本来就没配)。两条路径殊途同归地消灭了同一操作门控信号。若操作流程还拿这个标记当判据, 加 regulator_ignore_unused **不会改善反而雪上加霜**——门控应改用 shell 提示符 + 固定等待 >40s(见 §4)。

## 3. 是否影响 shell 卡死

**否。本 log 内"WMT_init 后 shell 卡死"是捕获超时假象, 不是已证实的回归。**

- 捕获跑满 1800s(14:05:39→14:35:39)在 boot4 的 113.3s 处恰好到点被 `capture_payload_end` 切断(`reader_status=124`=timeout 到点)。`calling BT_init`(113.309)之后只剩 ~0.01s 捕获时间, **无法判定 mtk_stp_bt_soc 是否挂死**。
- shell 本身从未慢: 所有 boot 的 `~ #` 都在 ~9.5s 出现, boot4 `echo SHELLOK` 立即回显; 210837 log 证明 ~9.5s 即可即时交互。
- 20260814 同模块 BT_init 8.5ms 正常加载, 是当前唯一正常参照。
- 若操作者在 live 终端确实看到 WMT_init 后无后续输出(超出捕获窗口), 那是**另一个独立现象**, 需重启一次更长窗口的捕获单独验证, 不要在本 log 上断言挂死。

## 4. 修复建议

1. **改操作门控(主线, 立竿见影)**: 不再用 `vusb/vmc/vmch: disabling` 当 boot 完成/稳定信号(已被 37a481a21 永久消除)。改为检测 shell `~ #` 提示符 + 固定等待 >40s(过 39.5s 危险点)后即发 connsys 命令 → 可回到 ~60s 内完成 WMT(对照 20260814 的 45/54/56s)。
2. **脚本首行打 uptime(一锤定音)**: `z1_modprobe_connsys.sh` 第一行加 `echo "CONNSYS_START uptime=$(busybox cut -d' ' -f1 /proc/uptime)"`, 直接看到脚本实际执行秒数; 或设 `export PS1='\w# $(busybox cut -d" " -f1 /proc/uptime)> '` 让提示符显示 uptime, 区分 H1(命令晚敲)与 H2(命令早敲被 console/tty 输入路径憋住)。
3. **确认操作口**: init 同时拉了 ttyGS0(USB ACM)getty 与 console getty。若操作者走 ttyGS0, UART log 的 93s 静默可能只是另一接口有活动而 ttyS0 无输出。
4. **如需实锤输入丢字**: 上板 `cat /proc/tty/driver/serial` 看 ttyS0 `oe:` overrun; `dmesg -n 1` 压 console 后重跑对比。
5. **BT_init 挂死(如确需单独验证)**: 重启捕获(加长 max_seconds)看脚本是否走完 `OK: mtk_stp_bt_soc.ko`; 若仍挂, 抓 `cat /proc/<pid>/stack` 或 ftrace 定位, 并对比 20260814 正常模块集与本次的差异。
6. **不建议加 `regulator_ignore_unused`**: 它不解决本次慢(慢在操作侧), 且会进一步消除门控日志, 与现有 always-on 改动同向。
7. 若改操作脚本/流程: 备份后 git commit + push(仓库公开, 禁密钥)。

## 5. 证据文件索引(供复核)

- 慢 log: `mainline_recon/z1_boot_mainline_20260815_140539.log`(boot4 行 5655-5681; 捕获头尾 1-14/5682-5688)
- 成功对照: `mainline_recon/z1_boot_mainline_20260814_124449.log`(boot#1 行 1431-1454, boot#2 行 3684-3705)、`_141135.log`、`_202853.log`、`_145055.log`、`_210837.log`
- WMT 源码链: `bridge_backups/products/products_connsys_modules_20260812_022121/mt6572-connsys-modules/conn_soc/`(wmt_dev.c:2346 init / wmt_lib.c:165 / wmt_plat_alps.c:312 / mtk_wcn_consys_hw.c:610 probe+229 reg_ctrl)
- regulator 晚清理机制: `fork_linux/drivers/regulator/core.c:6788-6899`(regulator_late_cleanup skip always-on @6811; 30s delayed work @6894-6895; regulator_ignore_unused flag @6841-6847 唯一消费点 @6852-6870)
- dts always-on: `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts:313-342`; consys 节点 supply×4 @191-194
- 修复 commit: `37a481a21`(2026-08-14 21:31:35 "fix: keep vmc/vmch/vusb always-on - stop ~39s preloader reboot loop")
- 验收标准: `mainline_recon/BOOT_STABILITY_FIX_20260814.md`、`BOOT_STABILITY_PLAN_20260814.md`
