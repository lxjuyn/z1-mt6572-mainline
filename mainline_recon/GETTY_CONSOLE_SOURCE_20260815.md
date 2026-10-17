# 串口 Shell 卡死 — 源码级核对报告 (2026-08-15)

> 只读源码核对。核对对象: `SERIAL_LOCAL_AUDIT_20260815.md` 的根因判定 R1/R2/R3 修复建议, 以及 /dev/console O_NONBLOCK + noctty、8250 console 写路径、MTK 端口类型/流控。
> 证据源(全本机, 未改任何文件): `fork_linux/drivers/tty/tty_io.c`, `8250_port.c`, `8250_mtk.c`, `8250_core.c`, `serial_core.c`, `kernel/printk/printk.c`, `.config`, 板级 boot log `mainline_recon/z1_boot_emmc_full6_232325.log`(cmdline 与审计 §2.2 完全一致, 同一条 kernel 线)。

---

## 0. TL;DR

1. **/dev/console O_NONBLOCK + 永不设 controlling tty: 源码确认**(tty_io.c:1927 / :2165)。/dev/ttyS0 直开则两者皆无(走 default 分支, noctty=false)。
2. **serial8250_console_write 整段关本地 IRQ + 清 UART IER: 源码确认**(8250_port.c:3302-3387)。`uart_port_lock_irqsave` 包住**整条 printk 消息**(非逐字符), `serial8250_clear_IER` 写 0 关掉含 RX 在内的所有 UART 中断。
3. **关键修正(审计最大偏差)**: 板上 ttyS0 的实际端口类型**不是** PORT_16550(fifo=1/tx_loadsz=1), 而是 autoconfig 检出的 **ST16650V2(fifo_size=32, tx_loadsz=16, UART_CAP_FIFO)**。板级铁证: boot log `... ttyS0 at MMIO 0x11005000 (irq = 56, base_baud = 1625000) is a ST16650V2`(多份 log 7/14–8/01 稳定一致)。因此 **console TX 走 `serial8250_console_fifo_write` 16 字节 FIFO 批量路径, 不是逐字节 putchar**; 审计 §2.3/§3 行4/§4.1 的 "PORT_16550 fifo=1 逐字节 → 117ms/行" 子机制在板上不成立。
   - 机制: `mtk8250_probe` 确实写 `uart.port.type = PORT_16550`(8250_mtk.c:557), **但** flags 只含 `UPF_BOOT_AUTOCONF | UPF_FIXED_PORT`, 无 `UPF_FIXED_TYPE` → `uart_configure_port` 把 type 重置为 PORT_UNKNOWN 并跑 autoconfig(serial_core.c:2515-2519)→ 板上 IIR 检出 16550A + EFR → `autoconfig_has_efr` → `PORT_16650V2`(8250_port.c:864)。
4. **R1/R2/R3 修复建议: 全部源码层面成立, 且不依赖逐字节子机制 → 建议保持, 只有诊断叙述需修正**。R1(`loglevel=3`+去 `ignore_loglevel`)仍是最大单一杠杆。

---

## 1. /dev/console: O_NONBLOCK + noctty(tty_io.c) — 确认

### 1.1 O_NONBLOCK(tty_io.c:1920-1934)
```c
case MKDEV(TTYAUX_MAJOR, 1): {                 /* /dev/console */
    struct tty_driver *console_driver = console_device(index);
    if (console_driver) {
        driver = tty_driver_kref_get(console_driver);
        if (driver && filp) {
            /* Don't let /dev/console block */
            filp->f_flags |= O_NONBLOCK;        /* ← tty_io.c:1927 */
```
- 审计引 `tty_io.c:1926` — 实际 1926 是注释行, `O_NONBLOCK` 赋值在 **1927**。功能确认: **内核强制置位, 与用户 open 时的 O_NONBLOCK/O_RDWR 标志无关**。
- 对照 `/dev/tty`(TTYAUX_MAJOR,0): `tty_open_current_tty` 同样 `filp->f_flags |= O_NONBLOCK`(tty_io.c:1879)。
- **对照真设备 `/dev/ttyS0`(char 4:64)**: `tty_lookup_driver` 走 `default:` 分支(tty_io.c:1935-1941), **不置 O_NONBLOCK**。→ R2(改 getty 用 ttyS0)源码层面消除 O_NONBLOCK。

### 1.2 noctty(tty_io.c:2163-2169)
```c
noctty = (filp->f_flags & O_NOCTTY) ||
         (IS_ENABLED(CONFIG_VT) && device == MKDEV(TTY_MAJOR, 0)) ||
         device == MKDEV(TTYAUX_MAJOR, 1) ||      /* ← tty_io.c:2165, /dev/console 恒 no */
         (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
          tty->driver->subtype == PTY_TYPE_MASTER);
if (!noctty)
    tty_open_proc_set_tty(filp, tty);              /* /dev/console 永不走 */
```
- **/dev/console 永不设为 controlling tty**。对照 `/dev/ttyS0`: 非 TTYAUX/非 VT/非 PTY master → noctty=false → 正常设 controlling tty。→ R2 源码层面消除"无 controlling tty"。

### 1.3 /dev/console 绑定到 ttyS0("last console= wins")— 确认(printk.c)
- cmdline `console=tty0 console=ttyS0,115200n8`(boot log z1_boot_emmc_full6_232325.log:39 实测一致)。
- `preferred_console` = **最后一个** console= 条目(printk.c:2590/2598, `__add_preferred_console`)。
- `register_console` 匹配 preferred_console 时置 `CON_CONSDEV`(printk.c:3931-3932); 链表插入时 **CON_CONSDEV 项放表头**、非 CON_CONSDEV 项插到表头之后(printk.c:4180-4183)。无 cmdline 指定时首注册的 console 强制置 CON_CONSDEV 成表头(4175-4178)。
- `console_device()`(printk.c:3565-3589)遍历表链返回**第一个** device() 非空的 console; `uart_console_device`/`vt_console_device` 都不查 CON_CONSDEV、无条件返回各自 driver(serial_core.c:2797-2802 / vt.c:3447-3451)。
- 结论: 无论 tty0/ttyS0 谁先注册, **表头 = ttyS0**(最后一个 console=)→ `/dev/console` 打开 ttyS0 index 0。审计 §2.2 的 "console shell 实际落 ttyS0" 成立。boot log `printk: legacy console [ttyS0] enabled` 佐证。

---

## 2. serial8250_console_write(8250_port.c:3302)— 关 IRQ 确认, 逐字节子机制**修正**

### 2.1 整段关 IRQ + 清 IER — 确认
```c
void serial8250_console_write(...)                    /* :3302 */
{
    ...
    if (oops_in_progress)
        locked = uart_port_trylock_irqsave(port, &flags);
    else
        uart_port_lock_irqsave(port, &flags);          /* :3316, 整段关本地 IRQ */
    ier = serial_port_in(port, UART_IER);
    serial8250_clear_IER(up);                          /* :3322, UART 全中断(含 RX)关 */
    ...
    /* use_fifo 判定 :3336-3354 */
    if (likely(use_fifo))
        serial8250_console_fifo_write(up, s, count);   /* :3357 */
    else
        uart_console_write(port, s, count, serial8250_console_wait_putchar); /* :3359 逐字节 */
    wait_for_xmitr(up, UART_LSR_BOTH_EMPTY);           /* :3365 */
    ...
    serial_port_out(port, UART_IER, ier);              /* :3373 恢复 */
    uart_port_unlock_irqrestore(port, flags);          /* :3386 */
}
```
- `serial8250_clear_IER`(8250_port.c:711-717): ST16650V2 无 `UART_CAP_UUE` → `serial_out(IER, 0)` — **RX 中断(RDI)一并关掉**。
- **关键点: IRQ 关闭窗口是"整条 printk 消息", 不是单字符**。`wait_for_xmitr(BOTH_EMPTY)` 用 `frame_time` 超时(8250_port.c:1956-1981), 不构成自锁; 饿死 RX 的是"长窗口内 RX FIFO(16B)溢出丢字 + RX IRQ 被关无法排空"。

### 2.2 **修正: 板上走 FIFO 批量路径, 不是逐字节**
`use_fifo` 条件(8250_port.c:3336-3354):
```c
use_fifo = (up->capabilities & UART_CAP_FIFO) &&
    !(up->capabilities & UART_CAP_MINI) &&
    up->tx_loadsz > 1 &&
    (up->fcr & UART_FCR_ENABLE_FIFO) &&
    port->state &&
    test_bit(TTY_PORT_INITIALIZED, &port->state->port.iflags) &&
    !(up->port.flags & UPF_CONS_FLOW);
```
- 板上实际端口 **ST16650V2**(见 §3): `tx_loadsz=16`、`UART_CAP_FIFO`、fcr 含 `UART_FCR_ENABLE_FIFO` → 一旦 getty 打开 ttyS0(`TTY_PORT_INITIALIZED` 置位), **use_fifo=true → 走 `serial8250_console_fifo_write`(16 字节批量, 每批等 THRE)**(8250_port.c:3260-3291), 而非逐字节 `uart_console_write`。
- 只有 ttyS0 尚未被任何 tty open 时(极早期 boot, getty 之前)才走逐字节。WMT insmod 洪流场景(getty 已跑)→ 恒为 FIFO 路径。
- 审计 §2.3 引的 `serial8250_console_putchar`(:3215-3217)逐字节确实存在, 但它只被 FIFO 批量的内层循环(每批 ≤16 次)或逐字节路径用到; 板级是前者。

### 2.3 PORT_16550 表项 — 确认但**板上用不到**
- `uart_config[PORT_16550]`(8250_port.c:61-65): `fifo_size=1, tx_loadsz=1`(审计引 L65-68, 实际表项 61-65)。
- 但该表项只在 `serial8250_set_defaults` 的 `UPF_FIXED_TYPE` 分支(3179-3188)或 autoconfig 后按检出 type 取用(1185-1188); MTK 无 UPF_FIXED_TYPE, 且 autoconfig 把 type 改成 ST16650V2 → **实际生效的是 `uart_config[PORT_16650V2]`(8250_port.c:85-93): fifo_size=32, tx_loadsz=16, fcr=`UART_FCR_ENABLE_FIFO|R_TRIG_01|T_TRIG_00`, flags=`UART_CAP_FIFO|UART_CAP_EFR|UART_CAP_SLEEP`**。

---

## 3. 8250_mtk.c: PORT_16550 是"probe 预设", 非最终; 无流控确认

### 3.1 probe 预设 vs autoconfig 覆盖(审计遗漏的关键一环)
- `mtk8250_probe`(8250_mtk.c:518): `uart.port.type = PORT_16550;`(:557, 审计说 554, 实际 557)+ `uart.port.flags = UPF_BOOT_AUTOCONF | UPF_FIXED_PORT;`(:558)。
- **无 `UPF_FIXED_TYPE`** → `serial8250_register_8250_port` → `uart_add_one_port` → `uart_configure_port`(serial_core.c:2515-2519):
  ```c
  if (port->flags & UPF_BOOT_AUTOCONF) {
      if (!(port->flags & UPF_FIXED_TYPE)) {
          port->type = PORT_UNKNOWN;   /* probe 预设被丢弃 */
          flags |= UART_CONFIG_TYPE;
      }
      port->ops->config_port(port, flags);   /* → autoconfig(up) */
  ```
- `autoconfig`(8250_port.c:1068): IIR FIFO 位检出 `UART_IIR_FIFO_ENABLED_16550A`(0xc0)→ `autoconfig_16550a`(:1173-1174)→ `.config` 里 `CONFIG_SERIAL_8250_16550A_VARIANTS=y` → 继续 → LCR=0xBF 下读标准 EFR 为 0 且非 broken_efr → `autoconfig_has_efr`(:954)→ 非 Oxford(serial_icr_read 出不来 0x16/0xC9)、非 XR16C850 → `size_fifo(up)!=64` → **`port->type = PORT_16650V2`(:864)**。
- autoconfig 末尾统一回填 `fifosize/capabilities/tx_loadsz`(8250_port.c:1185-1188), `config_port` 再写 `up->fcr = uart_config[type].fcr`(:3110)。
- **板级铁证**: `z1_boot_emmc_full6_232325.log:935`(以及 7/14–8/01 多份 log): `11005000.serial: <SERIAL> at MMIO 0x11005000 (irq = 56, base_baud = 1625000) is a ST16650V2`。
  - `base_baud=1625000` = uartclk 26MHz / 16(MT6572 uart0 时钟), 佐证是真实 MTK 端口。
  - `is a ST16650V2` 即 `serial8250_type` 打 `uart_config[PORT_16650V2].name`(8250_port.c:3124-3131 / uart_report_port)。
- 机制细节(为什么 MTK 会"误检"成 EFR UART): MTK 自己的流控寄存器 `MTK_UART_EFR=38`(8250_mtk.c:40)与标准 16550 EFR(offset 2, DLAB+bit6)不是同一物理寄存器; autoconfig 在标准 EFR 地址(offset 2)读到 0 → 判定"有 EFR 的空寄存器" → 落入 ST16650V2 兜底。属于经典误检, 但功能上无碍(16B FIFO 真实存在, 只是表里标 32)。

### 3.2 无流控 — 确认
- `mtk8250_set_termios`(8250_mtk.c:411-418):
  ```c
  if ((termios->c_cflag & CRTSCTS) && (!(termios->c_iflag & CRTSCTS)))
      mode = MTK_UART_FC_HW;
  else if (termios->c_iflag & CRTSCTS)
      mode = MTK_UART_FC_SW;
  else
      mode = MTK_UART_FC_NONE;
  ```
- 默认 termios(无 CRTSCTS、无软件流控)→ **MTK_UART_FC_NONE**(415-416); `mtk8250_set_flow_ctrl` FC_NONE 分支关掉 XOFFI/RTSI/CTSI 中断(8250_mtk.c:257-266)。→ 3 线调试口无 HW/SW 流控, 审计根因 B 叠加成立。

### 3.3 无 DMA — 确认
- dts `uart0@11005000`(mt6572.dtsi:425-432)无 `dmas`/`dma-names`; z1.dts `&uart0` 仅 `status="okay"`。
- `mtk8250_probe_of` 仅当 `dma-names` 计数==2 才分配 DMA(8250_mtk.c:501-512); 无则 `data->dma=NULL`。
- 即便有 DMA, `mtk8250_startup` 对 console 端口 `up->dma = NULL`(8250_mtk.c:196-198)。→ PIO + RX IRQ, RX 缓冲只有硬件 FIFO。审计 §2.3 成立。

### 3.4 共享 univ8250 console — 确认
- `8250_mtk.c` 无自有 `.console`; 走 `8250_core.c` `univ8250_console`(.name="ttyS", 8250_core.c:497-499)→ `univ8250_console_write`(:393-399)→ `serial8250_console_write`。审计 §2.3 成立。

---

## 4. R1/R2/R3 交叉验证(源码层面)

### R1 — 去 `ignore_loglevel` + `loglevel=3`(只改 cmdline): **成立, 仍是最大杠杆**
- `ignore_loglevel`(printk.c:1272-1289): `suppress_message_printing` 返回 `(level >= console_loglevel && !ignore_loglevel)` → ignore_loglevel=true 时**任何级别都不被抑制**。
- 本机 `.config:6055` `CONFIG_CONSOLE_LOGLEVEL_DEFAULT=7` → 默认 console_loglevel=7, **KERN_INFO(6)/KERN_WARNING(4) 本来就全量上 console**。→ **修正审计表述**: "ignore_loglevel 让 KERN_INFO/DEBUG 也全量上 console" 中的 KERN_INFO 部分**高估**了; ignore_loglevel 的边际贡献仅是 KERN_DEBUG(7) 及动态调试级别。真正的大头是默认 7 级 + WMT 的热路径 printk。
- `loglevel=3` 经 `console_loglevel` 写入(printk.c:1800-1802)→ 只有 0-3 级(EMERG..ERR)上 console → 掐掉 KERN_WARNING(4)/NOTICE(5)/INFO(6)/DEBUG(7)。**净削减量级依旧巨大, R1 建议维持**。
- 附注: 审计 §5 的 `dmesg -n 1` 验证手段与 `loglevel=1` 等价, 可用。

### R2 — getty 改 `/dev/ttyS0` + `-L`(只重打包 ramdisk): **内核侧成立**
- `/dev/ttyS0` 直开: 无 O_NONBLOCK 强制(§1.1), 会设 controlling tty(§1.2)→ 直接消除审计 §3.1 的两个本机特有内核行为。源码确认。
- `-L`(CLOCAL)是 busybox getty 的 userspace 行为, 不在 kernel 源码可核范围内; 仅提示: 无 `-L` 时 getty 对无 DCD 的 3 线口存在等载波风险, 属审计已标注的"次要项", 建议加, 无内核障碍。
- 恢复 ttyGS0 等待循环: ramdisk/init 脚本问题, 与内核源码无涉, 照审计执行。

### R3 — 交互 shell 挪 ttyGS0(USB ACM), ttyS0 只留 kernel console: **成立(架构建议)**
- USB ACM 输入不经 UART RX FIFO/IRQ 窗口, 与 printk 洪流解耦; 与 Z1 纪律(拔 USB 开机, 内核起来后再插线)兼容。不涉及 8250 源码改动。

### R5 — 维持 115200 / 不加 -h: 与源码无冲突, 维持。

---

## 5. 结论与建议

1. **审计主根因判定方向正确, 但量化口径需修正**:
   - ✅ /dev/console O_NONBLOCK + noctty(本机特有, §3.1 高置信): 源码确认。
   - ✅ A 机制(printk 洪流 + console 写整段关 IRQ/关 IER + 单核 + 无 DMA + 无流控 → RX FIFO 溢出丢输入): 源码确认。
   - ❌ "PORT_16550 fifo=1 → 逐字节 putchar → 117ms/行" : **板上不成立**, 实际 ST16650V2(fifo=32/tx_loadsz=16)走 16B FIFO 批量。该子机制不影响 A 的定性结论, 只影响"逐字节"叙述。
2. **修复建议维持**: R1、R2、R3 全部源码层面成立, 且不依赖逐字节子机制。优先级不变: R1(零编译, 消洪流)+ R2(消 /dev/console 两怪)+ R3(结构性)。
3. **建议顺带做的板上核对**(零改动, 实锤端口类型): `cat /proc/tty/driver/serial` → ttyS0 行应显示 `type:16650V2`(或 boot log 的 `is a ST16650V2`); `cat /proc/sys/kernel/printk` 当前值确认 console_loglevel。

---

## 6. 证据行号索引

| 项 | 位置 |
|---|---|
| /dev/console O_NONBLOCK | tty_io.c:1927(注释 1926); /dev/tty :1879 |
| /dev/console noctty | tty_io.c:2165(noctty 表达式内 `MKDEV(TTYAUX_MAJOR,1)`), 2168-2169 跳过 tty_open_proc_set_tty |
| /dev/ttyS0 无 O_NONBLOCK | tty_io.c:1935-1941(default 分支) |
| preferred_console=最后 console= | printk.c:2590/2598 |
| CON_CONSDEV 置位/表头插入 | printk.c:3931-3932, 4175-4183 |
| console_device 取表头 | printk.c:3565-3589 |
| serial8250_console_write 全段关 IRQ | 8250_port.c:3316(irqsave), 3322(clear_IER), 3373/3386(恢复) |
| serial8250_clear_IER 写 0 | 8250_port.c:711-717 |
| use_fifo 条件 | 8250_port.c:3336-3354 |
| serial8250_console_fifo_write 16B 批量 | 8250_port.c:3260-3291 |
| serial8250_console_putchar 单字节 | 8250_port.c:3215-3218 |
| PORT_16550 表项 fifo=1 | 8250_port.c:61-65 |
| PORT_16650V2 表项 fifo=32/tx_loadsz=16 | 8250_port.c:85-93 |
| autoconfig 覆盖 type + 回填参数 | 8250_port.c:1068(1166-1178 IIR 分派), 1185-1188, 864(PORT_16650V2) |
| MTK probe 预设 PORT_16550 | 8250_mtk.c:557; flags 558 |
| uart_configure_port 重置 type + autoconfig | serial_core.c:2515-2519 |
| 板级实测 "is a ST16650V2" | z1_boot_emmc_full6_232325.log:935(cmdline:39 同审计 §2.2) |
| MTK 无流控默认 | 8250_mtk.c:411-418, 257-266 |
| MTK 无 DMA | mt6572.dtsi:425-432; 8250_mtk.c:501-512, 196-198 |
| ignore_loglevel | printk.c:1272-1289; CONFIG_CONSOLE_LOGLEVEL_DEFAULT=7(.config:6055) |
| loglevel= 写入 | printk.c:1800-1802 |
