# Z1 串口 shell 卡死 — 汇总判定 (2026-08-15)

> 只读汇总。整合三份报告:
> 1. `SHELL_HANG_SCENE_20260815.md`(场景日志分析, log `z1_boot_mainline_20260815_140539.log`)
> 2. `SERIAL_LOCAL_AUDIT_20260815.md`(本机配置对照审计)
> 3. `GETTY_CONSOLE_SOURCE_20260815.md`(源码级核对, 修正审计)
> 另参考上游联网调研 `SERIAL_SHELL_WEB_20260815.md`(审计的引用源)。

---

## 0. 报告完整性

三份报告**全部在位、无缺失**, 且已逐一复核关键证据仍成立:
- `z1_boot_mainline_20260815_140539.log` 存在(405925B/5686 行); 其 cmdline = `console=tty0 console=ttyS0,115200n8 root=/dev/ram panic=0 ignore_loglevel clk_ignore_unused rdinit=/init`, **与审计 §2.2 假设的 boot_mainline_connsys.img 同一 cmdline**(✓ 场景报告与审计针对同一条 kernel 线)。
- `z1_boot_emmc_full6_232325.log:935` 与场景 log 均实测 `ttyS0 ... is a ST16650V2`(✓ 源码核对 §3.1 的板级铁证)。
- `out/boot_mainline_connsys.img`(8-14 16:29)与 `out/ramdisk_mainline_connsys.gz`(8-14 22:43)均存在(但两者 ramdisk 不一致, 见 §6 风险)。
- `SERIAL_SHELL_WEB_20260815.md` 存在, 属被引用的上游调研, 不在本次三份要求内, 已间接覆盖。

---

## 1. 汇总判定(一句话)

**根因 = 两个本机特有放大器的叠加, 机制根因 A(printk 洪流饿死单核 UART RX)真实存在; 但 2026-08-15 14:05 那次"卡死"抓取实际未卡死——是 1800s 抓取窗口到期, 设备在 capture 结束瞬间仍在打 initcall。修复三件套 R1(去 ignore_loglevel+loglevel=3)/R2(getty 改 ttyS0)/R3(交互 shell 挪 ttyGS0)经源码级核对**全部成立、保持原优先级**, 其中 R1 仍是最大单一杠杆。**

三层理由(由表及里):
- **场景层(本次抓取)**: 未发生真实卡死。无回显 = 抓取窗口到期(`reader_status=124`×2 = GNU timeout 正常到期), 7 条命令全部即时回显, 尾部 0.18s 设备连打 14+ 条 initcall。WMT_init 实际耗时恒 28.4~30.6ms,"113s"是加载时点非耗时。
- **机制层(审计)**: 卡死机制真实存在的根因链 = 交互 shell 挂在 `/dev/console`(内核强制 O_NONBLOCK + 永不设 controlling tty)+ cmdline `ignore_loglevel` 全级别刷 console + WMT/connectivity insmod 数千行 printk + legacy console 同步写整段关本地 IRQ/关 IER(含 RX)→ 单核无 DMA 无流控下 RX FIFO(16B)溢出丢输入。
- **源码修正层(GETTY_CONSOLE_SOURCE)**: 机制成立, 但审计一个子机制叙述需修正——板上 ttyS0 实际是 autoconfig 检出的 **ST16650V2**(fifo=32/tx_loadsz=16), console TX 走 **16B FIFO 批量**, 不是 PORT_16550 逐字节; 不影响定性结论, 只改叙述。

---

## 2. 根因整合(三份并排)

### 2.1 场景事实层(SHELL_HANG_SCENE, 本次抓取 4 次 boot)

| 事实 | 证据 |
|---|---|
| "无回显" = capture 1800s 窗口到期 | capture_start 14:05:39 → capture_end 14:35:39 恰 1800s; `reader_status=124`×2 是 GNU timeout 到期码; 结束瞬间有 `[113.309434] ca…` 被截断 = 设备仍在输出 |
| 全部 7 条命令即时回显 | boot2/3/4 共 7 条 `echo SHELLOK*` 均 14B 内回显; 本次无任何"就绪后无回显"事件 |
| WMT_init = 28.4ms, 非 113s | `initcall WMT_init ... returned 0 after 28402 usecs`(跨 5 份日志 28.5~30.6ms 恒稳) |
| cfg80211 加载时点剧烈漂移 | 跨日志 23.74 / 35.79 / 44.86 / 48.16 / 53.59 / 53.85 / 56.36 / 94.02 / 113.14 s → "40s"只是典型值非开销 |
| shell 就绪 ~[19.7] 到 cfg80211 [113.1] 间 ~93.4s 静默窗口 | 内核零打印只夹 276B shell 输出; **两候选(A 操作者晚敲 / B insmod 内核侧阻塞 ~93s)日志无法 100% 区分**, 两者都不是永久卡死(恢复后 3 模块连续加载成功) |
| 设备在"疑似卡死"期间主动输出 | 尾部 0.18s 连打 cfg80211/BTIF/WMT 全部 initcall; 第 4 个 insmod(mtk_stp_bt_soc)进行中被窗口截断 = 脚本没跑完, 非卡住 |
| 全程无 WDT 复位 | 内核可调度、喂狗在跑 |

### 2.2 机制根因层(SERIAL_LOCAL_AUDIT, 按本机证据排序)

1. **[高置信, 本机特有] UART 交互 shell 挂在 /dev/console**: init line 36 `exec busybox getty -n -l /bin/sh 0 console vt100`; 内核开 /dev/console 强制 `O_NONBLOCK`(`tty_io.c:1927`)+ 永不设 controlling tty(`tty_io.c:2165`)→ 无回显/作业控制失效/EAGAIN 空转都可能 = 调研根因 C 在本机的真实形式。
2. **[高置信, 与调研一致] A — printk 洪流 + legacy console 同步刷屏饿死输入**: `serial8250_console_write` 整段 `uart_port_lock_irqsave` + `serial8250_clear_IER`(连 RX 中断一起关)+ 单核 + 无 DMA + 无流控 → WMT insmod 数千行 → RX FIFO 溢出丢字; echo/命令输出排在同一条 TX 后 → 无回显假死。
3. **[中置信] B — 无流控 overrun**(叠加 A)。
4. **[中置信, 板级见过] ttyGS0 输入路径被快写冲掉**(ACM 多 KB 粘贴 overrun)。
5. **[低置信] E — ctrl-S/XON**(MTK FC_NONE 下驱动侧关停; ctrl-Q 可排除)。

另两个放大器: cmdline `ignore_loglevel`(全级别强制上 console); init 丢掉 ttyGS0 等待循环(8-01 参考版 25×0.2s, 当前版无 → 早期 getty 静默失败, 用户实际落在 line 36 的 /dev/console)。

### 2.3 源码修正层(GETTY_CONSOLE_SOURCE, 对审计的量化口径修正)

| 项 | 审计说法 | 源码核对修正 | 结论 |
|---|---|---|---|
| /dev/console O_NONBLOCK + noctty | `tty_io.c:1926/2165` | 行号精确化 1927 / 2165; **功能确认**; /dev/ttyS0 直开走 default 分支两者皆无 | 不改判定 |
| console TX 路径 | PORT_16550 fifo=1 → 逐字节 putchar → 117ms/行 | 板上实际 **ST16650V2**(fifo_size=32, tx_loadsz=16, UART_CAP_FIFO), getty 开 ttyS0 后 use_fifo=true → 16B FIFO 批量写 | **该子机制在板上不成立, 仅叙述修正, 定性不变** |
| 整段关 IRQ + 清 IER | 逐字符 | `8250_port.c:3302-3387` 确认为**整条 printk 消息**关 IRQ + IER 写 0(含 RX); wait_for_xmitr 用 frame_time 超时, 无自锁 | 确认, 机制更强 |
| ignore_loglevel 边际 | "让 KERN_INFO/DEBUG 全量上 console" | **修正**: `CONFIG_CONSOLE_LOGLEVEL_DEFAULT=7` → INFO(6)/WARNING(4) 本来就全量上 console; ignore_loglevel 边际贡献仅 KERN_DEBUG(7) 及动态调试; 真正大头是默认 7 级 + WMT 热路径 printk | R1 依然成立、净削减量级依旧巨大 |
| MTK 无流控/无 DMA/共享 univ8250 console | — | 确认(FC_NONE 默认, dts 无 dmas, 无自有 console) | 确认 |
| R1/R2/R3 | 全部成立 | **全部源码层面成立, 且不依赖被修正的逐字节子机制** | 保持原优先级 |

---

## 3. 修复方案确认

| 方案 | 内容 | 改动面 | 源码级确认 | 判定 |
|---|---|---|---|---|
| **R1** | 去 `ignore_loglevel`, 加 `loglevel=3`(quiet) | 只改 cmdline, 0 编译, 只重打包 boot.img | `printk.c:1272-1289`(suppress_message_printing), `:1800-1802`(loglevel 写 console_loglevel); 掐掉 WARNING/NOTICE/INFO/DEBUG | **保持, 仍是最大单一杠杆** |
| **R2** | init line 36 getty: `getty -L -n -l /bin/sh 115200 ttyS0 vt100`(真 tty + CLOCAL + 显式波特率); 恢复 ttyGS0 等待循环 | 只重打包 ramdisk | `/dev/ttyS0` 直开不置 O_NONBLOCK、会设 controlling tty(tty_io.c:1935-1941 default 分支 / 2168-2169)→ 直接消除 §2.2-1 的本机特有根因; `-L` 属 busybox userspace, 无内核障碍 | **保持** |
| **R3** | 交互 shell 移到 ttyGS0(USB ACM), ttyS0 只留 kernel console | 结构性的(init 已按此设计, 靠 R2 修好等待循环) | USB ACM 输入不经 UART RX FIFO/IRQ 窗口, 与 printk 洪流解耦; 与 Z1 纪律(拔 USB 开机、内核起来后插线)兼容 | **保持(推荐)** |
| R4(可选) | WMT 热路径 printk 改 pr_debug/pr_info_ratelimited | 需重编 .ko | R1 已覆盖, 非必须 | 保留可选 |
| R5 | 维持 115200, 不加 -h | — | 与源码无冲突; C3/脚本全部按 115200 校准, 改动面大 | 维持 |

---

## 4. 实施顺序 + 上板验证点

### Step 0(前置核对, 必须)
- **先解决 img 与 .gz 不一致风险**(审计 §8): `out/boot_mainline_connsys.img` 内部 ramdisk(831839B)与 `out/ramdisk_mainline_connsys.gz`(2.2MB XZ)**大小/格式对不上** → 板上实际跑的可能是旧 init。改 R2 前必须从单一事实源重打包并核对 flash 镜像内部 ramdisk。
- 备份现状 boot.img + 相关 ramdisk 到 `bridge_backups/`(带时间戳)。
- 上板零改动核对: `cat /proc/tty/driver/serial` → ttyS0 行应显示 `type:16650V2`; `cat /proc/sys/kernel/printk` 确认当前 console_loglevel=7。

### Step 1(R1, 最高价值/最低风险, 0 编译)
- 改 cmdline 为 `console=tty0 console=ttyS0,115200n8 root=/dev/ram panic=0 loglevel=3 clk_ignore_unused rdinit=/init`(去掉 ignore_loglevel), 用现有 mkbootimg 重打包(CLAUDE.md §4a 命令, 只换 `--cmdline`)。
- **验证点**:
  1. `cat /proc/sys/kernel/printk` 显示 console_loglevel=3(不再是 7)。
  2. modprobe 洪流期间敲命令/`echo SHELLOK` → 即时回显。
  3. `cat /proc/tty/driver/serial` 对比 WMT insmod 前/后 ttyS0 行 `oe:`(overrun)不再暴涨。
- 通过即 `git add`+`commit`+`push`(改的是打包脚本/文档级, 同步上传)。

### Step 2(R2, 高价值, 只重打包 ramdisk)
- 改 init line 36 → `getty -L -n -l /bin/sh 115200 ttyS0 vt100`; 恢复 ttyGS0 等待循环(`while [ ! -e /dev/ttyGS0 ] && [ $i -lt 25 ]; do mdev -s; sleep 0.2; i=$((i+1)); done`, 照 `bridge_backups/products/products_ramdisk_ttygs0_loop_20260801_023151/init_original_ttygs0_ref`); line 26 ttyGS0 getty 加 `-L`。重打 512B 头 + cpio(照 `products_ramdisk_fw_20260810_013549/repack_ramdisk_fw.sh`), 再与 R1 cmdline 一起打包。
- **验证点**:
  1. `ps | grep -E 'getty|sh'` → 主交互 getty 挂在 ttyS0, 不是 console。
  2. shell 内 `tty` 命令 → 输出 `/dev/ttyS0`(有了 controlling tty)。
  3. 重负载交互(粘贴/连续命令)回显稳定。
- 通过即 git 上传。

### Step 3(R3, 结构性)
- 依赖 R2 的等待循环修好; 开机时**拔 USB**(preloader LineState 纪律), 内核 ready 后**再插 USB 线**接 ACM shell。
- **验证点**:
  1. `/dev/ttyGS0` 存在; `ls -la /dev/ttyGS0`。
  2. 上位机 `socat - /dev/ttyACM0,raw,echo=0` 或 busybox `microcom` 接上, 重负载交互(含多 KB 粘贴)不丢输入。
  3. ttyS0 只剩 kernel console 打印(洪流仍被 R1 压到 warn/err)。

### 复测建议(场景报告 §7, 用于 100% 定案 93.4s 静默窗口归属)
- 抓取侧加墙钟: `z1_capture_mainline.sh` 的 `tr` 流每行前缀墙钟或用 `ts`。
- 操作者敲 modprobe 前先 busybox `date`, 让设备自己盖时间戳。
- shell 就绪后立即敲 modprobe(不等待): 若 cfg80211 仍延迟 ~90s → 确认 insmod 内核侧阻塞(查 finit_module/kernel_read_file/module_mutex/usermodehelper 竞争); 若立即加载 → 确认本次是操作节奏。

---

## 5. 残余未定项 / 风险

| # | 项 | 状态 | 处置 |
|---|---|---|---|
| 1 | 93.4s 静默窗口归属(操作者晚敲 vs insmod 阻塞) | 无法从现有日志区分 | §4 复测建议, 加墙钟 |
| 2 | `boot_mainline_connsys.img` 与 `ramdisk_mainline_connsys.gz` 内部 ramdisk 不一致 → 板上跑的可能不是这份 init | 未解决 | Step 0 前置核对/重打包 |
| 3 | kernel 内残留旧 ramdisk 段(parse_boot.py 解出 `console=ttyMT1,921600n1 ... androidboot.selinux=permissive` 旧 LK/ATAG) | 镜像结构混入历史段 | 打包纪律对齐, 以单一事实源重打包 |
| 4 | `/dev/console` 的 O_NONBLOCK + noctty 在未修 R2 前的残余影响 | R1 只压洪流, 不解决 console 两怪 | 必须 R1+R2 一起上才完整消除 |
| 5 | ttyGS0 ACM 输入被快写冲掉(板级见过) | 即使 USB shell 大块粘贴也丢 | R3 后仍建议 byte-paced 或分批粘贴 |

---

## 6. 结论

- **根因判定**: 机制根因 A(printk 洪流 + legacy console 同步写饿死单核 UART RX)本机成立, 且被两个本机特有因素放大(交互 shell 在 /dev/console、cmdline 带 ignore_loglevel); 源码级核对修正了"逐字节"子机制(实际 ST16650V2 走 16B FIFO 批量)但不影响定性。
- **"卡死"判定**: 2026-08-15 14:05 抓取**未发生真实卡死**(窗口到期), 但前序会话的真实无回显卡死由 §2.2 机制解释, 修复仍必要。
- **修复确认**: R1(去 ignore_loglevel + loglevel=3)→ R2(getty ttyS0 + -L + 恢复 ttyGS0 等待)→ R3(交互 shell 挪 ttyGS0), 全部源码层面成立, 按此顺序实施; 上板验证点见 §4。

---

## 7. 证据行号索引(跨报告)

| 项 | 位置 |
|---|---|
| capture 窗口到期 / reader_status=124 | SHELL_HANG_SCENE §1, §6 #1 |
| 结束瞬间设备仍在输出 `[113.309] ca…` | SHELL_HANG_SCENE §5, §6 #2 |
| WMT_init 28402 usecs | SHELL_HANG_SCENE §4.1; 场景 log L5675-5679 |
| cmdline `... ignore_loglevel clk_ignore_unused ...` | SERIAL_LOCAL_AUDIT §2.2; 场景 log L 头 |
| /dev/console O_NONBLOCK / noctty | GETTY_CONSOLE_SOURCE §1.1/1.2(tty_io.c:1927/2165) |
| serial8250_console_write 整段关 IRQ + 清 IER | GETTY_CONSOLE_SOURCE §2.1(8250_port.c:3302-3387) |
| 板上 ttyS0 = ST16650V2(fifo=32/tx_loadsz=16) | GETTY_CONSOLE_SOURCE §3.1; emmc_full6 log:935 + 场景 log `[0.312085]` |
| autoconfig 覆盖 PORT_16550 → ST16650V2 | GETTY_CONSOLE_SOURCE §3.1(serial_core.c:2515-2519, 8250_port.c:864) |
| MTK 无流控/无 DMA | GETTY_CONSOLE_SOURCE §3.2/3.3(8250_mtk.c:411-418, mt6572.dtsi:425-432) |
| init getty 三行 / ttyGS0 等待循环回归 | SERIAL_LOCAL_AUDIT §2.1, §3 #3 |
| img vs .gz 不一致风险 | SERIAL_LOCAL_AUDIT §8 |
| R1 验证 / R2 验证 / R3 验证 | SERIAL_LOCAL_AUDIT §6, GETTY_CONSOLE_SOURCE §4 |
