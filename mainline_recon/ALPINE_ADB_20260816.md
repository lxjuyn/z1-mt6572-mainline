# Z1 Alpine 发行版内 ADB 集成方案 — ALPINE_ADB_20260816

> 2026-08-16。**只读 + 联网分析产物**（板子关机；未改任何本地文件/rootfs/镜像，唯一落盘为本报告）。
> 目标：Alpine 发行版（`system_alpine.img` → p4 system）启动后，**自动挂 USB ADB gadget + 起原厂静态 adbd**，主机侧直接 `adb shell` 交互 + 用 root shell 测非重要驱动。
> 前置：`out/boot_mainline_z1_adb.img`（内核 configfs/ffs，见 `ADB_KERNEL_20260815.md`）+ Alpine 启动链（`ALPINE_BOOT_PATH_20260815.md` / `ALPINE_IMPLEMENT_20260816.md` 方案 A：initramfs switch_root p4）。
> 一句话结论：**Adbd 用原厂静态二进制零编译复用（自带 FunctionFS，strings 铁证）；在 Alpine 里写一个 `/etc/init.d/adbd` OpenRC 服务：挂 configfs → 建 0e8d:200e(adb+acm) gadget → 挂 functionfs → 起 adbd → 绑 UDC。关键修正：f_fs 要求"先起 adbd 后绑 UDC"（源码 `f_fs.c` 铁证，现成 `z1_adb_setup.sh` 顺序反了会 ENODEV）。Android 残留（/system/bin/sh、/dev/log、shell 用户、RSA 免认证）全部可用 `ln -s`+`adduser`+建目录解决；唯一真风险 = adbd 是否降权到 shell（ro.debuggable 缺省），有 3 级兜底。**

---

## 1. 结论摘要 (TL;DR)

1. **内核侧零改动可用**：当前 `fork_linux/arch/arm/boot/zImage`（8月16 01:37, sha `1eb98f3b…`）就是 ADB 版内核 —— 证实：`out/boot_mainline_z1_adb.img` 的 kernel 段 = 该 zImage + 拼接 dtb（3792548 = 3772928 zImage + 19620 dtb，逐字节成立），`.config` 含 `CONFIG_USB_CONFIGFS=y` `CONFIG_USB_CONFIGFS_F_FS=y` `CONFIG_USB_CONFIGFS_ACM=y` `CONFIG_USB_F_FS=y` `CONFIG_USB_F_ACM=y`，且 **`CONFIG_USB_G_SERIAL` 已关**（不抢 UDC）。UDC=`musb-hdrc`。Alpine boot 复用同一 zImage（appdtb 打包）即自带 ADB 能力。
2. **adbd 零编译**：`out/boot_extracted/rd/sbin/adbd`（202968B，static ARM EABI5，无 INTERP/NEEDED）strings 铁证自带 FunctionFS（`/dev/usb-ffs/adb/ep0..ep2` + `[ usb_init - using FunctionFS ]` + `init_functionfs`），先走 functionfs，失败才退 `/dev/android_adb_enable`（mainline 无，正好用不上）。Alpine/musl 上直接跑。
3. **核心方案** = 写 `/etc/init.d/adbd`（OpenRC，`#!/sbin/openrc-run`），`rc-update add adbd default`。开机自动：挂 configfs → 建 `0e8d:200e`(adb+acm) gadget → `functions/ffs.adb` + `functions/acm.usb0` → 挂 functionfs 到 `/dev/usb-ffs/adb` → **先起 adbd → 再绑 `musb-hdrc`**（顺序关键，见 §4.2）→ Android 残留缓解。
4. **顺序修正（本项目新发现）**：现成 `scripts/z1_adb_setup.sh`（也是 ADB 版 ramdisk 里那份）**先绑 UDC 后起 adbd —— 与 mainline f_fs 的 `desc_ready` 机制相悖**：`f_fs.c:3509` `ffs_do_functionfs_bind()` 在 `!ffs_opts->dev->desc_ready` 时返回 `-ENODEV`，`desc_ready` 要等 adbd 打开 ep0 写入描述符（`f_fs.c:4266` `ffs_ready()`）才置位 → 先绑 UDC 会让 composite bind 失败、gadget 不枚举。**Alpine 版脚本必须"先 adbd 后 UDC"，并加 UDC 写入重试**（§4.2）。
5. **Android 残留全可缓解**：`/system/bin/sh`→`ln -s /bin/sh`；`shell:x:2000:2000` 进 `/etc/passwd`+`/etc/group`；`mkdir /dev/log`（日志静默降级，非致命）；`ro.adb.secure` 缺省→免 RSA（Z1 原厂就是 0）；`ADB_EXTERNAL_STORAGE` 指向 `/mnt/data` 让 `adb pull` 有落点。唯一真风险 = **adbd 是否降权到 shell（uid 2000）**（ro.debuggable 缺省为空 + ro.secure 缺省 "1" → `should_drop_privileges()` 大概率返回真）。§5 给 3 级兜底，上板 `adb shell id` 一测便知。
6. **上板验证**：刷 p4(android)=Alpine + 刷 bootimg=ADB 内核 Alpine switch ramdisk → 拔线电池开机 → C3 桥登 ttyS0 → `rc-service adbd start` → 内核起来后插 USB → 主机 `adb devices`/`adb shell id`/`insmod` 验非重要驱动。

---

## 2. 现状盘点（本报告二次核实，证据列在每行）

| 项 | 值 | 证据 |
|---|---|---|
| 运行内核 | mainline 7.0-rc7, vermagic `7.0.0-rc7-z1+`，**USB configfs/ffs 全开** | `.config`: `USB_CONFIGFS=y` `USB_CONFIGFS_F_FS=y` `USB_CONFIGFS_ACM=y` `USB_F_FS=y` `USB_F_ACM=y`；`# CONFIG_USB_G_SERIAL is not set`（关键，不抢 UDC） |
| ADB boot.img | `out/boot_mainline_z1_adb.img`（6127616B ≤ 6MiB, `ANDROID!`, ksz=0x39e0a4, **appended dtb** at offset 0x399400, dt_size=0） | `validate_mtk_boot.py` → VALID；cmdline `console=ttyS0,115200n8 … rdinit=/init` |
| ADB img kernel = 当前树 zImage | img kernel payload 3792548B = `fork_linux/arch/arm/boot/zImage`(3772928B) + `mt6572-z1.dtb`(19620B) | sha256 img kernel `59ee7d3a…`；树 zImage `1eb98f3b…`；拼接长度等式成立 |
| UDC | `musb-hdrc`（`MUSB_DRIVER_NAME`） | `ADB_KERNEL_20260815.md` §3 + musb_core.c:95 |
| 原厂 adbd | `out/boot_extracted/rd/sbin/adbd` = static ARM EABI5 stripped 202968B，md5 `05845e16…`（与 `out/forensic/{live,rd_stock_extract}/rd/sbin/adbd` 同） | `file`/`readelf -l`（无 INTERP/NEEDED）；本报告 strings |
| adbd FunctionFS | `/dev/usb-ffs/adb/ep0`、`ep1`、`ep2`、`[ usb_init - using FunctionFS ]`、`init_functionfs`、`/dev/android_adb_enable`(仅兜底) | 本报告 `strings -a` |
| adbd Android 残留 | `/system/bin/sh`（硬编码）、`/dev/log/{main,radio,events,system}`、`/dev/socket/property_service`、`ro.adb.secure`、`ro.debuggable`、`service.adb.tcp.port`、`/data/misc/adb/adb_keys`、`/adb_keys`、`tzdata`(`/system/usr/share/zoneinfo/tzdata`)、`ADB_EXTERNAL_STORAGE`、`/system/bin/bu`、`/system/bin/lcdc_screen_cap`、`/dev/ptmx`、`/dev/urandom`、`/proc/meminfo` | 本报告 `strings -a` |
| ADB 版 ramdisk | 512B MTK ROOTFS 头(`88 16 88 58`) + XZ cpio；`/init` 只是 bringup（getty+喂狗+恢复 shell），**不含 adbd 自动启动**；`/root/connsys/` 里有 `adbd` + `z1_adb_setup.sh` + 12 个 CONNSYS .ko | 本报告解包 `adb_rd/init` 全文 + `find adb_rd -name adbd` |
| z1_adb_setup.sh 顺序 | 挂 functionfs → **绑 UDC → 起 adbd**（顺序反，见 §4.2 修正） | 本报告读 `scripts/z1_adb_setup.sh`（2462B, 8月16 01:27） |
| Alpine rootfs | `out/alpine_rootfs/`（WORK, 1028 条目, Alpine 3.24.0）；`/sbin/init→busybox`；OpenRC 全套；`/etc/inittab` 有 `ttyGS0`/`tty0` getty（**无 ttyS0**，需按 ALPINE_BOOT_PATH §3.4 补）；`/etc/fstab` 用 `/dev/block/*`（initramfs 已建软链）；runlevels: sysinit=`devfs dmesg mdev`、default=`usb-acm`（mainline 下 no-op，`android_usb` 不存在） | `ALPINE_INVENTORY_20260815.md` + 本报告读 inittab/fstab/init.d/runlevels |
| Alpine 有 start-stop-daemon | `/sbin/start-stop-daemon`（29944B, openrc 自带，非 busybox applet） | 本报告 find |
| 分区 | p4=`android`=system 900MiB，`system_alpine.img` 850MiB ≤ 分区（无 64bit ext4，可刷） | `ALPINE_INVENTORY_20260815.md` §6 |
| 主机 | `adb` 34.0.4-debian 已装；`20-mtk.rules` 已覆盖 0e8d→0666；隐患=50-android.rules 的 `adbusers` 组不存在 | `ADB_HOST_20260815.md` |

**关键澄清**：`out/boot_mainline_z1_adb.img` 的 ramdisk 虽装了 adbd 和 setup 脚本，但 `/init` **没有自动跑**它们（需从恢复 shell 手动 `sh /root/connsys/z1_adb_setup.sh`）。本次 Alpine 方案把等价逻辑变成 OpenRC 服务，**发行版启动即自动**——这正是用户要的"发行版里也有 ADB"。

---

## 3. 联网调研结论（adbd 在普通 Linux 发行版里的部署实践）

> WebSearch 工具在本 runtime 不可用（provider schema 错误），改用 tavily/firecrawl MCP 搜索 + 抓页完成。以下均为实际抓到的内容。

### 3.1 发行版里没有现成 adbd 包
- **Alpine edge 的 `android-tools` 只含 host 侧 `adb`+`fastboot`**（GitHub issue sorccu/docker-adb#9 确认："a android-tools package in Alpine edge which contains adb and fastboot only"）。**无 adbd** → 发行版侧必须自带 adbd 二进制，本项目用原厂静态 adbd 正合此路。
- Debian/Ubuntu 的 `android-tools-adbd` 已从全 suite 移除（`ADBD_PLAN_20260815.md` §1.4 已核）。

### 3.2 可复刻的 configfs/functionfs 建 ADB gadget 脚本（社区范式一致）
联网抓到 3 份同构脚本，逻辑完全一致，佐证 §4 的 gadget 建法：
- **tonyho/adbd-linux `adb.sh`**（GitHub，Linaro wiki 风格）：
  ```
  mount -t configfs none /config
  mkdir -p /config/usb_gadget/g1
  echo 0x18d1 > idVendor
  mkdir functions/ffs.adb
  ln -s …/functions/ffs.adb …/configs/c.1/ffs.adb
  mkdir -p /dev/usb-ffs/adb
  mount -o uid=2000,gid=2000 -t functionfs adb /dev/usb-ffs/adb
  adbd &
  echo 'ci_hdrc.0' > …/UDC
  ```
  **注意它也是"先 adbd 后 UDC"**，与 §4.2 的 f_fs 结论一致；`-o uid=2000,gid=2000` 是给"adbd 降权到 shell 后仍能开 ep"留的口子。
- **NXP i.MX (i.MX6QP) 官方做法**（community.nxp.com）：`mount -t functionfs adb /dev/usb-ffs/adb` → `start-stop-daemon --start --background --oknodo --quiet --exec /usr/bin/adbd` → `echo 0x1 > …/os_desc/b_vendor_code` → `echo ci_hdrc.0 > …/UDC`。**同样先 daemon 后 UDC**。
- **rvspace VisionFive2 (JH7110) DG_USB "Gadget as ADB"**：`mount -t configfs none /sys/kernel/config` + functionfs 挂载，与本案同。

### 3.3 adbd 在纯 Linux 上的已知坑（meta-openembedded issue #147）
- "Is adbd supposed to work with normal linux kernel? I tried and failed"（openembedded/meta-openembedded#147）：Yocto 编出的 meta-oe adbd 在无 `/system/build.prop`、无 `/sys/class/android_usb/` 的 plain Linux 上 `adbd --help` 直接 segfault。**佐证：adbd 对 Android 残留是硬依赖，必须像 §5 那样做缓解**（本 Z1 原厂 adbd 比 meta-oe 那版对缺失容忍度高，但缓解不能省）。

### 3.4 关键难点："adbd 以 root 跑"在普通 Linux 上的处理
- **Harrison's Sandbox "Patching the adb daemon to run as root"**（2020，`harrisonsand.com/posts/patching-adb-root`）：无 Magisk 的 IoT 设备上，adbd 默认以 shell 用户跑、`adb root` 报 `adbd cannot run as root in production builds`。解法 = **二进制补丁**：用 Ghidra 定位 `setgid(AID_SHELL)`/`setuid(AID_SHELL)`（搜 `0x7d0`=2000 或 "Local port disabled" 字符串），把对应 4 字节 `bl` 换成 `00 BF`（NOP）。他给的是 Nougat(7.x) 的精确字节（`4f f4 fa 61 2d f0 3e f9`），源自 **Christian Weiske**（cweiske.de/tagebuch/android-root-adb.htm）。
- 本案 Z1 adbd 是 3.4.67 时代（Android ~4.x/5.x），**同样有 `adbd cannot run as root in production builds` 字符串**（本报告 strings 命中），降权代码形态应是直接 `setgid(2000); setuid(2000)`（非 minijail）。二进制补丁可行但需按本二进制用 Ghidra 找点（§5.3 方案 C）。
- **Mozilla bugzilla 1021756**：`ro.secure=1` + `ro.debuggable=1` 时 adb 以 shell 跑；`ro.debuggable=1` 才允许 `adb root` 重启为 root。**→ 没有 property 服务时 ro.debuggable 缺省为空 → `adb root` 必然被拒**，所以别指望 `adb root`，直接走 §5 兜底。

### 3.5 OpenRC 服务写法（Alpine 官方规范）
- **Alpine wiki "Writing Init Scripts"**：`#!/sbin/openrc-run` 起头；优先用标准变量 `command/command_args/command_user/pidfile/command_background/start_stop_daemon_args`；`depend(){ need … }`；自定义逻辑写 `start()/stop()` + `ebegin/eend`。**项目现有 `/etc/init.d/usb-acm` 就是这种写法**（本报告已读原文，§6 脚本沿用其风格）。

---

## 4. 设计决策（为什么这么做）

### 4.1 gadget 参数（与 Z1 原厂/ADB 版 ramdisk 对齐）
- `idVendor=0x0e8d`(MediaTek) + `idProduct=0x200e`（原厂 `init.usb.rc` 的 `adb,acm` 复合 PID）→ **adb + acm 共存**：adb 走 ffs、串口 console 走 `ttyGS0`（Alpine inittab 里已有 ttyGS0 getty，正好复用，不冲突）。
- `functions/ffs.adb`（adbd 硬编码路径 `/dev/usb-ffs/adb`，**必须叫 `ffs.adb`**，挂载点名字随意的 `adb` 只是 fsname）。
- `functions/acm.usb0`（保留 USB 串口能力，替代被关掉的 g_serial）。
- 主机识别不靠 VID/PID，靠 interface-class `ff:42:01`（ADB_HOST §4.1），0e8d:200e 原厂即认。

### 4.2 ★ f_fs 启动顺序（本项目最重要修正）
从 mainline 7.0-rc7 `drivers/usb/gadget/function/f_fs.c` 源码核实：
- `ffs_do_functionfs_bind()`（:3491）→ `ret = ffs_opts->dev->desc_ready ? 0 : -ENODEV;`（:3509）→ **adbd 还没写描述符时返回 -ENODEV**。
- `desc_ready` 由 `ffs_ready()`（:4249）置位 = **adbd 打开 `/dev/usb-ffs/adb/ep0` 并写入 ADB 描述符**之后（`[ usb_init - using FunctionFS ]`）。
- 而 composite bind 在 `echo <udc> > …/UDC` 时执行；任一 function bind 失败 → 整个 gadget 不绑定（`configfs_composite_bind` 失败）。

**结论：必须 `mount functionfs → 起 adbd（写描述符）→ 再 echo UDC`。**
现成 `scripts/z1_adb_setup.sh`（:step8 绑 UDC 在 :step9 起 adbd 之前）顺序反了，绑定时 adbd 未起 → desc_ready=false → **gadget 大概率不枚举**（它把 UDC 写失败吞了只打日志）。Alpine 版脚本修正 + 加 UDC 写入重试（§6 脚本内）。

### 4.3 configfs 挂载方式
- 在 init 脚本内 `mkdir -p /sys/kernel/config && mount -t configfs none /sys/kernel/config`（内核 `CONFIG_CONFIGFS_FS=y` 已确认）。
- 也可同时往 `/etc/fstab` 加一行 `configfs /sys/kernel/config configfs defaults 0 0`（二选一；脚本内挂更稳，不依赖 fstab 时序）。
- `/sys` 由 initramfs bind 进 Alpine（`ALPINE_IMPLEMENT` §2.2），可写，`mkdir` 无碍。

### 4.4 adbd 放哪
- 推荐 `/usr/local/sbin/adbd`（Alpine FHS 惯例，外来静态二进制），或 `/sbin/adbd`（对齐 Android）。`/opt/adbd` 亦可。脚本用变量 `adbd=${adbd:-/usr/local/sbin/adbd}`，可经 `/etc/conf.d/adbd` 覆盖。
- 改前先备份原二进制到 `bridge_backups/`（纪律）。

### 4.5 服务生命周期
- 用 OpenRC `start-stop-daemon --start --background --make-pidfile --pidfile /run/adbd.pid --exec $adbd`。
- adbd 是长驻 daemon；若崩溃，OpenRC 普通 start-stop-daemon 不 respawn。可升级为 OpenRC 的 **`supervise-daemon`**（openrc 自带，Alpine 3.24 支持）做自动拉起——先跑通基础版，再决定是否上 supervise（§6 附备选）。

---

## 5. Android 残留缓解（Alpine 版逐项）

> 原理：Alpine 无 Android init/property 服务。adbd 的 bionic `property_get` 连不上 `/dev/socket/property_service`、mmap 不到 `/dev/__properties__` → 全部返回缺省值。逐项评估如下：

| # | 残留 | adbd 行为 | Alpine 处理 | 优先级 |
|---|---|---|---|---|
| 1 | `/system/bin/sh`（`adb shell` 硬编码） | `adb shell`/`exec:/system/bin/sh` 直接 exec 此路径，不存在则 shell 服务失败 | `mkdir -p /system/bin && ln -sf /bin/sh /system/bin/sh`（busybox ash 可直接当交互 shell） | **必做** |
| 2 | `shell` 用户（uid/gid 2000） | adbd 若降权（见 §5.0）执行 `setgid(2000);setuid(2000)`，数值 setuid 不需要 passwd 条目，但 shell 进程的 HOME/登录体验需要 | `/etc/passwd` 追加 `shell:x:2000:2000:shell:/data/local/tmp:/bin/sh`；`/etc/group` 追加 `shell:x:2000:`；`mkdir -p /data/local/tmp`。**优先直接 append 而非 `adduser`**（嵌入式 chroot 环境，避免 adduser 依赖项） | 必做（防降权路径残缺） |
| 3 | `/dev/log/{main,radio,events,system}` | log 通道缺失 → 写失败静默降级（非致命） | `mkdir -p /dev/log`（真 socket 建不了也没关系，open 失败被忽略） | 建议（防重复报错刷屏） |
| 4 | `ro.adb.secure`（RSA 认证） | property 缺省 → 大概率 `0`（免认证）；若被认成 1 → 主机显示 `unauthorized` | 免认证；若遇 `unauthorized`：`mkdir -p /data/misc/adb && cp 主机 ~/.android/adbkey.pub /data/misc/adb/adb_keys` | 兜底（Z1 原厂=0，大概率不用） |
| 5 | `ro.debuggable`/`ro.secure`（决定降权） | 缺省 → ro.debuggable=""（非"1"）、ro.secure="1" → `should_drop_privileges()` **大概率返回真 → adbd 降权到 shell** | 上板 `adb shell id` 实测；若 shell → §5.3 三选一 | **关键风险** |
| 6 | `ADB_EXTERNAL_STORAGE` | `adb pull/push` 目标目录报 Warning/空 | init 脚本 `export ADB_EXTERNAL_STORAGE=/mnt/data`（p6 usrdata，fstab 已挂） | 建议 |
| 7 | `service.adb.tcp.port` | 未设 → 纯 USB 模式（正好） | 不处理 | 无 |
| 8 | `tzdata`（bionic `__bionic_open_tzdata_path`） | `/system/usr/share/zoneinfo/tzdata`、`/data/misc/zoneinfo/tzdata` 缺失 → 时间戳退 UTC，非致命 | 忽略；要修可拷 Alpine 的 `/usr/share/zoneinfo` 重建？不划算，跳过 | 无 |
| 9 | `/system/bin/bu`、`/system/bin/lcdc_screen_cap` | backup/screen-cap 服务，Z1 场景用不上 | 不处理（缺了只会让这两个 service 报错，不阻塞 shell） | 无 |
| 10 | `/dev/ptmx`、`/dev/urandom`、`/proc/meminfo` | shell PTY / RSA 熵源 / 状态读取 | devtmpfs/ptproc 已挂，天然存在 | 无 |
| 11 | `/tmp/adb.log`、`/data/adb/*` | 调试日志落点 | `/tmp` 是 tmpfs 可写；`/data/adb` 需 `mkdir -p /data/adb`（或不管） | 可选 |

### 5.0 ★ adbd 是否降权到 shell（本方案最大不确定点）
- adbd strings 命中 `adbd cannot run as root in production builds` → 是 production/user-build adbd。无 property 服务时：
  - `ro.debuggable` 缺省 `""` → 不是 "1"；
  - `ro.secure` 缺省 `"1"` → `should_drop_privileges()` 返回真 → **`setgid(AID_SHELL);setuid(AID_SHELL)` 降权到 uid 2000**。
- 若 adbd 二进制带 `ALLOW_ADBD_ROOT`（getuid()==0 时先返回不降权）→ 保持 root。**strings 区分不了，上板一测便知**（`adb shell id`）。
- 影响：shell(uid 2000) 的 adb shell **不能 `insmod`、不能读写多数 /dev** —— 与"测非重要驱动"需求冲突，**所以 root 是目标**。

### 5.3 保 root 的 3 级兜底（按成本升序）
- **方案 A（首选，上板先试）**：原厂 adbd 直接跑，`adb shell id` → 若是 `uid=0(root)` → 收工，后续全免。
- **方案 B（二进制补丁，零编译）**：若降权，按 Harrison/Weiske 技法用 Ghidra 定位本 adbd 的降权点：
  1. 找 "adbd cannot run as root in production builds" 或 `should_drop_privileges` 引用；
  2. 定位 `setgid(2000)/setuid(2000)`（本 adbd 是 4.x 形态的直接调用，非 minijail；搜 `mov rN,#0x7d0` + `bl setuid`）；
  3. 把 `bl` 处 4 字节 `00 BF` NOP 掉 → adbd 永不降权。
  - 补丁前 `cp` 备份 + 改后 `sha256sum` 留档；产物进 `bridge_backups/products/`。工作量 ~1-2 小时，风险中（需 Ghidra 分析 + 上板复测）。
- **方案 C（干净重编，最稳）**：按 `ADBD_PLAN_20260815.md` §2 路径 A：meta-oe scarthgap `android-tools_5.1.1.r37.bb`（含 `0008-allow-adbd-root`、`0003-define-shell-command` 补丁，`-DALLOW_ADBD_ROOT=1`）交叉编译出**保持 root + shell 指向 `/bin/sh`** 的 adbd。工作量 ~半天-1 天，但行为确定。
- **方案 D（假 property 层，可选研究）**：构造 `/dev/__properties__`（bionic property area 文件，写 `ro.secure=0`/`ro.debuggable=1`/`ro.adb.secure=0`）让 adbd 的 property_get 读到。可行但**格式要精确匹配本 adbd 的 bionic 版本**（4.x 是数组式 prop_area，5.x 是 trie），复杂度不低于 B/C，除非想顺带开 TCP adb，否则不做。

---

## 6. 交付物：`/etc/init.d/adbd`（OpenRC 服务，Alpine 3.24 / openrc-run）

> 部署到 `out/alpine_rootfs/etc/init.d/adbd`（`chmod 0755`），`rc-update add adbd default`，并从 default 移除 no-op 的 usb-acm（`rc-update del usb-acm`）。改 rootfs 前先备份 `out/alpine_rootfs/`。

```sh
#!/sbin/openrc-run
# /etc/init.d/adbd — Z1 (MT6572) ADB gadget(configfs/functionfs) + 原厂静态 adbd
# 依赖: ADB 版内核 (CONFIG_USB_CONFIGFS + USB_CONFIGFS_F_FS + USB_CONFIGFS_ACM, G_SERIAL 已关)
# 顺序关键: 先起 adbd 写 f_fs 描述符(desc_ready=true), 再 echo UDC (f_fs.c:3509 反向会 ENODEV)
# 用法: rc-update add adbd default; 上板后 rc-service adbd restart

description="Z1 ADB USB gadget (functionfs + stock adbd)"

# ---- 可配置 (支持 /etc/conf.d/adbd 覆盖) ----
: ${adbd:=/usr/local/sbin/adbd}           # 原厂静态 adbd 拷贝路径
: ${gadget:=z1}
: ${idVendor:=0x0e8d}
: ${idProduct:=0x200e}                    # adb+acm 复合 (原厂 init.usb.rc 惯例)
: ${udc:=musb-hdrc}
: ${ffs_mount:=/dev/usb-ffs/adb}

CFG=/sys/kernel/config/usb_gadget

depend() {
    need mdev devfs
    after bootmisc
}

_start_gadget() {
    local f i
    # 1. 挂 configfs
    mkdir -p /sys/kernel/config
    mount -t configfs none /sys/kernel/config 2>/dev/null

    # 2. 清理旧 gadget
    if [ -d "$CFG/$gadget" ]; then
        echo "" > "$CFG/$gadget/UDC" 2>/dev/null
        rm -rf "$CFG/$gadget"
    fi

    # 3. 建 gadget + 字符串
    mkdir -p "$CFG/$gadget"
    echo "$idVendor"  > "$CFG/$gadget/idVendor"  2>/dev/null
    echo "$idProduct" > "$CFG/$gadget/idProduct" 2>/dev/null
    mkdir -p "$CFG/$gadget/strings/0x409"
    echo "Z1"            > "$CFG/$gadget/strings/0x409/manufacturer" 2>/dev/null
    echo "Z1 hearing-aid"> "$CFG/$gadget/strings/0x409/product"      2>/dev/null

    # 4. functionfs(adb) + acm(串口) 并存
    mkdir -p "$CFG/$gadget/functions/ffs.adb"
    mkdir -p "$CFG/$gadget/functions/acm.usb0"

    # 5. 配置 + 挂 function 进 config
    mkdir -p "$CFG/$gadget/configs/c.1"
    ln -sf "$CFG/$gadget/functions/ffs.adb"  "$CFG/$gadget/configs/c.1/"
    ln -sf "$CFG/$gadget/functions/acm.usb0" "$CFG/$gadget/configs/c.1/"

    # 6. 挂 functionfs (adbd 硬编码路径, 必须此名)
    mkdir -p "$ffs_mount"
    mount -t functionfs adb "$ffs_mount" 2>/dev/null

    # 7. Android 残留缓解 (见本报告 §5)
    mkdir -p /system/bin /dev/log /data/misc/adb /data/local/tmp
    ln -sf /bin/sh /system/bin/sh 2>/dev/null
    grep -q '^shell:' /etc/passwd 2>/dev/null || \
        echo 'shell:x:2000:2000:shell:/data/local/tmp:/bin/sh' >> /etc/passwd
    grep -q '^shell:' /etc/group 2>/dev/null || \
        echo 'shell:x:2000:' >> /etc/group

    # 8. 起 adbd (先! 让 f_fs desc_ready 置位)
    [ -x "$adbd" ] || { eerror "adbd 不存在: $adbd"; return 1; }
    start-stop-daemon --start --background --make-pidfile \
        --pidfile /run/adbd.pid --exec "$adbd" || return 1

    # 9. 绑 UDC (带重试, 等 desc_ready)
    i=0
    while ! echo "$udc" > "$CFG/$gadget/UDC" 2>/dev/null; do
        i=$((i+1))
        [ "$i" -ge 20 ] && { eerror "UDC bind failed after ${i}s (see dmesg)"; return 1; }
        sleep 1
    done
    return 0
}

start() {
    ebegin "Setting up Z1 ADB gadget + starting adbd"
    if _start_gadget; then
        eend 0 "ADB gadget up: $idVendor:$idProduct on $udc"
    else
        eend 1 "ADB gadget setup FAILED (rc-service adbd status; dmesg)"
    fi
}

stop() {
    ebegin "Stopping adbd + unbinding gadget"
    start-stop-daemon --stop --pidfile /run/adbd.pid 2>/dev/null
    if [ -d "$CFG/$gadget" ]; then
        echo "" > "$CFG/$gadget/UDC" 2>/dev/null
    fi
    eend $?
}

# 备选: 想让 adbd 崩溃自动拉起, 用 OpenRC supervise-daemon (openrc 自带) 替代
# start() { ...起 gadget 后:
#   supervise-daemon adbd --start --pidfile /run/adbd.pid --exec "$adbd"
# }
```

### 6.1 部署命令（PC 上，改 Alpine rootfs WORK 目录）
```bash
cd /home/<user>/claude/6572
# 备份 + 拷 adbd + 装 init 脚本
cp -a out/alpine_rootfs bridge_backups/alpine_rootfs_pre_adbd_$(date +%Y%m%d_%H%M)
sudo cp out/boot_extracted/rd/sbin/adbd out/alpine_rootfs/usr/local/sbin/adbd   # 202968B static
# (或 /sbin/adbd; 注意与 /etc/init.d/adbd 里 adbd= 变量一致)
sudo cp mainline_recon/../docs/path/to/init.d.adbd out/alpine_rootfs/etc/init.d/adbd  # §6 脚本
sudo chmod 0755 out/alpine_rootfs/etc/init.d/adbd
# OpenRC 注册 (chroot 内做, 或直接写 runlevel 软链):
sudo ln -sf /etc/init.d/adbd out/alpine_rootfs/etc/runlevels/default/adbd
sudo rm -f out/alpine_rootfs/etc/runlevels/default/usb-acm        # no-op 服务移除
# (可选) /etc/fstab 加 configfs; /etc/inittab 补 ttyS0 getty 见 ALPINE_BOOT_PATH §3.4
# 重打 system_alpine.img (make_alpine_rootfs.sh 末尾的 mke2fs -d 步骤)
```
> 注：OpenRC `rc-update add` 需在 chroot 里跑（`qemu-arm-static` + `chroot out/alpine_rootfs`），或直接写 `etc/runlevels/default/adbd` 软链（等价，少跑 chroot）。

---

## 7. 上板验证（板子当前关机；刷写才插 download 线，交互拔线电池开机）

### 7.1 准备（沿用 Alpine 启动链）
1. **刷 p4 (system, 分区名 `android`)**：`tools/flash_partition.sh android out/system_alpine.img`（若 §6 改了 rootfs，先用 make_alpine_rootfs 重打 850MiB 镜像）。
2. **刷 boot.img**：用 **ADB 内核** + Alpine switch ramdisk 打包：
   ```bash
   cd /home/<user>/claude/6572
   # 方案A initramfs (ALPINE_IMPLEMENT §3): Z1_ALPINE_SWITCH=1 bash tools/repack_ramdisk_connsys.sh
   cat fork_linux/arch/arm/boot/zImage fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb > /tmp/zImage_appdtb_alpine
   tools/z1_make_bootimg.sh /tmp/zImage_appdtb_alpine \
       fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb \
       out/ramdisk_alpine_switch.gz out/boot_mainline_alpine_adb.img
   tools/flash_with_retry.sh bootimg out/boot_mainline_alpine_adb.img ~/claude/6572/1/boot.img 8
   ```
   （`fork_linux/arch/arm/boot/zImage` 8月16 01:37 即 ADB configfs/ffs 内核，§2 已证实。）
3. **抓 log**：`./z1_capture_mainline.sh 600`。期待：`earlycon/Linux version` → `ttyS0` → `mmc0` → initramfs `[init] mount p4` → `switch_root` → `Welcome to Alpine Linux` → `ttyS0 login:`。

### 7.2 Alpine 内验证（C3 桥 ttyS0 登录）
```
mount | grep ' / '                 # /dev/mmcblk0p4 on / (ext4 rw)
rc-update show | grep -E 'adbd|usb-acm'
rc-service adbd start              # 期待 ebegin/eend 0 + pidfile
ls -l /sys/class/udc/              # musb-hdrc
cat /sys/kernel/config/usb_gadget/z1/UDC          # musb-hdrc
ls -la /dev/usb-ffs/adb/           # ep0 ep1 ep2
ps | grep adbd                     # adbd 在跑
id shell                           # uid=2000(shell)
ls -l /system/bin/sh               # -> /bin/sh
```

### 7.3 主机侧验证（内核完全起来后再插 USB，避开 preloader LineState 坑）
```
sudo udevadm control --reload-rules && sudo udevadm trigger   # 主机侧 §ADB_HOST 5.2
adb kill-server; adb start-server
lsusb            # 期望 0e8d:200e
adb devices -l   # 期望 <serial> device usb:...  (免认证 → 直接 device)
adb shell id     # ★ 关键判定: uid=0(root) 还是 uid=2000(shell)
adb shell uname -a
# 测非重要驱动 (需 root; shell 则先做 §5.3 兜底):
adb shell 'sh /lib/modules/7.0.0-rc7-z1+/z1_modprobe_connsys.sh'   # 或 rc-service connsys start
adb pull /proc/cmdline /tmp/z1_cmdline
adb push /tmp/test.bin /tmp/        # 验证 push 通路
```
**非重要驱动测试优先项**（§ALPINE_BOOT_PATH §4）：CONNSYS 12 个 .ko（WiFi wlan0 + BT）insmod → `echo 1 > /dev/wmtWifi` → `iw dev wlan0 scan`；或触屏/按键（`cat /dev/input/event*`）。这些都可从 `adb shell` 内以 root 做。

### 7.4 失败判读表
| log/现象 | 含义 | 处置 |
|---|---|---|
| `rc-service adbd start` 的 `UDC bind failed` | f_fs desc_ready 没置位（adbd 没起来/挂 functionfs 失败） | `ps|grep adbd`、`ls /dev/usb-ffs/adb`、`dmesg\|grep -i ffs`；确认内核是 ADB 版（`grep CONFIG_USB_CONFIGFS /proc/config.gz` 或 boot log） |
| `adb devices` 空 | gadget 没枚举 / 权限 | `lsusb`（有没有 0e8d:200e）；主机 udev（ADB_HOST §5.2）；`dmesg` 看 musb |
| `adb devices` 显示 `offline` | adbd 在跑但握手没完（旧 adbd vs adb 34） | `adb kill-server` 重试；换官方 platform-tools；看设备 `dmesg` |
| `adb devices` 显示 `unauthorized` | RSA 认证被要求（ro.adb.secure 被读成 1） | §5 兜底：推 `~/.android/adbkey.pub` → 设备 `/data/misc/adb/adb_keys` + `rc-service adbd restart` |
| `adb shell id` = `uid=2000(shell)` | adbd 降权（§5.0） | §5.3 方案 B(二进制补丁) 或 C(meta-oe 重编)，重打 img 复测 |
| `adb shell` 报 `/system/bin/sh: not found` | §5.1 的 symlink 没建 | 检查 init 脚本 step 7；手动 `ln -sf /bin/sh /system/bin/sh` + restart |
| `ttyGS0` getty 刷屏 | 若只配了 adb 没配 acm，ttyGS0 不存在 | gadget 里保留 `acm.usb0`（§6 已含）即无此问题 |

---

## 8. 风险清单

| 风险 | 概率 | 缓解 |
|---|---|---|
| adbd 降权到 shell → adb 内不能 root 测驱动 | 高（无 property 服务时 should_drop_privileges 大概率真） | §5.3 B/C/D；先上板 `adb shell id` 定案 |
| f_fs 顺序错（沿用 z1_adb_setup.sh 先 UDC 后 adbd）→ gadget 不枚举 | 高（若照抄旧脚本） | §6 已修正：先 adbd 后 UDC + 重试 |
| 旧 adbd(4.x) vs 主机 adb 34 协议不兼容 → offline | 中 | `adb kill-server`、官方 platform-tools；仍不行走 §5.3 C 编新 adbd |
| preloader LineState 坑（开机插 USB 进 download 循环） | 中（若违规） | 纪律：开机拔 USB，内核起来后再插 |
| Alpine boot 链本身的 initrd-end/mount 问题 | 中（已有方案） | `ALPINE_IMPLEMENT_20260816.md` §5/§6 判读表 |
| initramfs `/init` 没自动起 adbd（只是备了脚本） | 已确认 | 本方案用 Alpine OpenRC 服务接管，不依赖 initramfs |
| configfs/functionfs 挂载因 /sys 只读失败 | 低 | /sys 是 sysfs rw；mount 前 mkdir；出错有 eerror + 判读表 |
| `adduser`/passwd 改动影响 Alpine 登录 | 低 | 直接 append passwd/group，不碰 root；备份 rootfs |

---

## 9. 关键文件 / URL 索引

### 本地
- 原厂 adbd：`/home/<user>/claude/6572/out/boot_extracted/rd/sbin/adbd`（202968B static ARM；同 `out/forensic/live/rd/sbin/adbd`、`out/forensic/rd_stock_extract/sbin/adbd`）
- ADB 版内核：`/home/<user>/claude/6572/fork_linux/arch/arm/boot/zImage`（8月16 01:37，= `out/boot_mainline_z1_adb.img` kernel 段去掉 appended dtb）
- ADB 版 boot.img：`/home/<user>/claude/6572/out/boot_mainline_z1_adb.img`
- 现成 setup（顺序需修正）：`/home/<user>/claude/6572/scripts/z1_adb_setup.sh`（+ ADB ramdisk 内 `/root/connsys/z1_adb_setup.sh` 同份）
- Alpine rootfs WORK：`/home/<user>/claude/6572/out/alpine_rootfs/`；镜像 `/home/<user>/claude/6572/out/system_alpine.img`
- 内核 f_fs 顺序证据：`fork_linux/drivers/usb/gadget/function/f_fs.c:3509`（desc_ready→ENODEV）、`:4266`（ffs_ready 置 desc_ready）
- 前置文档：`mainline_recon/{ADB_KERNEL,ADBD_PLAN,ADB_FULL_PLAN,ADB_HOST}_20260815.md`、`ALPINE_BOOT_PATH_20260815.md`、`ALPINE_IMPLEMENT_20260816.md`
- 本报告：`/home/<user>/claude/6572/mainline_recon/ALPINE_ADB_20260816.md`

### 联网（2026-08-16 实测可达）
- Alpine Writing Init Scripts（openrc-run 规范）：https://wiki.alpinelinux.org/wiki/Writing_Init_Scripts
- Alpine android-tools 只有 adb/fastboot 无 adbd：https://github.com/sorccu/docker-adb/issues/9
- adbd-on-Linux 完整 configfs/functionfs 脚本：https://github.com/tonyho/adbd-linux/blob/master/adb.sh
- NXP i.MX adb gadget（start-stop-daemon + UDC 顺序）：https://community.nxp.com/t5/i-MX-Processors/how-to-enable-adb-tool-in-L5-4-3-1-0-0-of-imx6qpsaredsd/m-p/1066988
- VisionFive2 Gadget-as-ADB：https://doc-en.rvspace.org/VisionFive2/DG_USB/JH7110_SDK/gadget_as_adb.html
- adbd 保 root 二进制补丁（Harrison）：https://harrisonsand.com/posts/patching-adb-root
- adbd 保 root 补丁始祖（Christian Weiske）：https://cweiske.de/tagebuch/android-root-adb.htm
- adbd 在 plain Linux 上的坑（meta-oe#147）：https://github.com/openembedded/meta-openembedded/issues/147
- kernel gadget_configfs 文档：https://docs.kernel.org/usb/gadget_configfs.html
- ro.secure/ro.debuggable 与 adb root（Mozilla bug 1021756）：https://bugzilla.mozilla.org/show_bug.cgi?id=1021756
