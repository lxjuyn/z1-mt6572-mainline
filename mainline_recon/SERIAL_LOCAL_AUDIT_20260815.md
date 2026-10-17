# 串口 Shell 卡死 — 本机配置对照审计 (2026-08-15)

> 只读对照。依据: `mainline_recon/SERIAL_SHELL_WEB_20260815.md`(联网调研) + 本机解包 `out/ramdisk_mainline_connsys.gz` 的 init + `out/boot_mainline_connsys.img` cmdline + `fork_linux` 源码/.config。未改任何文件。

---

## 0. TL;DR

1. **网络调研的主根因 A(printk 洪流 + legacy console 同步刷屏饿死单核 UART 输入)在本机成立**, 且本机有两处**比调研假设更糟**的放大器:
   - cmdline 带 **`ignore_loglevel`** → 所有 printk 级别(含 KERN_INFO/KERN_DEBUG)全量刷到 console, 洪流最大化;
   - 8250_mtk 端口类型被强制 **`PORT_16550`(fifo_size=tx_loadsz=1)** → console 逐字节 putchar + 整段 `local_irq_save`, 正是调研引用的 kaiwan 117ms/行的最坏情况。
2. **但本机与调研前提有 3 处偏差, 其中之一可能就是"卡死/无回显"的直接元凶**:
   - 实际 UART 交互 shell 跑在 **`getty ... 0 console`(/dev/console)**, 不是 `getty ... ttyS0`。内核打开 /dev/console 时强制 **`O_NONBLOCK` 且永不设为 controlling tty**(tty_io.c), 这本身就能造成无回显/作业控制失效(调研根因 C 在本机以另一机制真实存在);
   - 当前 init **丢掉了 ttyGS0 等待循环**(8-01 参考版有 25×0.2s 等 /dev/ttyGS0), 早期 getty 很可能静默失败 → 用户实际落在线 36 的 `/dev/console` shell;
   - 板级实测("fast multi-kilobyte shell writes overran the small getty input path")证明 ACM 输入路径也会被快写冲掉(根因 B 的 USB 版)。
3. **给实施建议**: 先 `dmesg -n 1` 压 console 打印做上板验证(实锤 A); 然后 **R1 去掉 `ignore_loglevel`+加 `loglevel=3`(只改 cmdline, 0 编译)**, **R2 改 getty 用真设备 `ttyS0` + `-L`(只重打包 ramdisk)**, **R3 结构性把交互 shell 挪到 ttyGS0(USB ACM), ttyS0 只留 kernel console 打印**。R1+R2 组合即可大概率消除"卡死"。

---

## 1. 网络调研结论(原文一句话)

> 主因 A: WMT/connectivity insmod 的 printk 洪流在 legacy console=ttyS0 上同步逐字符刷屏, console_unlock 长持 + 单核本地 IRQ 被关 → UART RX FIFO 溢出 → 输入丢失; echo/命令输出又排在同一条 TX 后面 → 无回显/卡死。次要 B(无流控 overrun)、E(ctrl-S/XON)、C(getty 无 controlling tty)。

---

## 2. 本机事实对照

### 2.1 initramfs init 实际 getty 命令(解包 `out/ramdisk_mainline_connsys.gz`)

文件为 **512B MTK ROOTFS 头 + XZ 压缩 cpio**(`8816 8858 d0f2 2100 ROOTFS`, body 是 XZ)。解包后 `/init`(busybox 1.36.1):

```
line 26: busybox setsid sh -c 'exec busybox getty -n -l /bin/sh 115200 ttyGS0 vt100' </dev/ttyGS0 >/dev/ttyGS0 2>&1 &
line 27: busybox setsid sh -c 'exec busybox getty -n -l /bin/sh 0 tty0 vt100 2>/dev/null' </dev/tty0 >/dev/tty0 2>&1 &
line 36: exec busybox getty -n -l /bin/sh 0 console vt100 2>/dev/null   # PID1 兜底 shell
```

关键:
- **主交互 shell 设计在 ttyGS0(USB ACM), UART 路径只有 `console`(=/dev/console)兜底**, 并没有调研假设的 `ttyS0`。
- 三个 getty 都**没有 `-L`(CLOCAL)**; tty0/console 用 **波特率 0**(沿用现状, 即 console=ttyS0 的 115200)。
- **ttyGS0 getty 是后台 & 启动, 不等节点**: 开机早期单次 `mdev -s` 后立即 open /dev/ttyGS0, gadget 异步 probe 慢时静默失败。对比 `bridge_backups/products/products_ramdisk_ttygs0_loop_20260801_023151/init_original_ttygs0_ref`(8-01 参考版)有 `while [ ! -e /dev/ttyGS0 ] ... mdev -s; sleep 0.2`(最多 25 次≈5s)**当前版丢了这个等待**——疑似回归。

### 2.2 cmdline(从 `out/boot_mainline_connsys.img` strings 实测)

```
console=tty0 console=ttyS0,115200n8 root=/dev/ram panic=0 ignore_loglevel clk_ignore_unused rdinit=/init
```

- 与调研假设 `console=ttyS0,115200n8 panic=10 regulator_ignore_unused` **不同**: 本机多了 **`ignore_loglevel`**(所有 printk 级别强制上 console — 洪流放大器), panic=0, 还叠加了 `console=tty0`。
- `/dev/console` 绑定: 内核文档"**last console= wins for /dev/console**"→ `console=tty0 console=ttyS0` 的最后一个是 ttyS0 → **line 36 的 `console` shell 实际落 ttyS0**。
- `.config` 里 `CONFIG_CMDLINE=""`(526 行)→ cmdline 全部来自 mkbootimg。

### 2.3 8250_mtk.c / 8250 框架 console 输入问题(源码核对)

- **legacy console 确认**: `8250_mtk.c` 自身没有 `.console` 成员, 走 `8250_core.c` 的 `univ8250_console_write` → 通用 `serial8250_console_write`(`8250_port.c:3302`)。
- **逐字节 + 关 IRQ 确认**: `serial8250_console_write` 用 `uart_port_lock_irqsave(port, &flags)` 包住整段(清 IER → 逐字符 `serial8250_console_putchar`, `8250_port.c:3215-3217`); `use_fifo` 条件 `tx_loadsz>1` — 而 `8250_mtk.c` probe 强制 **`uart.port.type = PORT_16550`**(554 行附近), 查表 `PORT_16550 .fifo_size=1 .tx_loadsz=1`(`8250_port.c:65-68`)→ **console TX 就是调研引用的"单行 44 次 putchar/117ms"最坏情况**。整段 local_irq 关闭 → 单核上 RX IRQ 无服务窗口。
- **无 DMA**: dts uart0 无 `dmas` 属性(自查 mt6572.dtsi uart0@11005000 425-432 行), 虽 `CONFIG_SERIAL_8250_DMA=y` 但 DMA 未接 → PIO + RX IRQ, RX 缓冲仅 FIFO。
- **无流控**: `mtk8250_set_termios` 默认 `MTK_UART_FC_NONE`(c_cflag 无 CRTSCTS、c_iflag 无 IXON 时)→ 3 线调试 UART, 无硬件 RTS/CTS(调研根因 B 叠加成立)。
- **console 与 tty 同口**: 该口同时被 console 直写 + getty tty 使用, 锁互斥(调研根因 D), 符合。

### 2.4 busybox getty 版本与参数

- **BusyBox v1.36.1**(ramdisk 内实测), getty 支持 `-n -l -L -h -t -I -H -w -i -f`。`BAUD_RATE of 0 leaves it unchanged`(符合 line 27/36 的 `0`)。
- 当前参数 `-n -l /bin/sh`(跳过 login 直接 exec sh)语法正确, 但:
  - **没 `-L`(CLOCAL)**: 对真 UART, 3 线调试口 DCD 悬空, 无 CLOCAL 有挂断风险(busybox getty 内部是否自动 CLOCAL 未核, 属次要项, 建议显式加);
  - **作用在 `/dev/console` 上**: 见 §3.1, 这是与调研差异最大的点。

---

## 3. 网络前提 vs 本机实际的偏差(关键, 调研未覆盖)

| # | 调研假设 | 本机实际 | 影响 |
|---|---|---|---|
| 1 | 交互 shell 在 `getty 115200 ttyS0` | UART 交互 shell 实际是 line 36 `getty 0 console`(=**/dev/console**) | **内核打开 /dev/console 强制 `O_NONBLOCK`(`tty_io.c:1926`)且永不设为 controlling tty(`tty_io.c:2165`)**, busybox ML 经典警告原样命中 → 无回显/作业控制异常/读 EAGAIN 自旋都可能, 这就是调研根因 C 在本机的真实存在形式 |
| 2 | cmdline `console=ttyS0,115200n8 panic=10 regulator_ignore_unused` | `console=tty0 console=ttyS0,115200n8 ... **ignore_loglevel** clk_ignore_unused` | **`ignore_loglevel` 让 KERN_INFO/DEBUG 也全量上 console**, 比调研假设的洪流再大一个量级 |
| 3 | 假设 ttyGS0 路径可用 | 当前 init **丢了 ttyGS0 等待循环**(8-01 参考版有), 且 USB 平时不插 | 用户大概率没在 ttyGS0 shell 上, 而是在 /dev/console 上 |
| 4 | 波特率/流控是"次要因素" | MTK 端口 type=PORT_16550 → **fifo=1B 逐字节 console 写**, 刷新间隔每行最长 | 使 A 的量化(117ms/行)完全成立, 不弱于 RPi 案例 |
| 5 | 板级无实测 | MT6323_MFD 板级实测: "fast multi-kilobyte shell writes overran the small getty input path"(需 byte-paced 输入) | 即使 ACM 路径, 快写也会冲输入(根因 B 的 USB/缓冲版, 板子已见过) |

> 结论: 网络调研主根因 A 的判断方向正确, 但把"本机 shell 在 ttyS0"当既定前提; 本机实际 UART shell 在 **/dev/console**(O_NONBLOCK+无 controlling tty), 且 cmdline 有 `ignore_loglevel`。**这两个本机特有因素优先级应排在 A 的纯机制之前**——先修它们, 再谈 printk 限速。

---

## 4. 根因判定(按本机证据排序)

1. **[高置信, 本机特有] UART shell 挂在 /dev/console 上**: getty→sh 的 fd 被内核置 O_NONBLOCK 且无 controlling tty → 交互 shell 行为异常(无回显/作业控制失效/可能 EAGAIN 空转)。这直接解释"无回显/卡死", 且与"WMT insmod 后更明显"可以并存(A 放大)。
2. **[高置信, 与调研一致] A — printk 洪流 + legacy console 逐字节同步刷屏饿死输入**: `serial8250_console_write` 整段 `local_irq_save` + PORT_16550 tx_loadsz=1(逐字节) + `ignore_loglevel`(全级别刷) + 无流控 + 无 DMA + 单核 → WMT insmod 数千行 → 秒级假死窗口, RX FIFO 溢出丢字。
3. **[中置信] B — 无流控 overrun**(叠加 A; 板级已见 ACM 快写冲输入)。
4. **[中置信, 已板级见过] ttyGS0 输入路径被快写冲掉**(多 KB 粘贴 overrun)→ 即使是 USB shell, 大块粘贴也丢。
5. **[低置信] E — ctrl-S/XON**(MTK FC_NONE 下驱动侧关停, 但 tty IXON 默认态未核; 用 ctrl-Q 排除即可)。

---

## 5. 可实施建议(按优先级, 全部只读已验证可行性)

### R1(高价值/低风险 — 只改 cmdline, 不重编译)
**去掉 `ignore_loglevel`, 加 `quiet loglevel=3`**(或直接 `loglevel=3`), 重打包 boot.img(内核/ramdisk 不变)。
- 效果: console 只打 warn/err 以上, WMT 的 KERN_INFO/KERN_DEBUG 洪流被掐掉 90%+, 假死窗口消失。这是对 A 最直接的缓解。
- 若需调试再 `dmesg -n 7` 临时放开。
- 打包复用现有 `mkbootimg` 命令(CLAUDE.md §4a), 只换 `--cmdline`。

### R2(高价值/低风险 — 只重打包 ramdisk, 不重编译)
改 `/init` 两处, 重打 `ramdisk_mainline_connsys.gz`(512B 头 + cpio, 参考 `bridge_backups/products/products_ramdisk_fw_20260810_013549/repack_ramdisk_fw.sh`):
1. **line 36 兜底 shell**: `getty -n -l /bin/sh 0 console` → **`getty -L -n -l /bin/sh 115200 ttyS0 vt100`**(用真 tty 设备 + 显式 CLOCAL + 显式波特率)。ttyS0 打开不会被置 O_NONBLOCK、会设 controlling tty → 直接消除 §3.1 的本机特有根因。`mdev -s` 会建 /dev/ttyS0(8250 probe 已注册)。
2. **恢复 ttyGS0 等待循环**(照 8-01 参考版 `while [ ! -e /dev/ttyGS0 ] && [ $i -lt 25 ]; do mdev -s; sleep 0.2; i=$((i+1)); done`), 避免 ACM shell 静默失败。
3. 可选: line 26 的 ttyGS0 getty 也加 `-L`(ACM 无 modem 线, CLOCAL 无害)。

### R3(结构性, 推荐 — 调研 6.3.1 同款)
**交互 shell 移到 ttyGS0(USB ACM), ttyS0 只留 kernel console 打印**:
- 理由: USB ACM 输入不经过 UART RX FIFO, printk 刷屏时 USB 由主机重试/NAK, **不丢输入**(只会变慢); 彻底解耦"打印"与"输入"。
- init 已按此设计(line 26), 只需 R2 修好等待循环; 上位机用 `socat - /dev/ttyACM0,raw,echo=0` 或 busybox `microcom`。
- **注意 Z1 纪律**: preloader LineState 看 USB 判 download 模式, 开机时插着 USB 会干扰 boot。稳妥: 正常拔 USB 开机(此时 UART 用 R2 后的 ttyS0 shell 兜底), **内核起来后再插 USB 线**接 ACM shell 做重负载交互。

### R4(可选, 治本于源头 — 需重编 WMT)
对 `mtk_stp_wmt.ko`/`wlan_mt6620.ko` 热路径 printk 改 `pr_debug`(需 `CONFIG_DYNAMIC_DEBUG=y`, 当前 `# CONFIG_DYNAMIC_DEBUG is not set`)/`pr_info_ratelimited`。不改源码则靠 `loglevel=3` 压制。非必须——R1 已覆盖。

### R5(不建议)
- 921600: 缩短 TX 窗口但 RX 字符更密、overrun 窗口更小, 且 C3/脚本(`z1_capture_mainline.sh`/`z1cmd.sh`)现全部按 115200 校准, 改动面大。维持 115200。
- 改 getty 加硬件流控 `-h`: 3 线调试口无 RTS/CTS 引出, 无效。

---

## 6. 上板验证清单(零编译, 只读命令)

1. **实锤 A**: `cat /proc/tty/driver/serial` → 看 ttyS0 行的 `oe:`(overrun error)计数, WMT insmod 前/中/后各记一次。敲字时 `oe` 涨 / `rx` 不涨 = 输入没进内核(A/B 实锤); `rx` 涨但无回显 = TX 排队(A/D)。
2. **验证 A 可消除**: `echo "1 4 1 7" > /proc/sys/kernel/printk`(或 `dmesg -n 1`)后 WMT insmod + 敲命令, 回显恢复 = A 实锤。
3. **排除 E**: 卡死时发 `\x11`(ctrl-Q); `stty -F /dev/ttyS0 -ixon -ixoff`。
4. **核对 shell 位置**: `ls -la /dev/ttyGS0` 是否存在; `ps | grep -E 'getty|sh'` 看 PID1 是哪个 getty/device。
5. **核对端口占用**: `fuser /dev/ttyS0`。

---

## 7. 证据行号引用(便于复核)

- init: `/tmp 解包 out/ramdisk_mainline_connsys.gz → /init` line 26/27/36(§2.1)。
- cmdline: `strings out/boot_mainline_connsys.img | grep console=`(§2.2)。
- `/dev/console` O_NONBLOCK: `fork_linux/drivers/tty/tty_io.c:1926`(`filp->f_flags |= O_NONBLOCK`); noctty(永不设 controlling tty): `tty_io.c:2165`(`device == MKDEV(TTYAUX_MAJOR, 1)` 分支)。
- console 逐字节+关 IRQ: `8250_port.c:3302` `serial8250_console_write`(`uart_port_lock_irqsave` 包整段); `8250_port.c:3215` `serial8250_console_putchar`(逐字节); PORT_16550 `fifo_size=1 tx_loadsz=1`: `8250_port.c:65-68`。
- MTK 端口强制 PORT_16550: `8250_mtk.c` probe `uart.port.type = PORT_16550`(554 行附近); 无流控: `8250_mtk.c` `mtk8250_set_termios` 默认 `MTK_UART_FC_NONE`。
- ttyGS0 等待循环参考版: `bridge_backups/products/products_ramdisk_ttygs0_loop_20260801_023151/init_original_ttygs0_ref`(25 次等待)。
- 板级 ACM 快写冲输入: `mainline_recon/MT6323_MFD_ONLY_STRICT_BOARD_RESULT_20260718_LAYOUT2M.md:45-48`。
- 桥波特率校准 115200: `z1_capture_mainline.sh`/`z1cmd.sh`(Z1 console=115200)。

---

## 8. 风险提示(本审计顺带发现的旁支问题)

- **img 与 .gz 疑似不一致**: `out/boot_mainline_connsys.img`(8-14 16:29)内部 ramdisk(831839B)与 `out/ramdisk_mainline_connsys.gz`(8-14 22:43, 2.2MB XZ)**大小/格式对不上**, 板上实际跑的可能不是这份 init。改动生效前先核对/重打包 flash 镜像。
- **kernel 内部还有另一份旧 ramdisk 引用**: `parse_boot.py` 在该 img 里解出第二段 cmdline `console=tty0 console=ttyMT1,921600n1 ... androidboot.selinux=permissive`(旧 LK/ATAG 残留), 说明镜像结构混入历史段, 打包纪律要重新对齐。
