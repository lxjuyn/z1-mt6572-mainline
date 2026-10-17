# Z1 mainline 交互方案汇总 (2026-08-15)

> 汇总 3 份只读分析报告, 给出一句话根因 + 最小修复 + 方案优先级 + 上板验证点。
> 本汇总为只读产物, 未改任何源/固件/ramdisk。改前一律先备份(项目规则)。

---

## 0. 报告清单与状态

| 报告 | 状态 | 一句话主题 |
|---|---|---|
| `mainline_recon/UART_CONSOLE_DEEP_20260815.md` | ✅ 存在 (08-15 22:52) | CH340/ttyS0 收不到根因 + 修复方向 |
| `mainline_recon/ACM_TERMIOS_DEEP_20260815.md` | ✅ 存在 (08-16 00:12) | ttyGS0 卡 `>` 续行根因 + 最小修复 |
| `mainline_recon/GETTY_WEB_20260815.md` | ✅ 存在 (08-16 00:21) | 联网交叉验证, 与 ACM 本地分析互相印证 |

**缺失: 无。** 注意: 三份报告均**未覆盖 ADB**, ADB 备选方案无证据基础, 需另行调研才能列为选项。

---

## 1. UART (CH340/ttyS0) 收不到 — 根因 + 最小修复

### 1.1 身份确认
- 板上物理串口 (原厂 ttyMT0) 在 mainline 下 = **uart0 @ 0x11005000 = ttyS0** (`8250_mtk.c` 走通用 8250 框架, 分配 ttyS%d)。ttyMT0 是原厂 vendor 名字, mainline 不存在。
- 铁证 (17:10 log 行 947): `11005000.serial: <SERIAL> at MMIO 0x11005000 (irq = 56, base_baud = 1625000) is a ST16650V2`。

### 1.2 根因 (行为层, 有日志支撑)
**dts / console 注册链无回归; 死的是 printk→UART 的 console 轮询写路径, 在 boot 早期 ~0.69s 就 wedge。**
- 两条独立路径: `serial8250_console_write` (轮询 LSR/THR, printk 用) vs getty 的 tty 中断 TX (shell 用)。**console 死但 shell 活** = 两条路径分离的直接证据。
- 回归时间线: 17:10/17:45 (`ignore_loglevel`) 完整 boot log + `console [ttyS0] enabled` 全有; 17:52 起 bootargs 切 `loglevel=3/4/6` 后, 8250 probe 行与 `console [ttyS0] enabled` 全部消失, 但 boot 初期 13 条消息 + `[init]` + shell 仍在 → UART 硬件没死。
- `loglevel=6` + printk_ratelimit 掩盖故障: 连 `console [ttyS0] enabled` 这条 pr_info 都走已 wedge 的路径不再打印。
- 21:19 commit `1d3d5a0c0` 团队已把交互主力切到 USB ACM (`avoids CH340 unreliability`); 21:31 30min 0 字节 = 设备稳定无复位 + console 路径不输出 + 无人敲 CH340 的天然结果。

### 1.3 排除项 (别再查)
- uart0 `status=okay` (当前 stable dtb 实测)、compatible/clocks (CLK_TOP_UART0_SEL=25 / UART0=65 全匹配)/irq 都对。
- `CONFIG_SERIAL_8250_CONSOLE=y` + `univ8250_console("ttyS")` + `console=ttyS0` 全链完好, 同一内核 zImage #91 17:45 注册成功过。
- tty0 不独占 console (两个 console 都收 printk, 顺序只决定 /dev/console primary); earlycon 未启用不参与。
- 注: dts `GIC_SPI 31` 实测落 Linux IRQ 56 (=32+24), 历史上 TX/RX 都工作过, 暂非故障点; 若出现"只 TX 通 RX 死"再优先查此 IRQ 映射。

### 1.4 两个潜在隐患 (WDT 之外)
- `8250_mtk.c:429-442 mtk8250_runtime_suspend` 有 `while (serial_in(up, MTK_UART_DEBUG0));` **无限忙等** — getty 关闭 ttyS0 时触发, 是"shell 突然僵死"候选。
- WDT 30.5s 复位背景 (`busybox watchdog -t 5 -T 10` 喂狗不稳), 会掩盖/混淆 UART 观测。

### 1.5 最小修复 (在设备屏幕 shell 上执行, 只读优先)
先区分 **"8250 没 probe" vs "console 写路径 wedge"**:
1. `ls -l /dev/ttyS0 /dev/console` — 设备在否
2. `cat /proc/tty/driver/serial` — 应有 `0: uart:ST16650V2 port:000011005000 irq:56 tx:N rx:N`, tx 计数增长 = TX 路径通
3. `dmesg | grep -iE "11005000.serial|console \\[ttyS0\\]|Serial: <SERIAL>"` — 8250 probe 与 `console [ttyS0] enabled` 在否
4. TX 直测 `echo TEST_TX > /dev/ttyS0`; RX 直测 `cat /dev/ttyS0 &` 后 CH340 敲字
5. `cat /proc/sys/kernel/printk` (当前应 `6 4 1 7`); 临时 `echo 8 > /proc/sys/kernel/printk` 看 console 是否复明
6. 若 `/dev/ttyS0` 在但 TX 死: `stty -F /dev/ttyS0 115200 raw clocal` 重设; 或重启保持 CH340 capture 看 preloader banner (F0/F3/`Jump to BL`) — **banner 都不出 → 接线/CH340 侧问题, 与内核无关**
7. 若 `/dev/ttyS0` 不存在: 查 dmesg mtk8250 probe 是否 deferred/报错 (时钟 `devm_clk_get` "baud"/"busy")

**最终修复选项 (按成本从低到高)**: (a) 调 bootargs 降 loglevel / 去 printk_ratelimit / 回 `ignore_loglevel`; (b) 修 `mtk8250_runtime_suspend` 忙等; (c) 若需早期 console 拿 boot 诊断, 回到带 earlycon 的 DEBUG_LL 配置。

---

## 2. USB ACM (ttyGS0) 卡 `>` 续行 — 根因 + 最小修复

### 2.1 一句话根因
**不是 termios 没设 / ICANON 没开 / ICRNL 没开。** getty 在 exec sh 前把 tty 正确设成 cooked (`ICANON|ICRNL|ECHO|ONLCR`), 内核 g_serial 默认也是 cooked。**真凶 = 当前 ramdisk init 的 ttyGS0 双 getty 竞争** (等待循环 getty 加进去时没删旧直接 getty; 且等待循环 getty 未 `setsid`, getty `setsid()` 失败即死=死代码), 叠加 busybox ash 行编辑器在 ECHO 被清时的 `fgets` 退化 (ICRNL 若同被清, 主机裸 CR 永久阻塞) + ash 词法把 CR(0x0d) 当 CWORD 而非 CNL (只有 LF/0x0a 终结命令)。

### 2.2 机制链 (证据)
- `>` = ash **PS2 续行提示符** (ash.c:2143 `"PS2=> "`), 只在解析器"行未闭合"时打 (未闭合 `'`/`"`/`` ` ``/`$(`/`\` 续行)。
- 单 getty 链路实测 (qemu-arm + pty, 同款 busybox 二进制): 喂 `ls\n`/`ls\r`/`ls\r\n` 全部正常执行; 喂 `'`/`"`/`` ` ``/`$(` 才出持续 `>` — **getty+ash 单链路本身无问题**。
- 双 getty 竞争: 输家 `TIOCSCTTY: Operation not permitted`; termios 被两个 getty 各自 raw→cooked 来回覆盖; 两个 shell 分抢输入字节。
- fgets 退化条件 (lineedit.c:2478-2495): **ICANON 置位 + ECHO 被清** → 不打 raw、不用行编辑, 退回 canonical `fgets` 等 `\n`; 若 ICRNL 也被清 → 裸 CR 永远凑不齐 LF → 命令回显但行不提交。
- getty 正确性 (排除项): `finalize_tty_attrs` 显式设 `ICANON|ISIG|ECHO|ECHOE|ECHOK|ECHOKE|ECHOCTL` + `ICRNL` (因 `G.eol=='\r'`); `-n` 不影响该步。内核侧 `u_serial.c` `init_termios = tty_std_termios` (cooked), 无 `set_termios` 回调 (termios 全由 N_TTY 管), `f_acm.c` 对主机 SET_LINE_CODING 只记录不应用。**主机 CDC 控制请求不会改 ttyGS0 的 iflag/lflag。**

### 2.3 最小修复 (ramdisk init 层, 不需改内核)
**必修 5.1 + 5.2, 备选 5.3, 主机配合 5.4。** 改动都在 `tools/repack_ramdisk_connsys.sh` 处理的 init 脚本里 (ACM_TERMIOS_DEEP §5)。

```sh
# 5.1 单 getty + setsid 等待循环 (删掉旧直接 getty 行)
(
  i=0
  while [ $i -lt 600 ]; do
    if [ -e /dev/ttyGS0 ]; then
      exec setsid $BB getty -L -n -l /bin/sh 115200 ttyGS0 vt100
    fi
    $BB mdev -s 2>/dev/null; $BB sleep 1; i=$((i+1))
  done
) </dev/null >/dev/null 2>&1 &
```
```sh
# 5.2 getty 前显式 stty 兜底 (钉死 ICRNL/ECHO/ICANON, 消除 fgets 退化与 raw 窗口漂移)
$BB stty -F /dev/ttyGS0 115200 icanon icrnl onlcr echo isig ixon opost 2>/dev/null
```
```sh
# 5.3 最简备选: 不用 getty, 直接交互 sh (行编辑器自带 CR/LF/CRLF 提交; 无 job control 可接受)
exec setsid /bin/sh -i </dev/ttyGS0 >/dev/ttyGS0 2>&1
```
- getty 参数: `-L`(CLOCAL, ACM 无载波,**必须**)、`-n`(跳过用户名,**必须**配 `-l /bin/sh`)、`-i` 不需要 (与 termios 无关)。
- 主机侧 (5.4): socat 用 raw 且**发 CRLF 或 LF 提交** (`socat - /dev/ttyACM0,raw,echo=0`), 别只发裸 CR; 若坚持 CR 提交必须保设备侧 ICRNL。

---

## 3. 可靠交互方案推荐 (优先级)

**推荐排序: 1. 修 ACM termios/init (根因级, 首选) → 2. UART 仅作 boot 诊断/兜底 → 3. ADB (需另行调研, 当前无证据)。**

| 优先级 | 方案 | 成本 | 风险 | 理由 |
|---|---|---|---|---|
| **1 (首选)** | **修 ACM: 单 getty + setsid + stty 兜底** | 低 (ramdisk init 编辑 + 重打包, 0 内核改动) | 低 | 根因明确 (双 getty 结构 bug, 源码+qemu 实测+web 三方印证), 修复=删一行加两行; 团队 21:19 已把 ACM 定为主交互通道, 方向一致; getty+ash 单链路被证正常, 修完预期直接可用 |
| **2** | **UART (ttyS0) 降级为 boot 诊断通道** | 中 (需先 §1.5 区分 probe vs console wedge, 再选调 bootargs/修 runtime_suspend/回 earlycon) | 中 | 根因在 printk→console 轮询写路径 wedge, 修复方向多且需上板实测迭代; 但 CH340 是唯一不依赖 USB gadget 的硬件 console, **留作看 boot 早期 log 的兜底通道** (尤其后续 kernel 调试需要 console 输出时) |
| **3** | **ADB** | 未知 | 未知 | 三份报告均未覆盖, 无证据基础; 仅在 ACM 修复后仍不稳时调研 (需查 mainline f_adb/functionfs 支持、Z1 是否编 CONFIG_USB_CONFIGFS_F_FS 等)。当前**不列为可执行选项** |

**判定逻辑**: ACM 卡 `>` 是纯 ramdisk init 脚本 bug, 修复成本远低于 UART console wedge 的根因排查; 且 ACM 是当前团队已切的主通道。**先修 ACM 拿回交互 shell, 再用 §1.5 的只读清单在 ACM shell 里诊断 UART, 最后决定是否要修 UART console。** ADB 作为 UART/ACM 都不可用时的最后备选, 启动前先补一份 ADB 可行性调研。

---

## 4. 上板验证点

### 4.1 ACM 修复后 (30 秒, 应全过)
1. `cat /proc/tty/driver/gs` (或 `ls -la /dev/ttyGS0`) → ttyGS0 存在。
2. `ps | grep -E 'getty|sh'` → ttyGS0 上**只有一个** getty (无 `TIOCSCTTY: Operation not permitted` 噪声)。
3. shell 里 `stty -a < /dev/ttyGS0` → `icanon icrnl echo onlcr` 在位。
4. shell 里 `echo $-` → 含 `i` (交互, 行编辑器激活)。
5. 分别用 CR / LF / CRLF 敲命令 → 都应提交执行。
6. 若仍 `>`, 敲 `'` 验证 PS2 机制 (正常执行变续行) → 复测确认双 getty 是否清干净。

### 4.2 UART 诊断 (在 ACM 或屏幕 shell 上做, 见 §1.5 1-7)
- 关键判据: `cat /proc/tty/driver/serial` tx 计数增长 = TX 通; `echo 8 > /proc/sys/kernel/printk` 后 console 是否复明; 重启保持 CH340 capture 看 preloader banner 区分内核问题 vs 接线问题。
- 上板前先确认主机侧 `/dev/ttyACM0` 存在 (报告作者检查时该节点不存在, 桥疑似未插) — **先查桥供电/接线, 排除"根本没接上"的低级原因**。

### 4.3 回归/环境注意
- 改 ramdisk 前备份带时间戳到 `bridge_backups/`; 改完按项目规则立即可编译/可上板即 commit+push (仓库禁密钥)。
- WDT 30.5s 复位背景会干扰观测: 验证时注意喂狗状态 (`busybox watchdog -t 5 -T 10` 时好时坏)。

---

## 5. 待定/缺口
- ADB 备选: 三份报告无覆盖, 需另派调研 (mainline f_adb / configfs f_fs / g_android / Z1 USB 已编 musb-peripheral 的兼容性)。
- UART console wedge 的最终修法未定: 需上板按 §1.5 区分后再选 (降 loglevel / 修 runtime_suspend 忙等 / 回 earlycon 配置)。
- irq 56 vs dts `GIC_SPI 31` 的映射差异: 历史工作, 暂不追, 仅"只 TX 通 RX 死"时优先查。
