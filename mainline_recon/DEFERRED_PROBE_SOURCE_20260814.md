# Z1 (MT6572) — deferred probe / initcall 卡死 只读源码分析

> 日期: 2026-08-14 · 只读分析, 未改任何文件
> 分析对象: `fork_linux/drivers/base/dd.c` (7.0-rc7) + 板级 dts + `mainline_recon/z1_boot_*.log` 实机 boot log

---

## 0. TL;DR

1. **`deferred_probe_initcall` 从没阻塞过**: 所有实机 log 里它都在 late_initcall 阶段(~9.19s)跑完, 耗时 67~75ms, 恒返回 0。**没有任何 initcall 被 deferred probe 卡住。**
2. **-517 (EPROBE_DEFER) 设备只有 3 个**: `13000000.syscon`(mfgcfg, 已解决)、`1000f000.pwrap`(已解决)、`adc-battery`(旧内核永久 defer)。
3. **卡死与 `adc-battery` 永久 defer 强相关**:
   - 内核 **#90** (`g2cd361bd40c7-dirty`, 8/13 18:58 编): `mt6323_diagnostic_devs` 里**没有 auxadc cell**(见提交 `a00567cbe` diff)→ `mt6323-auxadc` 设备根本不存在 → `adc-battery` 拿不到 IIO channel → **永久 -EPROBE_DEFER**。5/5 次 boot 全挂(3 次静默卡死, 2 次 VFS root panic)。
   - 内核 **#91** (`7.0.0-rc7-z1+`, 8/14 01:46 编, 含提交 `a00567cbe` 把 auxadc cell 加进 diagnostic 表): `mt6323-auxadc` 正常 probe → `adc-battery` 成功。9/10 次 boot 进 shell(剩 1 次是**独立的** initrd/rootfs 交接问题)。
4. **卡死发生在 initcall 阶段之后**: 静默卡死点在 `efi_shutdown_init` 返回后的 9.31s 处(下一个 late_initcall `of_fdt_raw_init` 的 `calling` 都没打出来), 是 CPU 级 stall, 不是某个 initcall 函数在跑。内核 #90 全挂、#91 不挂, 说明去掉"永久 defer"后卡死消失 —— 但静态分析无法 100% 证明因果(详见 §3.4)。
5. **电源树无 supply 环**: MT6323 全部 regulator 没有 `regulator-supply`/`vin-supply`, MT6572 三个 power domain (DIS/MFG/CONN) 无跨域依赖、无 regulator 供电; `vcn18/vcn28/vcn33` 是叶级 supply, 无环。

---

## 1. dd.c deferred probe 机制 (`/home/<user>/claude/6572/fork_linux/drivers/base/dd.c`)

### 1.1 数据结构
- `deferred_probe_pending_list` / `deferred_probe_active_list` 两个链表 (L56-57), `deferred_probe_mutex` 保护 (L55)。
- `driver_deferred_probe_enable` (L157): **初始为 false**, 只有 `deferred_probe_initcall` 才打开。在这之前 `driver_deferred_probe_trigger()` 是空操作 (L178-179)。

### 1.2 `deferred_probe_initcall` (L345-376, `late_initcall`, L376) — 何时跑 / 怎么重试
```
late_initcall(deferred_probe_initcall)
```
流程 (L345-375):
1. `driver_deferred_probe_enable = true` (L350)。
2. 第一次 `driver_deferred_probe_trigger()` (L351): 把 pending 全部 splice 到 active, `queue_work(system_dfl_wq, deferred_probe_work)` (L188-196)。
3. **`flush_work(&deferred_probe_work)` (L353): 同步把 active 列表里的设备逐个重 probe** —— 这是 initcall 里可能阻塞的唯一路径(见 §1.4)。
4. `initcalls_done = true` (L354)。
5. 第二次 `driver_deferred_probe_trigger()` + `flush_work` (L363-364): 再兜一轮。
6. `CONFIG_MODULES=y` 时 `driver_deferred_probe_timeout` 默认 = 10 (L260-264), 调度 10s 后的 `deferred_probe_timeout_work` (L366-369), 到时打印仍挂在 pending 的设备。

### 1.3 重试引擎
- `driver_deferred_probe_trigger()` (L176-197): 每次**任意设备 probe 成功** (`driver_bound`, L479) 都会触发 → pending 移到 active + 排队 work。这就是"新资源就绪后旧 deferred 设备自动重试"的机制。
- `deferred_probe_work_func` (L82-130): 逐个取 active 列表设备 → `bus_probe_device()` 重试。重试仍返回 -EPROBE_DEFER 的, 由 `driver_probe_device` (L894-903) 重新塞回 **pending** 列表。
- `driver_deferred_probe_check_state` (L289-302): initcalls 完成后, 模块关闭时返回 -ENODEV; 设置了 timeout 且过期返回 -ETIMEDOUT; **否则永远返回 -EPROBE_DEFER**(CONFIG_MODULES=y 的 Z1 就是这种 → 永久 defer 不会自己死)。

### 1.4 什么情况会"卡住" initcall
`deferred_probe_initcall` 卡住 = `flush_work(&deferred_probe_work)` 卡住 = **active 列表里某个设备的 probe() 函数本身阻塞**(睡眠等一个永远不来的资源 / 自旋 / 死锁)。

关键区分:
- **返回 -EPROBE_DEFER 的设备**: probe 很快返回, 只是被重新排队, **不阻塞** initcall。
- **阻塞的 probe()**: 才卡 initcall。

另外 `really_probe` (L667-691) 在调 probe 前先 `device_links_check_suppliers(dev)` (fw_devlink); 供应商没好时返回 -EPROBE_DEFER(快速路径, 不阻塞)。

**实测结论**: Z1 所有 boot 里 `deferred_probe_initcall` 都在 9.19~9.21s 进、9.27~9.29s 回, 67~75ms, 恒返回 0 —— **dd.c 本身从没卡过**。

### 1.5 10s 后的 sync_state 消息 (不是卡死)
实机 log 在 ~19.7s 出现:
```
mtk-power-controller ... sync_state() pending due to 13010000.gpu / 14010000.larb / 18070000.connectivity
```
这是 `deferred_probe_timeout_work_func` (L305-321) 到点触发 `fw_devlink_probing_done()` 后的正常现象: 这几个 consumer 设备 probe 状态未定 (gpu/larb 在 DIS/MFG 域, connectivity 是外部 .ko), 供应商(电源控制器/regulator)的 sync_state 被压着。**不阻塞 boot**(成功 log 里都继续进 shell)。

---

## 2. boot log 里的 -517 设备清单

抓取范围: 2026-08-13 ~ 2026-08-14 全部 `z1_boot_mainline_2026081[34]_*.log` (内核 #90 与 #91)。

### 2.1 三次初始 probe -517 (两个内核一致)

| 设备 | 时间 | 原因 | 结局 |
|---|---|---|---|
| `13000000.syscon` (mfgcfg, `mediatek,mt6572-mfgcfg`) | 0.12s | `power-domains = <&spm MFG>`, 当时 MFG power domain (spm 电源控制器) 还没 probe | deferred_probe_initcall 时解决, `returned 0 after ~0.4ms` |
| `1000f000.pwrap` | 0.14s | 缺 reset 提供者 (watchdog@10007000) / fw_devlink 供应商未就绪 | 9.2s 解决, `returned 0 after 29~36ms` |
| `adc-battery` | 7.05s | `io-channels = <&mt6323_adc ...>` 拿不到 IIO channel (gab_probe 里 `devm_iio_channel_get` 返回 -EPROBE_DEFER) | **内核 #90: 永久 defer; 内核 #91: 9.2s 解决** |

### 2.2 `adc-battery` 的完整 -517 轨迹 (内核 #90 卡死 log `z1_boot_mainline_20260814_002746.log`)
```
[7.052661] probe of adc-battery returned -517 after 31 usecs   # gab_driver_init 直接 probe
[9.248049] probe of adc-battery returned 517 after 117 usecs   # deferred_probe_initcall 第1轮
[9.263967] probe of adc-battery returned 517 after 107 usecs   # deferred_probe_initcall 第2轮(initcalls_done 后)
[9.271206] initcall deferred_probe_initcall ... returned 0 after 67315 usecs
```
内核 #91 成功 log (`z1_boot_mainline_20260814_124449.log`) 对照:
```
[9.224649] probe of mt6323-auxadc returned 0 after 610 usecs   # ← 关键差异: auxadc 出现了
[9.262785] probe of adc-battery returned 0 after 968 usecs
[9.269849] initcall deferred_probe_initcall ... returned 0 after 68042 usecs
```

### 2.3 为什么 auxadc 在内核 #90 里"不存在"
`CONFIG_MFD_MT6323_NO_IRQ_DIAGNOSTIC=y` → `mt6397_probe` (drivers/mfd/mt6397-core.c:413-418) 走 `mt6323_diagnostic_devs` 分支。
- **内核 #90** (提交 `a00567cbe` 之前): `mt6323_diagnostic_devs = [mt6323-regulator]` 只有 1 个 cell(已用 `git show a00567cbe^` 验证) → **auxadc cell 从未注册 → `mt6323_adc` 设备不存在 → IIO channel 永远没有**。
- **内核 #91** (提交 `a00567cbe`): cell 表加进 `mt6323-auxadc` (mt6397-core.c:175-176), 注释明确写 "fixes Z1 battery EPROBE_DEFER loop"。auxadc probe 只需 `dev_get_regmap(dev->parent->parent)` 拿 pwrap regmap (drivers/iio/adc/mt6323-auxadc.c:257), 不依赖 MT6323 IRQ 域, 所以 diagnostic 模式下也能工作。

### 2.4 内核版本 ↔ 结果的完整对应表

| 内核 | 构建时间 | auxadc | adc-battery | boot 结果 |
|---|---|---|---|---|
| #90 `g2cd361bd40c7-dirty` | 8/13 18:58 | 无 (设备不存在) | 永久 -517 | **5/5 失败**: 3 次静默卡死(9.31s) + 2 次 VFS root panic |
| #91 `7.0.0-rc7-z1+` | 8/14 01:46 | probe 成功 | 成功 | **9/10 进 shell**, 1 次 VFS root panic (独立问题) |

---

## 3. Z1 dts 永久 EPROBE_DEFER 候选 + 是否卡 initcall

### 3.1 永久 defer 候选节点 (dts 视角)

| 节点 | 位置 | 依赖 | 状态 |
|---|---|---|---|
| `adc-battery` | mt6572-z1.dts:273-280, status="okay" | `io-channels = <&mt6323_adc BATSNS/BAT_TEMP>` (mt6323.dtsi:19-22), `monitored-battery = <&battery>` | **内核 #90 永久 defer**; #91 已解决 |
| `connectivity@18070000` | mt6572-z1.dts:175-195, status 无(默认 okay) | `vcn18/vcn28/vcn33_bt/vcn33_wifi-supply`, `power-domains=<&spm CONN>`, `mediatek,pwrap-regmap=<&pwrap>` | 由**外部 .ko** `mtk_stp_wmt_soc.ko` 绑定, initcall 阶段无驱动 → 设备挂着不 probe → 4 个 vcn regulator 的 sync_state 一直被压(见 log `sync_state() pending due to 18070000.connectivity`)。**不卡 initcall** |
| `wifi@180f0000` / `btif@1100e000` | z1.dts:197-201 / 142-173 | 外部 `wlan_gen2.ko` / `mtk_btif_drv.ko` | 同上, 外部模块 boot 后 insmod, 不卡 initcall |
| `connsys_pwr` (in-tree) | z1.dts:113-123 | 4 个 vcn supply + CONN 域 | **status="disabled"**, 不 probe |
| `touchscreen@5d` (goodix) | z1.dts:457-477 | AVDD28/VDDIO + EINT/RST GPIO | **status="disabled"**, 不 probe |
| `audsys` (AFE) | z1.dts:215-223, status="okay" | topckgen 时钟 + `interrupts=<GIC_SPI 0>` (占位非法 IRQ) | CONFIG_SND_SOC 已关 (提交 `0da8b21c4` "AFE probe hangs boot on Z1 (invalid GIC_SPI 0 IRQ)"), 无驱动 → 不 probe, 不卡 |

### 3.2 关键判断: 永久 defer 会不会卡 initcall
- **不会通过 dd.c 卡**: 永久 -EPROBE_DEFER 的设备只是反复被重排队, probe 每次 ~100us 就返回。`flush_work` 不会因它卡住。实测 `deferred_probe_initcall` 在有永久 defer 设备的内核 #90 里照样 67ms 返回 0。
- **唯一的阻塞来源**: probe() 函数内部自己阻塞(等锁/等硬件/死循环)。Z1 现存候选里没有这样的 probe 在 initcall 阶段跑(音频 AFE 因 GIC_SPI 0 已通过关 SND 规避)。

### 3.3 卡死点在 initcall 之后, 不在 initcall 里
静默卡死 log (#90, `002746`) 最后几行:
```
[9.316521] initcall efi_shutdown_init+0x0/0x58 returned -19 after 0 usecs
# ← 之后 9.5 分钟无任何输出, capture_end (超时)
```
成功 log (#91, `124449`) 同一位置继续:
```
[9.316521 附近] initcall efi_shutdown_init returned -19
[9.323452] calling of_fdt_raw_init ...
... clk_disable_unused / genpd_power_off_unused / regulator_init_complete / of_platform_sync_state_init ...
→ shell
```
即卡死发生在**最后一个 late_initcall 之后、下一个 late_initcall 打印之前** —— 此时没有 initcall 函数在执行, 是 CPU 级 stall (中断风暴 / 自旋死锁 / hard lockup 类), **不是"某 initcall 被 deferred probe 阻塞"**。

### 3.4 因果说明 (诚实的结论)
- 事实: 内核 #90 5/5 全挂、#91 9/10 成功; 两个内核唯一的 deferred 差异就是 auxadc/adc-battery 这条链。
- 强相关: 永久 defer 的 `adc-battery` + 缺失的 auxadc, 是 #90 所有失败 boot 的共性标志。
- 但静态分析**无法** 100% 证明"永久 defer 直接导致 CPU stall"。可能机制包括: (a) 未就绪的 IIO 图 / fw_devlink 让后续设备(如 battery 相关的 power-supply/thermal/regulator 路径)进入异常状态; (b) #90 与 #91 之间不止 auxadc 一处改动, 还混入了其他构建差异。**可验证方向**: 在内核 #91 上手动 `echo 1 > /sys/.../bind` 或临时把 `adc-battery` status="disabled" 再复现, 看是否还能触发 9.3s 卡死。
- 工程结论(与项目 commit 一致): 提交 `a00567cbe` ("enable mt6323-auxadc in diagnostic mode for Z1 battery") 已经把这条卡死链修掉, 后续 boot 实测也证实。

---

## 4. 电源树 supply 环检查 (vcn18 / vcn28 / vcn33)

### 4.1 检查对象
- `mt6323.dtsi` 全部 26 个 regulator (buck_vproc/vsys/vpa, ldo_*)。
- `mt6572.dtsi` 三个 power domain (DIS/MFG/CONN)。
- 消费方: `connsys_pwr`(disabled)、`connectivity@18070000`、`wifi@180f0000`、`btif@1100e000`。

### 4.2 结论: 无 supply 环
1. **MT6323 regulator 之间无链**: mt6323.dtsi 里**没有任何一个 regulator 带 `regulator-supply` 或 `vin-supply`** → 不存在 regulator→regulator 依赖, 也就没有环。
2. **power domain 无链**: `drivers/pmdomain/mediatek/mt6572-pm-domains.h` 里 DIS/MFG/CONN 三个域都只有 `sta_mask/ctl_offs/sram_pdn/bp_cfg/caps`, **没有 `.parent_dom` 跨域依赖、没有 regulator 供电**。CONN 域带 `MTK_SCPD_KEEP_DEFAULT_OFF` (L47)。
3. **vcn18/vcn28/vcn33 是叶级 supply**: 只被 connsys_pwr(disabled) 和 connectivity(外部 ko) 消费, 这两个都不再是任何 regulator/power-domain 的供应商 → 无回路。
4. **`mfgcfg`→spm / gpu→mfgcfg 是 DAG 不是环**: gpu 同时依赖 mfgcfg(clock)和 spm(MFG power); mfgcfg 依赖 spm(MFG power); spm MFG 域依赖 topckgen(clock)。方向无回边, 不是死锁环。`13000000.syscon` 的 -517 正是这条"等 MFG 域"的暂时 defer, 已解决。

### 4.3 运行时 regulator 报错 ≠ boot 环
实机 log (WMT/CONNSYS 上电阶段, 非 initcall):
```
vcn18: mode operation not allowed.
[WMT-CONSYS-HW][E] ... disable VCN_2V8 fail!
[WMT-CORE][E] ... pwr_on fail(-2)
```
这是**外部 conn_soc 驱动运行时调 `regulator_set_mode()`** 被 mt6323 regulator ops 拒绝 (`mode operation not allowed` = REGULATOR_CHANGE_MODE 不支持), 属于 CONNSYS 上电序列的功能问题, 与 boot 卡死/电源树环无关。

---

## 5. 结论

1. **deferred probe 机制本身没有卡死 Z1**: `deferred_probe_initcall` 恒在 ~9.2s 返回 0; 永久 defer 的设备(adc-battery)只会反复重排队, 不会阻塞 `flush_work`。
2. **历史上真正的卡死(内核 #90)与 auxadc/adc-battery 永久 defer 强相关**, 并已由提交 `a00567cbe`(auxadc cell 加进 `mt6323_diagnostic_devs`)修复; 内核 #91 实测 9/10 进 shell。
3. 卡死点是 **late_initcall 结束后的 CPU 级 stall**(efi_shutdown_init 之后), 不是 initcall 内阻塞; 静态分析无法给出最终硬件/中断级根因, 建议用 #91 + 对拍复现确认。
4. **电源树无 supply 环**(regulator 无 vin-supply 链、power domain 无跨域依赖、vcn18/28/33 为叶级)。
5. 当前(#91)残余偶发问题是**独立的 initrd/rootfs 交接**(`rootfs image is not initramfs ... XZ decoder` / `check access for rdinit=/init failed: -2` / VFS panic), 属 `linux,initrd-start/end` 与 LK 实际加载的 MTK ROOTFS payload 长度对不齐, 不是 deferred probe 问题。

---

## 6. 关键证据文件

- 机制: `/home/<user>/claude/6572/fork_linux/drivers/base/dd.c` (L345-376 `deferred_probe_initcall`, L82-130 work func, L176-197 trigger, L289-302 check_state, L305-322 timeout work)
- 修复提交: `/home/<user>/claude/6572/fork_linux/drivers/mfd/mt6397-core.c` L163-178 (`mt6323_diagnostic_devs`), 提交 `a00567cbe`
- auxadc probe: `/home/<user>/claude/6572/fork_linux/drivers/iio/adc/mt6323-auxadc.c` L249-283
- adc-battery probe: `/home/<user>/claude/6572/fork_linux/drivers/power/supply/generic-adc-battery.c` L154-238
- power domains: `/home/<user>/claude/6572/fork_linux/drivers/pmdomain/mediatek/mt6572-pm-domains.h` L13-54
- dts: `/home/<user>/claude/6572/fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts`, `mt6572.dtsi`, `mt6323.dtsi`
- 卡死 log: `/home/<user>/claude/6572/mainline_recon/z1_boot_mainline_20260814_002746.log` (内核 #90)
- 成功 log: `/home/<user>/claude/6572/mainline_recon/z1_boot_mainline_20260814_124449.log` (内核 #91)
- VFS panic(独立问题): `/home/<user>/claude/6572/mainline_recon/z1_boot_mainline_20260814_140521.log`
