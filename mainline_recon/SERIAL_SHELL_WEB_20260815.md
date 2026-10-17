# 串口 Shell 无回显 / 卡死 — 联网调研报告 (2026-08-15)

调研范围：busybox getty 串口 shell 卡死/无回显、printk 与 shell 抢 console、115200 无流控输入丢失、MT6572/8250_mtk 串口问题。
场景：Z1 mainline 7.0-rc7，initramfs 用 `busybox getty -n -l /bin/sh 115200 ttyS0` 起 shell；boot 后发命令有时无回显（疑似卡死），**WMT (connectivity/mtk_stp_wmt) insmod 后更明显**；cmdline 含 `console=ttyS0,115200n8 panic=10 regulator_ignore_unused`。
方法：tavily + firecrawl 联网检索 + 关键原文抓取（printk-basics 内核文档、kaiwanTECH、Launchpad bug 1534216）。只读调研，未改任何源码/配置。

---

## 1. 结论先行（TL;DR）

**最可能的根因：WMT/connectivity 驱动 insmod 时的 printk 洪流 + legacy 串口 console（console=ttyS0）同步刷屏，把 console_sem 长时间握在手里。** 机制链：

1. MTK WMT/connectivity 驱动加载时打印大量 `printk`（MTK 驱动历史上每个寄存器写/事件都打一行，动辄几百上千行）。
2. legacy console（7.0-rc7 的 8250/8250_mtk 都是 legacy，非 nbcon）在 `console_unlock()` 里**同步、逐字符**（`serial8250_console_putchar`）把消息刷到 UART。115200 下每字符 ~87µs，一行 80 字符 ~7ms；一次 insmod 刷屏可达**秒级阻塞**。实测：单个 printk 行（44 字符）就要 117ms（kaiwan 实测，见 §4.4）。
3. 期间**本地 IRQ 被关**（serial8250_console_write 用 `local_irq_save/restore` 包住整个 TX 循环，防止与 TX IRQ 死锁）。Z1 是单核 Cortex-A7，RX IRQ 在这段时间被饿死 → UART RX FIFO（MTK UART 是 32B FIFO）溢出 → **你敲的字符被硬件丢**，tty 层根本没收到 → 无从回显。
4. 就算字符进了 tty，echo 也是走**同一条 UART TX**，要拿同一个 `uart_port` 锁，排在 printk 洪流后面 → 表现为“敲了没反应、过几秒才一次性蹦出来”或直接丢。

次要因素（可叠加）：无硬件流控的 3 线调试 UART 在 115200 高 IRQ 负载下本身就容易 overrun；WMT 驱动的高频 IRQ/时钟操作进一步拉大 IRQ 延迟。

**这不是 busybox getty 的 bug，也不是波特率配错（输出正常就说明波特率对）。核心是 printk/console 与 tty 输入在同一个无流控 UART 上互抢。**

---

## 2. 常见根因清单（按可能性排序）

| # | 根因 | 机制 | 证据 | 本场景匹配度 |
|---|------|------|------|------|
| A | **printk 洪流 + legacy console 同步刷屏饿死 RX** | console_sem 长持、单核 IRQ 关、UART RX FIFO 溢出 | 内核 printk-basics 文档明确写“legacy consoles (console=ttyS0) synchronously acquire console_sem… disabling interrupts long enough to trigger lockup”；Launchpad bug 1534216；kaiwan 实测 117ms/行 | ★★★★★（WMT insmod 后更明显 → 完美吻合） |
| B | **115200 无流控 + 高负载下 RX overrun** | 3 线 UART 无 RTS/CTS，IRQ 延迟超一个字符时间即丢字节（115200 下 1 字符 ~87µs） | 大量 8250 overrun 讨论；TI 文档 buf_ovrr 计数；rx FIFO trigger 调整 | ★★★★（叠加 A） |
| C | **getty `-n -l /bin/sh` 无 controlling tty / termios 异常** | `-n` 跳过 login 直接 exec shell，若没正确设 controlling tty，job control/回显行为会怪 | busybox 文档；busybox 邮件列表“can't access tty; job control turned off” | ★★（只解释“无 job control”，不解释“无回显”） |
| D | **console=ttyS0 与 tty 输出抢同一端口锁** | printk 写寄存器绕过 tty 层；tty echo 排队在 uart_port 锁后面 | 8250 代码：console write 与 uart 端口锁互斥；groups.google“console=ttyS1 breaks ttyS1 termios” | ★★★（A 的一部分） |
| E | **软件流控 XON/XOFF (IXON) 被 ctrl-S 卡死** | 终端或 stty 开了 IXON，误发 ctrl-S → TX 挂起直到 ctrl-Q | FreeBSD 论坛“serial console blocks/hangs at >9600”；ArchWiki | ★★（可能，检查 stty） |
| F | 8250 输入中断被驱动误关 / MTK pinctrl 把 RX 脚切走 | MTK SoC 引脚复用/pinctrl 配置错误，或 8250_mtk 与 console 的 IER 同步 bug | banana-pi 论坛 8250.mtk 需 patch pinctrl；8250_mtk.c“Port locked to synchronize UART_IER access against the console” | ★★（若 RX 完全没反应才怀疑） |
| G | 进程占着 /dev/ttyS0 / getty 没 respawn | shell 进程退出、另一个进程持有 tty | HPE 论坛“ps -ft ttyMM0 找占用进程” | ★（卡死更久才怀疑） |

---

## 3. 关键机制详解（含原文）

### 3.1 Legacy console 同步刷屏 = 单核下 RX 饿死（根因 A）

**docs.kernel.org/core-api/printk-basics.html → “Avoiding lockups from excessive printk() use”**（7.x 文档，原文）：

> Using `printk()` in hot paths … with **legacy consoles (e.g., `console=ttyS0`) may cause lockups. Legacy consoles synchronously acquire `console_sem` and block while flushing messages, potentially disabling interrupts long enough to trigger hard or soft lockup detectors.**

建议（原文）：`pr_*_ratelimited()` 限速、`KERN_DEBUG` 降级 + `console_loglevel` 过滤、`printk_deferred()`、迁移 nbcon（把打印卸到 kthread）。

注意：7.0-rc7 的 `8250_mtk`/`serial8250` **仍是 legacy console**（nbcon 只有极少数驱动在用），Z1 恰好命中这条文档警告的“hot path printk + console=ttyS0”场景。WMT 驱动加载正是 hot path（probe 里高频打印）。

### 3.2 为什么逐字符刷屏（根因 A 的量化）

**kaiwanTECH: “Why the printk can be so slow on a serial line”**（2019，Raspberry Pi 3B+，实测）：
- 一次 printk（单行）被 ftrace 抓到调用 `serial8250_console_putchar()` **44 次**（逐字符写 UART）。
- 这一个 printk 花了 **117.26 ms**。
- 结论：printk 慢是因为它在一个循环里逐字符把串口寄存器塞满等空。

按这个速率粗算：WMT insmod 若打 2000 行 → ~10 秒以上阻塞；期间你敲的字符（115200 每字符 87µs）全进 32B RX FIFO 就溢出。即便打 200 行，也是 1-2 秒的“假死窗口”，人敲字全丢 → 表现为无回显/卡死。

### 3.3 printk 洪流 + 串口 = 已知会“把系统 hang 很久”的真 bug

**Launchpad Bug #1534216 “lots of printk to serial console can hang system for long time”**（Ubuntu linux，2016，Fix Released）：
> “When the massive number of kernel messages are sent to the serial port driver, it can't keep up, so sending all the log messages can take a very long time - minutes or longer - and the serial port driver fails to schedule itself off the cpu... Other cpus hanging, waiting for the serial port driver's cpu to become available.”
> 修法：`console_may_schedule` 正确置位 + `cond_resched()` 在行间调用（upstream commit 6b97a20d3a79 / 8d91f8b15361）。

这正是“WMT insmod 后 shell 更卡”在单核 Z1 上的放大版：console 刷屏占住 CPU，其他进程/softirq（包括 shell、tty workqueue）全部排队。

### 3.4 8250 console write 关 IRQ（根因 A 的直接代码机制）

`drivers/tty/serial/8250/8250_port.c` 的 `serial8250_console_write()` 用 `local_irq_save()/local_irq_restore()` 包住整段逐字符 TX 循环（防止与 TX IRQ 处理死锁）。后果：**console 打印期间本地中断被屏蔽**，单核上 RX 中断完全无服务窗口 → FIFO overrun。这也是经典“printk 刷屏时串口键盘没反应”的机理，Linux 社区与嵌入式论坛大量复述（NXP/TI/ST 论坛的 overrun 讨论，见 §5 URL）。

### 3.5 tty 层输入缓冲溢出（根因 B）

**TI Processor SDK UART 文档**（serialstats 字段解释）：
- `ovr`：UART HW FIFO overrun 次数（RTS/CTS 没接或没开时发生）。
- `buf_ovrr`：内核 tty 层软件缓冲丢字节数（“driver bug or tty ldisc bug… ldisc 层有责任在接收过载时调 `uart_throttle()`”）。
- `rx/tx` 计数不匹配 = 数据丢失。

调试工具：TI 的 `serialcheck`/`serialstats` 可测；Linux 下直接看 `/proc/tty/driver/serial` 每口统计（`oe:` = overrun error 计数）——**这是上板验证 A/B 的最快手段**（见 §6.2）。

### 3.6 getty 层面（根因 C，优先级低）

busybox `getty -n -l /bin/sh` 跳过 login 直接 exec shell。busybox 邮件列表经典回帖（2005）：
> `::wait:/sbin/getty -n -l /bin/sh 115200 /dev/console vt100` → “/bin/sh: can't access tty; job control turned off”

即：若 shell 没有 controlling tty，job control 关闭——这只影响 Ctrl-C/作业控制，**不导致无回显**。busybox 提供了 `cttyhack`（`/bin/cttyhack /bin/sh`）给 PID1 shell 补 controlling tty。若你们 initramfs 里 shell 是 PID1 或没控 tty，可考虑 `setsid cttyhack sh` 组合，但这不是“无回显”的主因。

### 3.7 console=ttySX 影响该口 termios（根因 D 的旁证）

groups.google linux.kernel 帖 “console=ttyS1 breaks ttyS1 termios and prevents me from logging in”：某口成为 console 后，其 termios/stty 行为会被内核 console 代码干扰（console 直写寄存器 + 该口被 /dev/console 关联）。对本场景：console=ttyS0 是必需的（要打印），但要意识到 ttyS0 同时被 console 与 getty 用，行为比普通口“脏”。

### 3.8 软件流控坑（根因 E）

FreeBSD 论坛 “Serial console blocks/hangs at speeds > 9600 baud”：高波特率下串口“挂死”，解法是 **ctrl-Q**（恢复 XON）或关掉 stty 的 IXON/IXOFF。若某次误发了 ctrl-S（0x13），无流控配置下某些终端会把 TX 冻住直到 ctrl-Q——表现为“无回显但系统没死”。ArchWiki 也强调 minicom 要关掉 hardware flow control，否则“laptop keyboard won't respond”。

### 3.9 MTK 专用坑（根因 F，优先级低）

- **8250_mtk.c**（torvalds master）代码注释多处 “Port locked to synchronize UART_IER access against the console”——MTK 驱动自己就在处理 console 与 IER 的竞态，说明该驱动对 console+input 同口是敏感的。
- **banana-pi 论坛（MT7988, 8250.mtk）**：有用户因 pinctrl 问题必须 patch 8250.mtk 强制把引脚设成 UART 功能才能用——提示 MTK 平台检查 **RX 引脚是否被 pinctrl/pinmux 切走**（Z1 mainline 的 mt6572 pinctrl 若没把 UART0 RX 配成功能脚，输入会断）。Z1 输出正常输入偶发丢，pinctrl 概率低，但值得上板时用 `cat /sys/kernel/debug/pinctrl/...` 核对。

---

## 4. 我们场景的最可能判定

**主因（高置信）：根因 A —— WMT/connectivity insmod 的 printk 洪流在 legacy console=ttyS0 上同步刷屏，单核 IRQ 被关 → RX FIFO overrun → 输入丢失；且 echo/命令输出排队在同一条 TX 后面 → “无回显/卡死”。**
- “WMT insmod 后更明显”是决定性的：空闲 shell 偶尔抽风（开机时 init 打印、磁盘/时钟事件也会刷屏）→ 偶发；WMT 一上来就是几千行打印 → 变得明显。
- 与波特率无关（输出正常）。与 getty 无关（shell 本身在工作，只是输入进不来）。
- 无流控 3 线 UART 放大了它（根因 B 叠加）：一旦有瞬时过载，32B FIFO 溢出即丢。

**第二候选：根因 E 的 ctrl-S/XON 冻结**（如果“无回显”是持续卡死而不是“卡几秒又恢复”）。用 ctrl-Q 或 `stty -ixon` 即可排除。

**第三候选：根因 C/G**（若 shell 连 Ctrl-C、kill、echo 命令都没反应很久）——查 `ps` 看 getty/sh 是否还活着、有无别的进程占 ttyS0。

---

## 5. URL 证据清单

内核/官方文档：
- https://docs.kernel.org/core-api/printk-basics.html （“Avoiding lockups from excessive printk() use”，legacy console 同步刷屏 + 建议）
- https://docs.kernel.org/admin-guide/serial-console.html （console= 语法、多 console、/etc/ioctl.save 38400 坑）
- https://docs.kernel.org/driver-api/tty/n_tty.html （canonical 行缓冲 4096、overrun 处理）
- https://software-dl.ti.com/processor-sdk-linux/esd/docs/06_03_00_106/linux/Foundational_Components/Kernel/Kernel_Drivers/UART.html （ovr / buf_ovrr / rx-tx 计数，硬件流控缺失=overrun）
- https://lwn.net/Articles/800946 （printk 历史、console_lock、慢 console 以中断关闭状态被调用）

具体 bug/实测：
- https://bugs.launchpad.net/bugs/1534216 （printk 洪流刷串口 → CPU hang 数分钟；console_unlock 不 cond_resched；修法 commit 6b97a20d3a79/8d91f8b15361）
- https://access.redhat.com/solutions/1310503 （高 printk + 串口 console → softlockup；去掉串口 console 即好）
- https://kaiwantech.wordpress.com/2019/02/08/why-the-printk-can-be-so-slow-on-a-serial-line （单 printk 44 次 putchar、117ms）
- https://www.thegoodpenguin.co.uk/blog/the-impact-of-non-blocking-consoles-nbcon-on-linux-boot-time/ （nbcon 把串口打印卸到 kthread 的动机）

串口输入丢失 / 流控：
- https://groups.google.com/g/fa.linux.kernel/c/lfW5fcnznmg （rx FIFO trigger 调到 1 字节消 115200 overrun）
- https://forums.freebsd.org/threads/serial-console-blocks-hangs-at-speeds-9600-baud.50903 （高波特率串口挂死 = ctrl-S/ctrl-Q XON-XOFF）
- https://community.onion.io/topic/4001/no-input-over-serial-console-solved （终端侧 hardware flow control 开着 → 串口无输入；关掉就好）
- https://www.embeddedrelated.com/showthread/comp.arch.embedded/91884-1.php （Linux 串口丢字节）
- https://forums.developer.nvidia.com/t/serial-communication-issue-got-overrun-errors/43473 （串口 overrun）

getty / termios：
- https://lists.busybox.net/pipermail/busybox/2005-October/050676.html （getty -n -l /bin/sh → “can't access tty; job control turned off”）
- https://busybox.net/BusyBox.html （cttyhack：给 shell 补 controlling tty）
- https://groups.google.com/g/linux.kernel/c/nBGpbVpJMIg （console=ttyS1 破坏该口 termios）
- https://forum.doozan.com/read.php?8,101334 （“Lost serial console after kernel starts init”，buildroot+busybox 同款场景）

MTK/8250_mtk：
- https://github.com/torvalds/linux/blob/master/drivers/tty/serial/8250/8250_mtk.c （“Port locked to synchronize UART_IER access against the console”）
- https://forum.banana-pi.org/t/uart1-2-as-debug-console/26398?page=2 （MT7988 需 patch 8250.mtk 强制 pinctrl 才能用 UART）

---

## 6. 建议（按“先验证再改”排序）

### 6.1 立即上板验证（只读，不重编译）

1. **看 overrun 计数（决定性证据）**：WMT insmod 前/中/后分别
   ```
   cat /proc/tty/driver/serial
   ```
   找 ttyS0 那行，关注 `tx:`/`rx:`/`oe:`（overrun error）计数。敲字时若 `oe:` 或 `rx` 不涨 = 输入根本没进内核（A/B 实锤）；`rx` 涨但屏幕没回显 = 是 echo/TX 排队问题（A/D 的 TX 侧）。
2. **降 console 打印量验证 A**：
   ```
   echo "1 4 1 7" > /proc/sys/kernel/printk        # console 只打 emerg
   dmesg -n 1
   ```
   再 WMT insmod + 敲命令。若回显恢复正常 → 根因 A 实锤（console 刷屏饿死输入）。
3. **排除 ctrl-S 冻结（根因 E）**：卡死时发 `\x11`（ctrl-Q），或
   ```
   stty -F /dev/ttyS0 -ixon -ixoff
   ```
4. **查进程/占用**：`ps | grep -E 'getty|sh'`、`fuser /dev/ttyS0`。
5. **看 IRQ 分布**：`cat /proc/interrupts` 看 serial IRQ 计数在刷屏期间是否长时间不涨。
6. 若怀疑 pinctrl（F）：`cat /sys/kernel/debug/pinctrl/*/pinmux-pins | grep -i uart`。

### 6.2 中期缓解（低风险，重编一次内核/改 cmdline）

1. **限 console 打印**，cmdline 或 sysctl 常驻：
   - cmdline 加 `quiet loglevel=3`，或 boot 后 `dmesg -n 3`（只看 warn/err 以上）。
   - 需要时再 `dmesg -n 7` 临时放开调试。
2. **WMT/connectivity 驱动打印降级或限速**：对 `mtk_stp_wmt.ko`/`wlan_mt6620.ko` 的 printk 做处理——能改源码就换成 `pr_debug`（配 `CONFIG_DYNAMIC_DEBUG` 后按需开）或 `pr_info_ratelimited`；不改源码就靠 `loglevel` 压制 KERN_INFO 以下（MTK 驱动打印大多是 KERN_INFO/KERN_DEBUG，`dmesg -n 4` 能压掉大部分）。
3. **`/proc/sys/kernel/printk` 四元组里把 minimum 调低**，防止 panic 场景反向拉高。
4. **8250 RX FIFO trigger**：MTK UART 若走 8250 框架，可考虑把 FCR RX trigger 调到 1 字节（消 overrun 窗口，代价是中断更多）；需要改 8250_mtk.c 或端口初始化参数，改动小但属源码级，先做 6.1.1 确认 oe 计数再动。

### 6.3 结构性方案（推荐，治本）

1. **把交互 shell 挪到 USB ACM (`ttyGS0`)，串口只留 kernel console 打印**：
   - Z1 已有 musb peripheral + f_acm（CLAUDE.md 路C 已编通 `CONFIG_USB_MUSB_MEDIATEK=y` + f_acm），`/dev/ttyGS0` 是 USB CDC-ACM 虚拟串口。
   - USB ACM 走 DMA/批量端点，**与 printk 刷屏互不干扰**（printk 仍往 ttyS0 打，shell 在 ttyGS0 上 echo/输入全走 USB，没有共享 UART FIFO/锁问题），输入不丢。
   - 做法：cmdline 保持 `console=ttyS0`，另起一个 getty 于 ttyGS0：`ttyGS0::respawn:/sbin/getty -L ttyGS0 115200 vt100`（或 `-n -l /bin/sh`）。上位机用 `socat - /dev/ttyACM0,raw,echo=0,b115200` 或 busybox `microcom` 连。
   - 代价：C3 桥那根线就只服务 console 打印；交互换到 USB 线。符合 Z1“刷写才插 USB、平时拔掉”的纪律时需要权衡（平时交互 shell 挂在 USB 线上，意味着要插着 USB 线开机——**注意 preloader LineState 坑**：内核起来后插线安全，但开机时已插会干扰 preloader 判模式。稳妥做法：内核起来后再插 USB 线）。
2. **给 console 换 nbcon 或把关键驱动打印改成 `printk_deferred()`**：7.0-rc7 的 8250_mtk 仍是 legacy console，nbcon 迁移不现实（需驱动级改造）；更实际的是把 WMT 驱动热路径的 printk 换 `pr_debug`/限速，这是治本于源头。
3. **不把 console 口当唯一调试口**：接受“WMT 加载时 console 被刷屏占用”是 legacy console 的正常代价，一切交互走 ttyGS0。

### 6.4 不建议/不相关项

- `console_suspend`：那是 suspend/resume 时要不要停 console，与本次症状无关，别浪费时间。
- 重压 ramdisk / 改 getty 参数（`-L`/`-n`）：对“无回显”帮助有限；若排查出 C/G 再动。
- 提高波特率到 921600：能缓解 TX 刷屏时间（每行快 8 倍），但 RX overrun 窗口反而更小（字符更密），且需重配 C3 桥/终端；可作为 A 的临时缓解但不是主解。
- 加硬件流控：Z1 调试口若引出 RTS/CTS 可以开 `CRTSCTS`，对治 B 有效；但常见 3 线调试口没有，且 C3 桥侧也要对应。

---

## 7. 一句话交接

上板先跑 `cat /proc/tty/driver/serial` 看 ttyS0 的 `oe`（overrun）计数 + `dmesg -n 1` 压 console 打印再敲命令：两者一起证明“WMT printk 刷屏饿死串口输入”就实锤，然后按 6.2.1（loglevel 限速）+ 6.3.1（shell 挪 ttyGS0）两条线落地。这大概率不是 busybox/getty 的锅，是 legacy console 与无流控 UART 的固有互抢。
