# v13.3b 正确性评审报告（Ryan，正确性维度门禁）

- 日期: 2026-08-29
- 评审对象: `out/boot_mainline_v13_3.img`（sha256 `814ef070eb823d63acec76fc402e5d94c23da1f74861487589a6c25a5875f84c`，与 PACKAGE_NOTE 一致）+ `out/ramdisk_v13_3_acm.gz` + `out/init_v13_3_acm.sh`
- 评审方式: 只读审查 + /tmp 解包（/tmp/rev133b），未改动 out/、fork_linux/、bridge_backups/ 任何文件
- 参照基线: v13.2a 上板实证（8/26-8/27 串口取证日志 + `bridge_backups/products/products_v13_2a_20260826_032426/`）

---

## 总裁决: **GO-WITH-WARNINGS**

**无 FAIL 级（阻断上板）问题。** 镜像在算术、二进制一致性、内核契约三个层面全部过关，刷写本身是安全的：zImage 与已上板验证的 v13.2a 逐字节一致（eMMC CMD1 预热修复确认在二进制内），dtb 仅 initrd-end 一行差异且数值三重验证正确，ramdisk/init/busybox 完整可执行。按 PACKAGE_NOTE 命令刷写不会比 v13.2a 更差。

但发现 **1 项高危 WARN（W1）**：**boot.img 的 `--cmdline` 在本启动链上是无效参数**（板级日志与源码双重证明，详见清单 3-W1），`console=ttyS0,921600n8` 从未生效，实际生效的是 dts chosen bootargs（`console=tty0 console=ttyMT0,921600n8 ...`）。后果：**/init 的全部 [acm] 自报只会打到 LCD（/dev/console=tty0），串口上 `Run /init` 之后将一片寂静** —— PACKAGE_NOTE 写的验收标准"串口见 [acm] 自报(含 acm linked into c.1)"**不可能达成**。这不是 boot 失败（v13.2a 的 /init 输出同样没上过串口），但按现定验收流程刷完会被误判为失败。**刷写前必须改验收判据（改用 lsusb 0e8d:2006 + ttyACM 探测，或读 LCD），或在 /init 里直接向 /dev/ttyS0 节点回显自报。**

---

## ⚠️ BLOCKING

无。未发现任何会导致上板启动失败的实质缺陷。

---

## 逐项审查

### 1. /init 逐行逻辑

#### 1a. ramdisk 提取与 /init 一致性 — **PASS**

```
dd if=out/ramdisk_v13_3_acm.gz bs=1 skip=512 | gzip -dc | cpio -idmu  → /tmp/rev133b
sha256 /tmp/rev133b/init          = 3e66b48f757aeb21679a4ad514b84011f83948eea8b41c33b6314542d58e869e
sha256 out/init_v13_3_acm.sh      = 3e66b48f757aeb21679a4ad514b84011f83948eea8b41c33b6314542d58e869e
cmp → IDENTICAL（3159 字节，含 -rwxr-xr-x 执行位）
```
镜像内嵌 ramdisk 段（`boot.img @0x3a3000, 1,070,030 B`）与 `out/ramdisk_v13_3_acm.gz` **逐字节一致**（cmp 通过）。shebang `#!/bin/busybox sh`，/bin/busybox 存在（见 1b）。

#### 1b. busybox applet 完备性 — **PASS**

用 qemu-arm-static 实际运行镜像内 busybox（非仅 strings）：
```
qemu-arm-static /tmp/rev133b/bin/busybox --list   →  exit 0
BusyBox v1.36.1 (2026-07-06 23:24:34 CST)
```
脚本用到的全部 applet 均在列表内：**mount、mdev、mkdir、ln、ls、head、cat、setsid、getty、sh、echo** 全部存在。**硬需求 getty 与 setsid 双双在列**（`getty`、`setsid` 都出现在 --list 输出中）。另：`cmp` 确认此 busybox 与 v13.2a（已上板跑通 /init）ramdisk 内的 busybox **逐字节相同**，applet 集合无回归。

getty 用法实证（qemu 下对 pty 实测脚本第 60/67/73 行的命令形态 `getty -n -l /bin/sh 0 <tty> vt100`）：
- `--help` 明确记载 **"BAUD_RATE of 0 leaves it unchanged"** → 脚本中三处波特率 `0` 合法，不会触发 B0 挂断；
- pty 实测：getty 打开 tty 后立即 exec shell，`echo HELLO_GETTY_$((40+2))` 返回 `HELLO_GETTY_42` → **getty -n -l 路径功能正常**。

#### 1c. configfs 操作序列 — **PASS**

脚本顺序（out/init_v13_3_acm.sh 行号）：mkdir gadget(25) → idVendor(27) → idProduct(28) → bcdUSB(29) → bcdDevice(30) → strings/0x409(32-35) → configs/c.1 + strings + configuration + MaxPower(36-39) → functions/acm.usb0(40) → **ln -s 进 c.1(43)** → 写 UDC(49)。与内核 configfs gadget 契约（mandatory: idVendor/idProduct 先于 UDC、function 链接先于 UDC、UDC 最后写）逐条吻合；v13.3b 评审修复后 **ln -s 失败不再静默**（行 44 有成功/失败双分支自报）。

内核侧逐项核实：
- `0x0e8d` 这类带 0x 前缀的写法合法：`fork_linux/drivers/usb/gadget/configfs.c:175/214/231` 均为 `kstrtou16(page, 0, &val)`，base=0 接受十六进制；
- 挂载点 `/sys/kernel/config` 由内核创建：`fork_linux/fs/configfs/mount.c:146` `sysfs_create_mount_point(kernel_kobj, "config")`，且 `CONFIG_CONFIGFS_FS=y`（内建，无需 insmod）；
- 二进制层确认已编进发布 zImage（解压出 14.3MB Image 后 strings）：`configfs`×23、`usb_gadget`×27、`acm`×8、`ttyGS`×5、`musb-hdrc`×5、`musb-mtk`×1；
- UDC 侧：dtb `usb@11100000` `dr_mode = "peripheral"` + `status = "okay"`（内嵌 dtb 反编译 845-861 行），v13.2a 板上日志实锤 probe 成功：`musb-mtk 11100000.usb: probe: UDC should now be available in /sys/class/udc/`（musb-hdrc.2.auto）。
- 失败路径全部有自报兜底（gadget dir / c.1 / acm function / ln -s / UDC bind 五处 && || 分支），任何一步挂掉不会卡死 init，最终仍落到行 73 的 console getty。

#### 1d. 三个 getty 冲突分析 — **WARN（W2，轻微）**

- ttyGS0 后台 getty（行 60）：独立 tty，与其它两个无冲突。PASS。
- tty0 后台 getty（行 67）与行 73 `exec getty ... console`：由于 /dev/console 实际解析为 **tty0**（见 W1 推论，effective cmdline 里唯一注册的 console 就是 tty0），**这两路 getty 落在同一个 VT1 上**，两个 shell 竞争读同一 tty 输入（按键/输出互相穿插）。与 v13.2a 行为相同（非回归），且 LCD 无键盘实际不可用，影响轻微。建议后续二选一。
- 引号 bug 已修：v13.2a 旧 init 内层 `'exec $BB getty...'` 的 `$BB` 在单引号内不展开（bug）；v13.3b 改为字面 `/bin/busybox`，正确。

#### 1e. 语法检查 — **PASS**

```
qemu-arm-static busybox sh -n out/init_v13_3_acm.sh  → SYNTAX OK
dash -n out/init_v13_3_acm.sh                        → SYNTAX OK
```

### 2. initrd-end 算术与头字段 — **PASS**

```
body = dd skip=512 | wc -c            = 1,069,518 B (0x1051ce)
0x84100000 + 1,069,518                = 0x842051CE  == dts linux,initrd-end ✓
512B 头 magic(0x00-0x03)              = 88 16 88 58 ✓
512B 头 len(0x04-0x07, LE u32)        = 1,069,518 ✓（与 body 互为镜像）
"ROOTFS" @0x08                        ✓
```
三处数值（body 实测、MTK 头字段、dtb 属性）完全自洽。v13.2a 镜像内嵌 ramdisk 头字段同样核对：inner 1,068,865 → 0x84204f41，与历史配对表一致。

### 3. cmdline/ttyS0 断言 — **代码断言 PASS；board 实效 WARN（W1，本次评审最重要发现）**

**3.1 8250 口注册名是 ttyS0 —— PASS（代码级 + 板级双证）**
- `fork_linux/drivers/tty/serial/8250/8250_core.c:528`：`struct uart_driver serial8250_reg { .dev_name = "ttyS", ... }`；
- `8250_core.c:703` `serial8250_find_match_or_unused()` 分配首个空闲 slot（ARM 无 legacy ISA 口占用，mtk 口得 line 0）→ ttyS**0**；`8250_mtk.c:578` `data->line = serial8250_register_8250_port(&uart)`；
- `8250_mtk.c:676` `OF_EARLYCON_DECLARE(mtk8250, "mediatek,mt6577-uart", early_mtk8250_setup)` → cmdline `earlycon=mtk8250,0x11005000` 名字匹配成立；
- dts：`mt6572-z1.dts:20` `serial0 = &uart0`，`mt6572.dtsi:429` `uart0: serial@11005000`；
- **板上实证**（v13.2a 运行日志 `mainline_recon/z1_boot_mainline_20260826_123612.log` 等三份）：
  `[0.244398] 11005000.serial: <SERIAL> at MMIO 0x11005000 (irq = 56, base_baud = 1625000) is a ST16650V2`
  设备名 ttyS0 板级确认。**ttyMT0 在 mainline slim config 里不存在**（无 serial_mtk 驱动），`console=ttyMT0` 永远匹配不到任何 console。

**3.2 cmdline 合并顺序代码 —— 代码如评审提纲所述，但本板根本走不到这一步（W1）**
- 合并代码在解压器：`fork_linux/arch/arm/boot/compressed/atags_to_fdt.c:158-171`：`CONFIG_ARM_ATAG_DTB_COMPAT_CMDLINE_EXTEND=y` 时 `merge_fdt_bootargs()`（73-102 行）**dts bootargs 在前、ATAG cmdline 追加在后** → 若走到这一步，最后一个 console=（boot.img 的 ttyS0）确实胜出。
- **但板级日志证明 ATAG cmdline 从未合并进来**。铁证（`z1_boot_mainline_20260826_123612.log` / `20260827_000707.log`）：
  1. 内核打印的完整 command line 与 **dts chosen bootargs 逐字相同**；
  2. 其中 `panic=10 loglevel=7 clk_ignore_unused regulator_ignore_unused oops=panic sysrq_always_enabled rdinit=/init earlycon=mtk8250,0x11005000` **只存在于 dts bootargs**；
  3. LK 自己串口打印的 cmdline（= 它传给内核的 ATAG_CMDLINE，日志第一段 `cmdline: console=tty0 console=ttyMT0,921600n1 root=/dev/ram lcm=... printk.disable_uart=0 boot_reason=0 androidboot.serialno=<SERIAL> boot.img 专有参数**，也**不含** v13 镜像 boot.img 头里的 ttyS0 串——即 LK 根本不转发 boot.img 头的 cmdline；
  4. 若 ATAG 合并发生过，`lcm= / androidboot.serialno= <SERIAL> printk.disable_uart=` 必然出现在内核 command line 里——**全部缺席**。
- 旁证：dts 提交 `201a1c62b`（08-15）commit message 原话 "kernel uses DT chosen bootargs, not mkbootimg/LK"——项目在 8/15 已实证过一次，本次评审独立复核成立。
- 附带确认：v13.2a1（8/27 01:22，第一个带 ttyS0 cmdline 的镜像）**从未上板**（8/27 01:22 后无任何取证日志），"console=ttyS0 定案" 只改了 mkbootimg 参数、从未被板子验证过；AGENTS.md §3.2 与 PACKAGE_NOTE 第 49 行的该论断前提不成立。

**W1 推论链（高置信）**：effective cmdline = dts bootargs → `console=tty0 console=ttyMT0,921600n8` → 注册的 console 只有 tty0（VT）→ `/dev/console = tty0（LCD）`。因此：
- (a) /init 全部 echo（含 [acm] 五处自报）写向 LCD，**串口抓不到**；串口在 `Run /init as init process` 后静默（v13.2a 两次运行同样如此，`[init] welcome` 从未在任何串口日志出现过）；
- (b) 内核日志仍可见：mtk8250 bootconsole 在整个取证窗口内未被 disable（日志无 "bootconsole disabled" 行，消息持续到 0.79s 截止处）——串口保留"只读内核日志"能力；
- (c) 串口交互完全依赖 ttyGS0（ACM gadget）——这正是 v13.3 的设计路径，且 UDC/configfs/ACM 全链（1c）就绪，gadget 成败可用主机 `lsusb 0e8d:2006` + ttyACM 探测独立验证；
- (d) dts bootargs 里残留的 `console=ttyMT0` 不是"无害的一致性问题"，而是"实际生效的 cmdline 里根本没有串口 console"。**要改 cmdline 必须改 dts 并重编 dtb 重打包，改 mkbootimg --cmdline 无效。**

### 4. zImage 逐字节一致性 — **PASS**

```
boot.img 头: kernel_size=3,809,568（= 512B MTK KERNEL 头 + 3,809,056 payload）
kernel 段 @0x800, kernel_addr=0x10008000
payload(zImage+appended dtb) sha256 = cabcb152... (v13_3) / 8c40c302... (v13_2a，仅尾部 dtb 不同)

zImage 部分（payload 前 3,789,312 B）sha256：
  e099f0cddd1ef7912658089345352404533a7e07ec02994db26b049a569cbba0   ← v13_3 镜像内
  e099f0cd...                                                        ← v13_2a 镜像内（同值）
  e099f0cd...                                                        ← fork_linux/arch/arm/boot/zImage（同值）
  e099f0cd...                                                        ← bridge_backups/products/products_v13_2a_20260826_032426/zImage_appended_v13_2a 前 3,789,312 B（同值）
```
与任务给定期望 hash **完全一致**；四路互证（本轮镜像 / 已上板 v13.2a 镜像 / 源树产物 / 8-26 归档）。

**CMD1 预热修复二进制在位**：(1) zImage 与已上板验证的 v13.2a 逐字节相同；(2) 解压 zImage payload（xz 流 @0xf088，14,302,492 B Image）后 strings 命中 `Z1 CMD1 pre-warm` ×3、`Z1 CMD1 launch-pre`/`completion` 格式串；(3) 8/27 00:07 板上日志实证预热生效：`Z1 CMD1 pre-warm ready: i=3 OCR=c0ff8080`、`mmc0: new MMC card at address 0001`、`mmcblk0: mmc0:0001 H4G1d 3.64 GiB`；(4) 源码层：工作树 `fork_linux/drivers/mmc/host/mtk-sd.c:3397` `msdc_z1_prewarm_cmd1()` 在位，且 git HEAD `b1380bf32`（预热修复 commit）已在 main。

### 5. dtb 抽查（镜像内嵌 dtb 反编译） — **PASS**

用 `fork_linux/scripts/dtc/dtc -I dtb -O dts` 反编译 payload 尾部 19,744 B dtb：
```
chosen {
    linux,initrd-start = <0x84100000>;     ✓
    linux,initrd-end   = <0x842051ce>;     ✓
}
mmc@11120000      status = "okay"  (bus-width 1, max-frequency 50MHz, non-removable) ✓
usb@11100000      status = "okay", dr_mode = "peripheral"（ACM gadget 前提）✓
phy@11110800      status = "okay"  ✓
mmc@11130000      status = "disabled"（预期内）
```
**与 v13.2a 内嵌 dtb 反编译 diff：有且仅有一行** `linux,initrd-end = <0x84204f41>` → `<0x842051ce>`。与 PACKAGE_NOTE 声明完全一致。

### 6. boot.img 头 — **PASS**

```
magic       = ANDROID!                              ✓
kernel_addr = 0x10008000 (base 0x10000000 + 0x8000) ✓
ramdisk_addr= 0x11000000 (base + 0x1000000)         ✓
page_size   = 2048                                  ✓
tags_addr   = 0x10000100
second_size = 0
cmdline     = "console=tty0 console=ttyS0,921600n8 root=/dev/ram panic=10 loglevel=7 clk_ignore_unused regulator_ignore_unused oops=panic sysrq_always_enabled rdinit=/init earlycon=mtk8250,0x11005000"  ← 与 PACKAGE_NOTE 打包命令逐字一致 ✓
总大小      = 4,884,480 B ≤ 6,291,456 (6MB)         ✓
MTK 头两枚: kernel 段前 88168858/"KERNEL"/3809056；ramdisk 段前 88168858/"ROOTFS"/1069518 ✓
（mkbootimg load_mtk_file 检测到 ramdisk 文件自带 MTK 头故不再重复加头——镜像 ramdisk 段与 out 文件逐字节一致已 cmp 证实）
```
注：cmdline 字符串本身打包无误——无效的是"它会被内核采用"这一假设（见 W1）。

### 7. 对比 v13.2a 上板实证 — **PASS（无回归）**

- **zImage 未动**：见清单 4，四路同 hash。
- **dts 仅 initrd-end 一行差异**：见清单 5。
- **ramdisk 差异仅 /init**：v13.2a ramdisk（`boot_mainline_v13_2a.img` 内嵌，body 1,068,865）解包对比——busybox 与全部目录结构相同，差异为 /init 的 android_usb 块换成 configfs 块（+134 B body）；usr/bin 结构不变。
- **v13.2a 成功路径要素在 v13.3b 全部保持**：`Run /init as init process`（12:36 日志 0.566s）、mmc 枚举、UDC probe（musb-hdrc.2.auto）。boot 路径零改动，v13.3b 上板表现下界 = v13.2a。
- v13.2a 老 init 尾部三 getty 结构与 v13.3b 相同（含 `exec getty ... console`），其 [init] 输出同样从未出现在串口日志——W1 的行为 v13.2a 已然如此，非 v13.3b 引入。

### 8. ramdisk 内容完整性 — **PASS**

cpio 列表共 65 项：根目录、`init`（0755，3159 B，根下）、`bin/busybox`（ARM 静态 ELF 1,533,764 B）+ 40 余个 →busybox 符号链接（含 sh/getty/setsid/mdev/mount/ln/ls/head/cat）、目录 `dev/ etc/ newroot/ proc/ sbin/ sys/ tmp/ usr/{bin,sbin}` 全在。
`dev/` 目录在 cpio 内为空（无设备节点）——**无害**：内核内建默认 initramfs（`fork_linux/usr/default_cpio_list`：`nod /dev/console 0600 0 0 c 5 1`）先于外层 cpio 解包，/dev/console 一直存在；且 `fork_linux/init/main.c:1700` `console_on_rootfs()` 在 `run_init_process("/init")`（:1610）之前执行，init 的 0/1/2 已接到 /dev/console。脚本行 8-9 `mount tmpfs /dev + mdev -s` 会重建 console/tty0/ttyS0/ttyGS0 节点，行 73 的 getty 打开 /dev/console 无障碍。

---

## 问题清单（按严重度）

| # | 级别 | 问题 | 影响 | 建议 |
|---|------|------|------|------|
| W1 | **HIGH WARN**（不阻断启动，阻断既定验收） | boot.img `--cmdline` 在本启动链无效：LK 不转发 boot.img 头 cmdline、ATAG cmdline 未被合并进 DT（板级日志双证 + 8/15 commit 201a1c62b 旁证）。实际生效 cmdline = dts chosen bootargs（console=tty0 + ttyMT0 死参数）→ /dev/console=tty0(LCD)，`console=ttyS0,921600n8` 从未生效 | 串口看不到任何 [acm]/[init] 自报（`Run /init` 后静默）；PACKAGE_NOTE 验收标准"串口见 [acm] 自报"不可达成；无 UART 串口交互 shell（交互只剩 ttyGS0/USB） | ① 刷写前改验收判据：主机 `lsusb` 应现 0e8d:2006、/dev/ttyACM* 探测（注意与 C3 桥抢口风险，AGENTS §5.8）；LCD 可读自报。② 根治：把期望 cmdline 写进 `mt6572-z1.dts` chosen bootargs（如 console=tty0 console=ttyS0,921600n8 ...）重编 dtb 重打包——改 mkbootimg 参数永远无效。③ 低成本观测补丁：/init 里向 `/dev/ttyS0` 节点（mdev -s 会创建）直接 echo 自报，一行改动即可让串口看到五处 [acm] 报告 |
| W2 | LOW | tty0 后台 getty 与行 73 console getty 落在同一 VT1（/dev/console=tty0），双 shell 竞争同 tty 输入 | 仅 LCD 路径；板子无键盘，实际无感；v13.2a 同样如此 | 后续 /init 删掉行 67 或行 73 二选一 |
| W3 | INFO | 文档谬误需订正：AGENTS.md §3.2"v13.2a1 起定案 console=ttyS0"与 PACKAGE_NOTE 第 49 行"评审应改 ttyMT0 为误报"均建立在"boot.img cmdline 生效"的错误前提上；v13.2a1 从未上板 | 后续打包决策会被误导（继续在 mkbootimg 里调参无效） | 在 W1 处理时同步修文档 |
| N1 | INFO | cpio 内 dev/ 为空 | 无害：内核内建 default initramfs 提供 /dev/console | 无需处理 |
| N2 | INFO | dts bootargs 残留 `console=ttyMT0,921600n8` 死参数（无 serial_mtk 驱动，ttyMT0 永不存在） | 无直接危害（仅多一条匹配不到的 console=），但掩盖了"没有串口 console"的事实 | 随 W1 根治一并清理 |

---

## 结论

v13.3b 在"会不会比 v13.2a 更糟"意义上是干净的：zImage/dts/ramdisk 全链一致性与算术零瑕疵，boot 路径与已实证版本逐字节等价，**可以安全刷写**。但验收方案必须先按 W1 修正——按 PACKAGE_NOTE 现写的"串口见 [acm] 自报"验收，刷完必然误判失败。建议：接受 W1 修正案（改用 USB 侧验收）后执行刷写；若要求串口可见自报，先给 /init 打一行 `/dev/ttyS0` 直写补丁再重打包（ramdisk 算术需按新 body 同步重算 initrd-end 与 MTK 头长度字段）。

（评审证据均可在 /tmp/rev133b、/tmp/dtb_v133b.dts、/tmp/Image.bin 复现；本报告为唯一写入文件。）
