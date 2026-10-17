# ACM (ttyGS0) shell 卡 `>` 续行提示符 — 深度分析 (2026-08-15)

> 只读分析 + 根因判定 + 最小修复。不修改任何源/固件。
> 证据 = 源码级核对(busybox 1.36.1 真实构建 + fork_linux 内核 gadget 驱动) + qemu-arm pty 实测复现。

---

## 0. 结论一句话

**ttyGS0 卡 `>` 不是 "getty 没设 termios / ICANON 没开 / ICRNL 没开"。busybox getty 在 exec sh 前把 tty 设成了标准 cooked(ICANON|ICRNL|ECHO|ONLCR), 内核 tty 默认值也是 cooked。`>` 是 ash 的 PS2 续行提示符, 出现它 = shell 解析器已处于"未闭合构造"(未闭合引号/反引号/`$(`/续行)状态。真正的破坏点在本机当前 init 的 **ttyGS0 双 getty 竞争**(R2 等待循环 getty 加进去时没删掉旧直接 getty), 叠加 busybox ash 行编辑器在 ECHO 被临时清掉时退化的 `fgets` 回退路径 + ash 把 CR(0x0d)当普通单词字符(CWORD)而非换行(只有 LF/0x0a 是 CNL)。**

---

## 1. busybox getty 在 ttyGS0 上的 termios 行为(结论: 设置是**正确**的)

源码: `/home/<user>/claude/6572/build/busybox/busybox-1.36.1/loginutils/getty.c`
(板上 ramdisk 的 busybox 与该构建树 busybox md5 完全一致 `e83f528e4b106b4f27ba819835d98709`, 1.53MB 静态 ARM)

`getty -L -n -l /bin/sh 115200 ttyGS0 vt100` 的执行序列:

1. `open_tty()` (getty.c:211-241): 打开 /dev/ttyGS0 为 fd0 (O_RDWR|O_NONBLOCK), 随后 `ndelay_off` 去掉 O_NONBLOCK; dup2 到 1/2; TIOCSCTTY 抢控制终端; tcsetpgrp 置前台 pgrp。
2. `init_tty_attrs()` (getty.c:256-329): 先打成 **raw**(c_iflag=0, c_lflag=0, c_cc[VMIN]=1), 供读用户名用。
3. `-n` 跳过读用户名(`get_logname`), `G.eol` 保持默认 `'\r'`。
4. `finalize_tty_attrs()` (getty.c:331-408): **exec sh 前最终状态 =**
   - `c_iflag = IXON | IXOFF | ICRNL`(因 `G.eol=='\r'`, getty.c:337-339 明确加 ICRNL)
   - `c_oflag = OPOST | ONLCR`
   - `c_lflag = ICANON | ISIG | ECHO | ECHOE | ECHOK | ECHOKE | ECHOCTL`
   - `c_cflag = CBAUD(115200) | CS8 | HUPCL | CREAD`(`-L` 时再加 CLOCAL)
   - `c_cc`: VINTR=^C, VEOL='\n', VKILL=^U ...
   - 最后写一个 `"\n"`。
5. `BB_EXECLP("/bin/sh", "/bin/sh", "--", NULL, ...)` exec sh。

**判定**: 这个 termios 是标准 cooked 终端(ICANON + ICRNL + ECHO + ONLCR 全齐)。**假设 (a) ICANON 没设 / (b) ICRNL 没设 / (c) getty 没正确设置 tty —— 在 getty 路径上均不成立。**

补充: 即使不走 getty、直接 exec sh, ttyGS0 内核默认 `init_termios = tty_std_termios`
(`fork_linux/drivers/tty/tty_io.c:124-135`, `u_serial.c:1562-1571` 覆盖 c_cflag=B9600|CS8|CREAD|HUPCL|CLOCAL):
`c_iflag=ICRNL|IXON`, `c_oflag=OPOST|ONLCR`, `c_lflag=ISIG|ICANON|ECHO|...` —— 同样是 cooked。

---

## 2. ttyGS0 内核 gadget tty 层(结论: 不做任何 termios 改动)

- ttyGS0 来源: `CONFIG_USB_G_SERIAL` legacy g_serial (`fork_linux/drivers/usb/gadget/legacy/serial.c`), 实际 tty 驱动在 `u_serial.c`(`g_serial` 驱动, name=`ttyGS`)。
- `u_serial.c:911-922` `gs_tty_ops`: 只有 open/close/write/put_char/flush_chars/write_room/chars_in_buffer/unthrottle/break_ctl/get_icount —— **没有 set_termios/get_termios 回调**, 即 ICANON/ECHO/ICRNL 完全由 N_TTY 行规程实现, 驱动不干预。
- `f_acm.c:299-328` `acm_complete_set_line_coding`: 主机 SET_LINE_CODING 只 `port_line_coding = *value` **记下来, 不应用**到 tty termios(源码注释 "REVISIT: we currently just remember this data")。
- `f_acm.c:375-388` SET_CONTROL_LINE_STATE: 只存 `port_handshake_bits`。
- 结论: **主机侧(socat/CDC-ACM)的控制请求不会改动 ttyGS0 的 c_lflag/c_iflag**。termios 只由 getty/shell 自己控制。

---

## 3. 关键机制(为什么 CR/LF "不提交"、`>` 从哪来)

### 3.1 `>` 是 ash 的 PS2, 不是 PS1

- busybox ash PS1 默认 `PS1=\w \$ `(FANCY_PROMPT, ash.c:2142/2321), root 也是 `\w \$ `; **PS2 = `> `**(ash.c:2143 `"PS2=> "`)。
- PS2 只在解析器"行未闭合"时打: 未闭合 `'`/`"`/`` ` ``/`$(`/`\` 续行等(`setprompt_if(needprompt,2)` 调用点 ash.c:13040/13187/13243/13385/10986)。
- 实测(pty + qemu-arm, 同一 busybox): 输入 `'` 或 `"` 或 `` ` `` 或 `$(` 后, 后续所有行都回 `> ls\r\n> `, **命令永不执行** —— 与现场描述完全一致。

### 3.2 ash 词法: CR(0x0d)是普通单词字符, 只有 LF(0x0a)是命令终结

- busybox ash 语法表(`shell/ash.c:3120-3150`): **byte 0x0d = `CWORD`**, byte 0x0a = `CNL`。
- `readtoken1`(ash.c:12443-12682): `CNL` 在 BASESYNTAX 下 `goto endword` 结束命令; `CWORD` 直接拼进单词。
- 含义: **CR 送到 ash 词法器 = 变成命令单词的一部分, 不是换行**。`ls\r` → 命令名 `ls\r` → 等真正的 LF。若主机 Enter 只发 CR(裸 `\r`)、且 tty 没把 CR→LF(ICRNL), 则永远等不到 LF → "回车不提交"。
- 实测(管道非交互喂 `\r`/`ls\r`/`ls\r\n`): `sh: \r: not found` / `sh: ls\r: not found` —— 证明 CR 确实进了单词。

### 3.3 busybox ash 行编辑器(FEATURE_EDITING)覆盖一切 —— 但只在"交互"时

- 板上 busybox `CONFIG_FEATURE_EDITING=y`, `CONFIG_SH_IS_ASH=y`。
- `preadfd()`(ash.c:10796-10805): 交互(`iflag`)且读 stdin 时走 `read_line_input()` 行编辑器, 否则 `nonblock_immune_read()` 直读。
- `iflag=1` 需要 `isatty(0) && isatty(1)`(ash.c:14562-14568)。getty 把 ttyGS0 dup2 到 0/1/2 → isatty 满足 → **正常应走行编辑器**。
- 行编辑器(`libbb/lineedit.c`): 自己把 tty 打成 raw, 自己回显, **Enter 同时认 `\n` 和 `\r`**(lineedit.c:2588-2595), 提交时**补一个 `\n` 进返回行**(lineedit.c:2999-3002)。→ 行编辑器激活时 CR/LF/CRLF **全部能提交**。

### 3.4 行编辑器退化陷阱(fgets 回退) —— 本机最可能的实际死法

`read_line_input` 开头(lineedit.c:2478-2495):
```c
n = get_termios_and_make_raw(fd, &new_settings, &initial_settings, 0 | TERMIOS_CLEAR_ISIG);
if (n != 0 || (initial_settings.c_lflag & (ECHO|ICANON)) == ICANON) {
    /* 退化路径: 不打 raw, 不用行编辑 */
    parse_and_put_prompt(prompt);
    if (fgets(command, maxsize, stdin) == NULL) ... 
}
```
触发条件 = **ICANON 置位 + ECHO 被清掉**。
- 退化后 `fgets()` 在 canonical 模式等 `\n`:
  - 若 ICRNL 还在 → CR 被映射成 LF → `ls\r` 能提交(无害)。
  - 若 ICRNL 也被清(如竞争 getty 的 raw 窗口 c_iflag=0 残留)→ 主机只发 `\r` → `fgets` **永久阻塞**: 字符被 canonical ECHO 回显, 但行永不提交。**这就是"命令回显了但换行不提交"的最直接形式。**
- ECHO 什么时候会被清? getty `init_tty_attrs` 把 c_lflag 清零(raw 窗口) + 竞争 getty 时序交错时, shell 读到 ECHO=0 的 termios; 或一次被打断的行编辑会话把 tty 留在半成品状态。

### 3.5 双 getty 竞争(init 结构 bug, 本机实锤)

当前 ramdisk(`out/ramdisk_mainline_connsys.gz`, repack_ramdisk_connsys.sh `Z1_GETTY_FIX=1` 产物)init 里 **ttyGS0 有两个 getty**:

```
( i=0; while [ $i -lt 600 ]; do
    if [ -e /dev/ttyGS0 ]; then exec $BB getty -L -n -l /bin/sh 115200 ttyGS0 vt100; fi
    $BB mdev -s; $BB sleep 1; i=$((i+1)); done
) </dev/null >/dev/null 2>&1 &          ← 等待循环 getty(未 setsid)
...
$BB setsid $BB sh -c 'exec $BB getty -L -n -l /bin/sh 115200 ttyGS0 vt100' </dev/ttyGS0 >/dev/ttyGS0 2>&1 &   ← 旧直接 getty
```

- R2 的 python 注入只是把 marker 注释替换成等待循环块, **没有删掉下面的旧直接 getty 行**(repack 脚本见 `tools/repack_ramdisk_connsys.sh:75-95`)。
- 后果 A: 等待循环 getty 由 `( ) &` 起在 init 会话的后台进程组里, **不是会话组长** → getty.c:568-585 `setsid()` 失败且 `getsid(0)!=pid` → 直接 `bb_simple_perror_msg_and_die("setsid")` 死掉, 错误进 /dev/null。→ 等待循环是**死代码**(除非 ttyGS0 节点在循环起之前已存在且被 setsid 包装)。
- 后果 B: 若 `/dev/ttyGS0` 在 init 执行旧直接 getty 行时**还不存在**(gadget 异步 probe), 该行的 `</dev/ttyGS0` 重定向失败 → **整个直接 getty 也不起** → ttyGS0 上根本没有 shell。若节点已存在, 两个 getty 竞争 TIOCSCTTY/前台 pgrp, termios 被两个 getty 各自 raw→cooked 来回覆盖, 两个 shell 分抢输入字节。
- 后果 C: 竞争输家 getty 报 `TIOCSCTTY: Operation not permitted` 死在 ttyGS0 上(旧直接 getty 的 stderr 是 `2>&1`→ttyGS0, 用户会看到这行噪声)。

**这就是现场 `>` 的温床**: 单一 getty 链路(3.3)实测全部正常; 双 getty 竞争 + 行编辑器 fgets 退化 + CR 当 CWORD, 三者叠加才能在真机上复现"回显但停在 `>`、CR/LF 都不提交"。

---

## 4. 复现证据(本机 qemu-arm + pty, 同款 busybox 二进制)

| 场景 | 结果 |
|---|---|
| 单 getty `-L -n -l /bin/sh 115200 -`, 喂 `ls\n` / `ls\r` / `ls\r\n` / 逐字符人类输入+CR / 粘贴 CRLF | **全部正常执行** `ls`, 提示符 `$ ` |
| 单 getty, 喂 `echo hi` + CR/LF | 正常执行 |
| 喂 `'` 或 `"` 或 `` ` `` 或 `$(` | **出现 `> ls\r\n> ` 持续续行, 命令不执行**(复现现场 `>` 形态) |
| 管道非交互喂 `ls\r` | `sh: ls\r: not found`(CR 进单词) |
| 双 getty 竞争同一 tty | 输家 `TIOCSCTTY: Operation not permitted`; 输入分抢/混乱 |

结论: **getty+ash 单链路本身无问题; `>` 必须由"未闭合构造"或"双 shell 分抢输入"产生。**

---

## 5. 修复建议(最小 + 稳健)

### 5.1 必修: 修掉双 getty(根因级)

只留 **一个** ttyGS0 getty, 且等待循环版必须 `setsid`(否则死):
```sh
# 删掉旧直接 getty 行; 等待循环改成:
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

### 5.2 建议: getty 前显式 stty 兜底(消除 fgets 退化/ICRNL 漂移)

在 getty 前对 ttyGS0 强设 cooked:
```sh
$BB stty -F /dev/ttyGS0 115200 icanon icrnl onlcr echo isig ixon opost 2>/dev/null
```
- `icanon` 保行缓冲、`icrnl` 保 CR→LF、`echo` 保回显(同时确保行编辑器走正常路径, 不进 fgets 退化)、`onlcr` 保输出换行。
- getty 的 `finalize_tty_attrs` 会再设一次同值, 幂等无害。
- 这与任务假设 (a)(b) 的"修复"方向一致, 但**真正作用是把 fgets 退化分支和 ICRNL 漂移兜死**, 而不是"补 getty 没设的位"。

### 5.3 可选: 干脆不用 getty, 直接交互 sh(最简恢复 shell)

busybox ash 交互模式自带行编辑器, CR/LF/CRLF 都能提交, 不需要 getty 的 termios 仪式:
```sh
(
  i=0
  while [ $i -lt 600 ]; do
    if [ -e /dev/ttyGS0 ]; then
      exec setsid /bin/sh -i </dev/ttyGS0 >/dev/ttyGS0 2>&1
    fi
    $BB mdev -s 2>/dev/null; $BB sleep 1; i=$((i+1))
  done
) </dev/null >/dev/null 2>&1 &
```
注意: 少掉 getty 的 TIOCSCTTY/前台 pgrp 设定, 交互 sh 无 job control(可接受); 行编辑器仍靠 isatty 激活。

### 5.4 主机侧(配合)

- socat 用 raw 模式且**发送 CRLF 或 LF 提交**(`socat - /dev/ttyACM0,raw,echo=0`), 避免"只发裸 CR"。
- 若坚持 CR 提交, 必须保证设备侧 ICRNL 在(5.1/5.2 已兜)。

### 5.5 getty 参数层面

- `-L`(CLOCAL): 保留 —— ACM 无真实载波, 不加会在主机断开时误判 DCD 掉线(SIGHUP 杀 shell)。
- `-i / -f`: 只影响 /etc/issue 显示, **与 termios 无关, 不需要**。
- `-n`: 保留(不读用户名, 直接 exec sh)。

---

## 6. 现场核对清单(上板后 30 秒验证)

1. `cat /proc/tty/driver/gs`(或 `ls -la /dev/ttyGS0`) → ttyGS0 存在。
2. `ps | grep -E 'getty|sh'` → 确认 ttyGS0 上**只有一个** getty。
3. shell 里 `stty -a < /dev/ttyGS0` → 确认 `icanon icrnl echo onlcr` 在位。
4. shell 里 `echo $-` → 含 `i`(交互, 行编辑器激活)。
5. 敲命令: 用 CR、LF、CRLF 各试一次 → 都应提交。

---

## 7. 证据行号索引

| 项 | 位置 |
|---|---|
| getty finalize 设 cooked(ICANON\|ICRNL\|ECHO) | `build/busybox/busybox-1.36.1/loginutils/getty.c:331-408`, ICRNL 见 337-339, lflag 见 367 |
| getty init 先打 raw | getty.c:313-326 |
| getty setsid 失败即死(等待循环 getty 死因) | getty.c:567-596 |
| u_serial init_termios = tty_std_termios | `fork_linux/drivers/usb/gadget/function/u_serial.c:1562-1571` |
| tty_std_termios 默认 cooked | `fork_linux/drivers/tty/tty_io.c:124-135` |
| gs_tty_ops 无 set_termios | u_serial.c:911-922 |
| f_acm 只记 line coding 不应用 | `fork_linux/drivers/usb/gadget/function/f_acm.c:299-328` |
| ash PS2=`> ` | `shell/ash.c:2143` |
| ash 0x0d=CWORD / 0x0a=CNL | ash.c:3120-3150(byte 13 / byte 10) |
| ash 交互才走行编辑器 | ash.c:10796-10805; iflag 需 isatty(0)\|isatty(1) 见 14562-14568 |
| 行编辑器 Enter 认 `\n`/`\r` | `libbb/lineedit.c:2588-2595` |
| 行编辑器 fgets 退化条件(ICANON 且无 ECHO) | lineedit.c:2478-2495 |
| 行编辑器提交补 `\n` | lineedit.c:2999-3002 |
| 当前 init 双 getty | `ramdisk_mainline_connsys.gz` 解出 init 第 26-41 行(repack 脚本 `tools/repack_ramdisk_connsys.sh:75-95` 只加循环没删旧行) |

---

## 8. 结论

- **任务假设 (a)(b)(c) 均不成立**: getty 正确设置了 ICANON/ICRNL/ECHO; 内核默认也 cooked; 真正决定 Enter 提交的是 busybox ash 行编辑器(CR/LF 都认)。
- **根因 = 当前 init 的 ttyGS0 双 getty 竞争**(等待循环 getty 是未 setsid 的死代码 + 旧直接 getty 并存), 竞争造成 termios/前台 pgrp 抖动、两 shell 分抢输入; 叠加行编辑器在 ECHO 被清时的 `fgets` 退化(ICRNL 若同时被清, 主机裸 CR 就永久阻塞)与 ash 把 CR 当 CWORD 的词法特性, 表现为 `>`(PS2)续行、命令不提交。
- **最小修复 = 5.1(单 getty + setsid 等待循环)+ 5.2(getty 前 stty 兜底)**; 5.3(直接交互 sh)为最简备选; 主机侧 5.4 配合。
