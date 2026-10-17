# Z1 mainline ttyS0 / CH340 UART console 深挖 (2026-08-15 只读分析)

> 只读分析, 未改任何文件。结论先行: **dts / console 注册链没有回归, uart0=ttyS0 仍被正确 enable 且 console=ttyS0 注册机制完好; 但最近(bootargs 改 loglevel=6 后)UART console 的 printk→串口轮询写路径在 boot 早期(~0.69s)就 wedge, 只剩 getty 的 tty 中断路径在工作; 而 21:19 起团队已把交互 shell 主力转向 USB ACM(ttyGS0), CH340 已非主用通道。**
> 关键文件: `fork_linux/drivers/tty/serial/8250/8250_mtk.c` / `fork_linux/arch/arm/boot/dts/mediatek/mt6572.dtsi` / `mt6572-z1.dts` / `drivers/tty/serial/8250/8250_core.c` / `kernel/printk/printk.c`

---

## 1. Q1 — 驱动注册的 tty 名: **ttyS0**(不是 ttyMT0)

`8250_mtk.c` 的 probe(`mtk8250_probe`, 行 518)最终调用
`serial8250_register_8250_port(&uart)`(行 578)把端口登记进通用 8250 框架,
由 8250 核心分配 `ttyS%d` 名称(port line 0 → **ttyS0**)。

- `ttyMT0` 是**原厂 3.4.67 MTK 串口驱动**(`drivers/misc/mediatek/uart` 或 vendor `tty_MTK`)的名字, mainline 里**不存在**。
- 板上物理"UART1"口在 mainline 下对应 **uart0 @ 0x11005000 = ttyS0**(见 Q3)。
- 佐证(最近一次完整工作 boot, `mainline_recon/z1_boot_mainline_20260815_171058.log` 行 947):
  `11005000.serial: <SERIAL> at MMIO 0x11005000 (irq = 56, base_baud = 1625000) is a ST16650V2`

## 2. Q2 — uart0@11005000 dts 节点: compatible/status 正常,**status=okay**

`mt6572.dtsi`(行 425-432, 未改动, mtime 07-30):
```
uart0: serial@11005000 {
    compatible = "mediatek,mt6572-uart", "mediatek,mt6577-uart";
    reg = <0x11005000 0x400>;
    interrupts = <GIC_SPI 31 IRQ_TYPE_LEVEL_LOW>;
    clocks = <&topckgen CLK_TOP_UART0_SEL>, <&topckgen CLK_TOP_UART0>;
    clock-names = "baud", "bus";
    status = "disabled";
};
```
dtsi 默认 `disabled`, **在 `mt6572-z1.dts` 行 283-285 被翻开**:
```
&uart0 { status = "okay"; };
```
**当前刷入镜像 `out/boot_mainline_z1_stable.img`(21:19 构建)的 dtb 实测确认**:
- `serial@11005000` → `status = "okay"`; uart1 `serial@11006000` → `status = "disabled"`。
- clocks phandle 解析: `CLK_TOP_UART0_SEL=25(0x19)` + `CLK_TOP_UART0=65(0x41)`, 与
  `include/dt-bindings/clock/mediatek,mt6572-clk.h`(行 34/77)及
  `drivers/clk/mediatek/clk-mt6572-topckgen.c`(行 189/268)完全匹配 → **时钟查询不会失败**。
- `aliases { serial0 = &uart0 }` 在位; chosen **无 stdout-path**(注释明确"do not instantiate an early console from DT")。

## 3. Q3 — 哪条 UART 物理口接 CH340: **uart0 @ 0x11005000 = ttyS0**

- 项目文档/历史:"PC↔/dev/ttyACM0↔C3↔UART1↔Z1 ttyMT0"(原厂 3.4.67 时代 ttyMT0=UART0 IP)。板上那个串口排针接的就是 CH340/C3 桥。
- mainline 下该口 = `serial@11005000`(uart0)→ ttyS0;uart1 `@11006000` 被 disabled。
- 铁证:工作 boot log 里 CH340 抓到的是 `11005000.serial: <SERIAL> ...` 的完整 boot 输出(见 §5), 即 **CH340 ↔ uart0 ↔ ttyS0** 三者同一物理链路。
- 注: 工作 log 显示 `irq = 56`, 而 dts 写 `GIC_SPI 31`(常规映射应为 32+31=63)。56=32+24, 说明 MT6572 GIC 偏移下该 specifier 实际落到 Linux IRQ 56。TX(console/轮询)不依赖 IRQ; 历史上 RX(getty 回显敲字)也工作过, 故暂非故障点, 但若"只 TX 通 RX 死", 优先怀疑这个 IRQ 映射。

## 4. Q4 — console=ttyS0 真的注册吗:**机制完好, 历史上注册过; 与 tty0 不冲突**

### 4.1 注册链(逐段核实)
1. `CONFIG_SERIAL_8250_CONSOLE=y` → `8250_core.c:497` `univ8250_console`:
   `{ .name = "ttyS", .write = univ8250_console_write, .setup = univ8250_console_setup, .flags = CON_PRINTBUFFER | CON_ANYTIME, .index = -1 }`,
   由 `console_initcall(univ8250_console_init)`(行 518)注册。
2. cmdline `console=ttyS0,115200n8` → `add_preferred_console("ttyS",0,...)`;8250 核心的
   `univ8250_console_match`(行 462)只处理 `console=uart...` 形式, 常规形式走默认匹配(name+idx)。
3. uart0 probe 成功(=`serial8250_register_8250_port` 返回 line 0)后端口获得 `UPF_CONSOLE`, `register_console` 使能并打印 `printk: legacy console [ttyS0] enabled`, 同时 `CON_PRINTBUFFER` 把整个 boot 缓冲 flush 到 UART。
4. `8250_mtk.c:676` `OF_EARLYCON_DECLARE(mtk8250, "mediatek,mt6577-uart", ...)` 只提供 **earlycon**(需 cmdline `earlycon=` 或 DT stdout-path 才激活); 当前两者都没有 → 无 early console, 不参与。
5. of_serial(`8250_of.c`, `CONFIG_SERIAL_OF_PLATFORM=y`)的 match 表**不含** mt6577-uart(只有 `mediatek,mtk-btif`), 不会与 mtk8250 抢 uart0 → 绑定唯一。

### 4.2 tty0 不会"独占"console
`console=tty0 console=ttyS0,...` 两个 console **都会**收到 printk; `tty0` 在前**不会**把输出独吞。
顺序只决定 `/dev/console` 指向谁:**最后一个 `console=`(ttyS0)是 primary console** → init 的 `[init] welcome...` 打到 ttyS0, 所以历史上 welcome 出现在 CH340 UART 上(见 §6)。

### 4.3 历史上确实注册成功(铁证)
`z1_boot_mainline_20260815_174557.log`(17:45, `ignore_loglevel`):
```
[ 0.271] Serial: <SERIAL> driver, 5 ports, IRQ sharing enabled
[ 0.293] printk: legacy console [ttyS0] disabled
[ 0.294] probe of 11005000.serial:<SERIAL> returned 0
[ 0.317] 11005000.serial: <SERIAL> at MMIO 0x11005000 (irq = 56, base_baud = 1625000) is a ST16650V2
[ 0.319] printk: legacy console [ttyS0] enabled
```
17:10 的 414KB log 有 **8 条** `console [...] enabled` + **82 条** serial 行(多轮 boot 均通)。

## 5. 回归时间线(证据)

| 时间 | bootargs 相关 commit | log | UART 表现 |
|---|---|---|---|
| 17:10 / 17:45 | `ignore_loglevel panic=0` | `171058`/`174557` | 完整 boot log + `console [ttyS0] enabled` + 8250 probe 全有 ✅ |
| 17:52 | `201a1c62b` loglevel=3 (DT bootargs 生效) | | bootargs 切到 loglevel |
| 17:59 | loglevel=3 | `175914` | 30min capture 0 字节, 但 shell 可交互(`cat /proc/sys/kernel/printk` → `3 4 1 7`) |
| 20:16 | `8dda0c6f0` loglevel=4 | | |
| 20:28 | `c71f6d4cf` loglevel=6 + printk_ratelimit | | |
| 20:02 | loglevel=6 | `200224` | 无 kernel boot 行, 只有 `[init]` + shell(`echo UART_TEST` 回显) |
| 20:48 | loglevel=6 | `204801` | 只出 13 条 loglevel≤6 消息, ~0.69s 后 kernel console 静默; shell 仍可交互; ~30.5s WDT 复位 |
| 21:19 | `1d3d5a0c0` getty 主攻 ttyGS0 (USB ACM) "avoids CH340 unreliability" | `stable.img` 构建 | |
| 21:31-22:01 | | `213100` | **30min 0 字节** |

**关键量测**: 17:59 起所有 log 里 `console [ttyS0] enabled` 与 8250/`11005000.serial` 行全部消失(20:48 log 亦无), 但 boot 初期 13 条消息 + `[init]` + shell 仍在 UART 上 → **UART 硬件没死, 死的是 console 输出路径**。

## 6. "welcome 出现在 UART log"的机制(= 你问的历史证据)
`[init] welcome to the Z1 Linux bringup initramfs` 来自 initramfs `/init` 行 22
(当前 stable ramdisk 提取确认), 打到 `/dev/console` → primary console = ttyS0 → CH340。
它在 17:10/17:45 的 UART log 里出现 = 当时 **ttyS0 console 和 tty 路径都活着**。

## 7. 判定: ttyS0 console 为什么现在收不到

### 7.1 排除项(源码/配置层,**无回归**)
- uart0 `status=okay`(当前 stable dtb 实测)、compatible/clocks/irq 都对。
- `CONFIG_SERIAL_8250_CONSOLE=y` + `univ8250_console("ttyS")` + `console=ttyS0` 全链完好; 同一内核 zImage #91 17:45 还注册成功过。
- tty0 不独占; earlycon 未启用, 不影响。

### 7.2 真凶(行为层, 有日志支撑)
1. **printk→UART console 轮询写路径在 boot 早期就 wedge**:
   20:48 log 中 kernel 消息止于 `[0.690686] mmcblk0`(13 条 loglevel≤6), 之后 printk 再不出现;
   与项目自记录的"shell-hang / /dev/console O_NONBLOCK + printk 洪峰"(`83a7948ed`, 8/15 17:02 的 shell-hang fix)同族。
   `serial8250_console_write`(轮询 LSR/THR)与 getty 的 tty 中断 TX 是**两条不同路径**, 所以 console 死但 shell 活。
2. **`loglevel=6` + printk_ratelimit 让"console 死掉"更难被日志暴露**: 本来能看到 `console [ttyS0] enabled` 的 `pr_info` 现在也不出了(该消息本身就要走这条已 wedge 的路径), 表现就是"boot 输出稀薄 + 无 8250 probe 行"。
3. **交互主力已迁到 USB ACM(ttyGS0)**: `1d3d5a0c0`(21:19)commit 明确"USB ACM shell now usable for interactive debug **(avoids CH340 unreliability)**" → 项目方已知 CH340 不稳。
4. **21:31 的 30min 0 字节** 合理解释: 设备稳定(未每 30.5s 复位, 屏幕 shell 正常)→ 无 preloader banner; console 路径不输出; 无人敲 CH340 → 天然 0 字节。若你敲了也没回显, 则要么 ttyS0 设备不存在(8250 probe 失败), 要么 tty TX 也 wedge, 要么 CH340 侧接线/波特率(见 §8)。

### 7.3 附注: 两个潜在隐患
- `8250_mtk.c:429-442 mtk8250_runtime_suspend` 有 `while (serial_in(up, MTK_UART_DEBUG0));` **无限忙等**——若 UART 空闲位不清(或时钟被 gate 后读寄存器恒非零)会死循环。`pm_runtime_put_sync_suspend` 在 getty 关闭 ttyS0 时触发, 是"shell 突然僵死"的候选。
- WDT 30.5s 复位背景(P6_EOD 判定): `busybox watchdog -t 5 -T 10` 喂狗不稳定, boot 存活时好时坏, 会掩盖/混淆 UART 观测。

## 8. 排查建议(在设备屏幕 shell 上执行, 只读优先)
1. 确认 ttyS0 设备在: `ls -l /dev/ttyS0 /dev/console`
2. `cat /proc/tty/driver/serial` → 应有 `0: uart:ST16650V2 port:000011005000 irq:56 tx:N rx:N`(tx 计数增长=TX 路径通)
3. `dmesg | grep -iE "11005000.serial|console \\[ttyS0\\]|Serial: <SERIAL>"` → 看 8250 probe 与 `console [ttyS0] enabled` 是否出现
4. TX 直测: `echo TEST_TX > /dev/ttyS0`(CH340 应收到)
5. RX 直测: `cat /dev/ttyS0 &` 后在 CH340 敲字 → 屏幕应回显
6. `cat /proc/sys/kernel/printk`(当前 `loglevel=6` → `6 4 1 7`); 可临时 `echo 8 > /proc/sys/kernel/printk` 看 console 是否复明
7. 若 /dev/ttyS0 在但 TX 死: `stty -F /dev/ttyS0 115200 raw clocal` 重设; 或重启时保持 CH340 capture, 看 preloader banner(F0/F3/`Jump to BL`)是否出——**banner 都不出 → 接线/CH340 侧问题与内核无关**
8. 若 /dev/ttyS0 不存在: 查 dmesg mtk8250 probe 是否 deferred/报错(时钟 `devm_clk_get` "baud"/"bus")
9. 硬件侧: C3/CH340 TX↔RX 交叉、共地、115200 8N1; 当前主机 `/dev/ttyACM0` 都不存在(刚查), 确认桥还插着

## 9. 一句话
**dts/config 没坏, console 注册链没坏, 但当前 `loglevel=6` 构建下 printk→CH340 的 console 写路径在 boot 早期就 wedge, 团队已把交互通道切到 USB ACM(ttyGS0); 要在 CH340 拿回控制台, 先按 §8.3/§8.4 区分"8250 没 probe" vs "console 写路径 wedge", 再决定是调 bootargs(降 loglevel/去掉 printk_ratelimit)、修 `mtk8250_runtime_suspend` 忙等, 还是回到早期 `ignore_loglevel` 配置。**
