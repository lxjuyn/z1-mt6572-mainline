# Z1 (MT6572) ADB 完整落地计划 — 汇总 (2026-08-15)

> 本文件是三份分报告的汇总落地版（只读分析 + 汇总，未实施）：
> - 内核侧: `ADB_KERNEL_20260815.md` — mainline 内核开 functionfs/configfs 支持 ADB
> - 设备端: `ADBD_PLAN_20260815.md` — 静态 adbd 获取/编译路径 A(编)/B(零编译复用原厂)
> - 主机侧: `ADB_HOST_20260815.md` — 主机 adb + udev 准备与连接步骤
>
> **一句话结论**: mainline 无 `f_adb`，ADB 走 **configfs(functionfs) + 用户态 adbd**。内核只加 3 个 CONFIG 关 1 个（零驱动代码，vermagic 不变，zImage +~40KB）；设备端**先零编译复用原厂静态 adbd**（自带 FunctionFS 支持），失败再按 meta-oe 配方交叉编译；主机侧只补一步 udev。总工作量以"板子可交互 shell"为目标约 1 天（含上板验证），其中主机侧 5 分钟、内核 30 分钟、设备端是主要风险区（adbd 的 Android 残留）。

---

## 0. 现状盘点（三报告已核实事实）

| 侧 | 现状 | 差距 |
|---|---|---|
| 内核 | `CONFIG_USB_GADGET/MUSB_HDRC/MUSB_GADGET/MUSB_MEDIATEK/USB_LIBCOMPOSITE/USB_F_ACM/F_SERIAL/G_SERIAL=<SERIAL> 已注册；**`USB_CONFIGFS`、`USB_F_FS` 未开** | 开 3 关 1，零驱动代码 |
| DTS | `usb@11100000` compatible 匹配 `musb/mediatek.c`，`dr_mode="peripheral"`，PHY `PHY_MTK_MT6572_USB2_HANDOFF=y` | 无需改动 |
| 设备端 | mainline initramfs 无 adbd；本机有**原厂静态 adbd** `out/boot_extracted/rd/sbin/adbd`（202968B, static ARM EABI5, 无 INTERP/NEEDED，**自带 FunctionFS 支持**） | 拷入 + setup + Android 残留缓解 |
| 主机 | `adb` 34.0.4-debian 已装；`20-mtk.rules` 已覆盖 0e8d→0666 plugdev；隐患: `50-android.rules` 通用 `ff420?` 规则引用**不存在的 adbusers 组** | 补 udev 兜底（5 分钟） |
| 认证 | 原厂 `ro.adb.secure=0` → 无 RSA 免认证 | 天然免授权 |
| VID/PID | 原厂 adb-only=`0e8d:0c01`，adb+acm=`0e8d:200e`；现代 adb 按 interface-class `ff:42:01` 识别，不依赖 VID/PID | — |

---

## 1. 完整落地计划

### 阶段 1 — 内核配置（ADB_KERNEL_20260815.md，零驱动代码）

**原理**：mainline 无 `f_adb.c`（全树仅 `drivers/macintosh/adb*.c`，是 Mac 总线无关）。正确姿势 = `USB_CONFIGFS` + `USB_CONFIGFS_F_FS`（configfs 下提供 functionfs function），用户态 adbd 写 ADB 描述符（class 0xFF/0x42/0x01）。musb 经 `usb_add_gadget_udc()` 注册为标准 UDC，configfs 在其上工作，天然支持。

**CONFIG 变更（开 3 关 1）**：
```bash
cd /home/<user>/claude/6572/fork_linux
./scripts/config --enable USB_CONFIGFS \
                 --enable USB_CONFIGFS_F_FS \
                 --enable USB_CONFIGFS_ACM \
                 --disable USB_G_SERIAL
# 校验期望:
# CONFIG_USB_CONFIGFS=y / CONFIG_USB_CONFIGFS_F_FS=y / CONFIG_USB_CONFIGFS_ACM=y
# CONFIG_USB_F_FS=y (被 select)  / # CONFIG_USB_G_SERIAL is not set
```
- `CONFIG_USB_F_FS` 被 `USB_CONFIGFS_F_FS` select，不手写（其依赖 `DMA_SHARED_BUFFER` 已在）。
- **关键**：`USB_G_SERIAL`（legacy 预组 g_serial）会在启动时自动抢占唯一 UDC "musb-hdrc"，导致 configfs 后面绑不上（"can't bind UDC"），**必须关**。串口能力由 `USB_CONFIGFS_ACM` 以 configfs function 接回（`acm.usb0`），不丢失。
- 顺手可选：关 `USB_MUSB_TUSB6010/OMAP2PLUS/DSPS`（=m 不 probe，非必须）。

**重编（项目纪律：改 config 后 make clean）**：
```bash
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- olddefconfig
grep -E 'CONFIG_USB_CONFIGFS|CONFIG_DEBUG_LL' include/config/auto.conf   # 确认没把 DEBUG_LL 退掉
make clean && make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- mt6572-z1.dtb zImage
```

**体积与 vermagic**：configfs.o ~14KB + f_ffs.o ~40-60KB 原始码，压缩后 **+~30-40KB**；现状 zImage 3.76MB / boot.img 5.17MB ≤ 6MB 分区，加后 ~5.21MB 安全，**无需砍 config**。vermagic 由 `UTS_RELEASE`（=`7.0.0-rc7-z1+`）决定，本次只加减 `CONFIG_USB_*` 符号 → **字符串不变，现有 5 个 CONNSYS .ko 照常 insmod 不重编**（纪律：全程默认 make，命令行绝不设 `LOCALVERSION=`）。

**打包 boot.img（appdtbdual 风格，沿用 debugll 基线）**：
```bash
cd /home/<user>/claude/6572
cat fork_linux/arch/arm/boot/zImage fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb > /tmp/zImage_appdtb_adb
./build/mtkbootimg/mkbootimg --kernel /tmp/zImage_appdtb_adb \
  --ramdisk out/forensic/linux/ramdisk.gz \
  --dt fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb \
  --base 0x10000000 --kernel_offset 0x8000 --pagesize 2048 --mtk 1 \
  --cmdline "console=tty0 console=ttyMT0,921600n8 earlycon=mtk8250,0x11005000 root=/dev/ram panic=0" \
  -o out/boot_mainline_adb.img
# ≤6MB 校验 + 归档 bridge_backups/products/ + git 上传
```

**内核侧验收点**：
1. dmesg: `musb-hdrc` 注册 UDC + `musb-hdrc.1.auto: USB MODE` 正常。
2. `ls /sys/class/udc/` → 出现 `musb-hdrc`。
3. `mount -t configfs none /sys/kernel/config` 成功。
4. `mount -t functionfs adb /dev/usb-ffs/adb` 成功，出现 `ep0/ep1/ep2`。
5. `echo musb-hdrc > .../UDC` 绑定后主机 `lsusb` 见 VID:PID（建议 VID 0x18d1 / PID 0x4ee2），`adb devices` 识别。

---

### 阶段 2 — 设备端 adbd（ADBD_PLAN_20260815.md）

**策略：先路径 B（零编译，原厂静态 adbd）→ 失败再路径 A（meta-oe 配方交叉编译）。**

#### 路径 B — 复用原厂静态 adbd（首选先试，零编译）
- 二进制：`out/boot_extracted/rd/sbin/adbd`（202968B，static ARM EABI5，`readelf -l` 无 INTERP/NEEDED → 纯静态，Alpine/musl rootfs 可直接跑）。
- **FunctionFS 已核实**：strings 命中 `/dev/usb-ffs/adb/ep0/ep1/ep2` + `[ usb_init - using FunctionFS ]` + `init_functionfs`，与内核 f_fs 配置一致 → 直接走 functionfs 路径，不退回 `/dev/android_adb_enable`。
- **Android 残留风险（需缓解）**：
  1. property 服务读 `ro.adb.secure/ro.debuggable/ro.product.*/service.adb.tcp.port`。`/dev/__properties__`、`/dev/socket/property_service` 不存在 → property_get 空。`ro.debuggable` 空判 production → 不降权则 `setuid("shell")` → Alpine 无 shell 用户 → **`adduser shell`（uid/gid 2000）或建假属性层**。
  2. `/dev/log/main` 缺可能静默降级 → `mkdir /dev/log` 兜底。
  3. shell 硬编码 `/system/bin/sh` → `ln -sf /bin/sh /system/bin/sh`。
  4. tzdata 只影响时间戳，忽略。
  5. `ro.adb.secure` 空 → 默认不走 RSA，大概率免认证。

#### 路径 A — meta-oe android-tools 配方交叉编译（B 失败时）
- 配方：`meta-openembedded` scarthgap `meta-oe/recipes-devtools/android-tools/android-tools_5.1.1.r37.bb`（AOSP android-5.1.1_r37 固定 SRCREV + 17 个 Linux 化 patch + 自定义 mk）。**已核实 5.1.1 的 `usb_linux_client.c` 含完整 FunctionFS 支持**（ep0 → `usb_ffs_init()` 写 ADB 描述符 + ep1/ep2），与内核 f_fs 对齐。
- 源：AOSP `system/core`(SRCREV `2314b110…`) + `system/extras` + `hardware/libhardware` + `external/libselinux` + `build`（仅取 `AndroidConfig.h`）。
- 静态依赖库三选一：
  - **A1 glibc static**（用现成 `arm-linux-gnueabihf-gcc`，最省事）：`dpkg --add-architecture armhf` + 装 `libssl-dev:armhf zlib1g-dev:armhf libbsd-dev:armhf libpcre3-dev:armhf libcap-dev:armhf`；**adbd 5.1.1 需 OpenSSL 1.0 API，必须带 `core/adb_libssl_11.diff` 补丁**（或自编 openssl-1.1.1w 静态）。链接 `-static -static-libgcc` → 纯静态 glibc，musl 上无 glibc 也跑。
  - **A2 musl static**（更贴 Alpine、体积最小，首选）：`apt-get install musl-tools:armhf` → `arm-linux-musleabihf-gcc`；配方 `0006-adb-Musl-fixes.patch` 已让源码过 musl。musl 静态零 glibc/NSS 坑。
  - A3 动态 musl：Alpine armv7 有依赖包，非默认。
- 编译骨架：`make -f adbd.mk CC=arm-linux-gnueabihf-gcc AR=arm-linux-gnueabihf-ar LDFLAGS="-static -static-libgcc"`；CFLAGS 必含 `-DADB_HOST=0 -DALLOW_ADBD_ROOT=1 -DANDROID -D_GNU_SOURCE` + `-include …/build/core/combo/include/arch/linux-arm/AndroidConfig.h`。
- 产物验证：`file` 报 statically linked、`readelf -l` 无 INTERP → 立刻归档 `bridge_backups/products/` + git 上传。

#### 用户态运行时（两条路径共用，与内核无关）
1. 挂 configfs → 建 gadget（idVendor=0x18d1, product 0x4ee2/0xd002）→ `functions/ffs.adb` → `mount -t functionfs adb /dev/usb-ffs/adb`（adbd 硬编码路径，**必须此名**）→ 可同时加 `functions/acm.usb0` 保留串口 → `echo musb-hdrc > /sys/kernel/config/usb_gadget/z1/UDC`。参考 meta-oe `android-tools-conf/android-gadget-setup`。
2. adbd 以 root 起（`ALLOW_ADBD_ROOT=1`）。
3. `mkdir -p /system/bin && ln -sf /bin/sh /system/bin/sh`（adbd shell 路径硬编码）。
4. `ro.adb.secure` 未设 → 免 RSA；若被拉高放 `/data/misc/adb/adb_keys`。
5. initramfs 里起 adbd 的 init 脚本（busybox rc 或 inittab 方式，按现有 rootfs 形态接）。

---

### 阶段 3 — 主机侧（ADB_HOST_20260815.md，5 分钟）

- `adb` 34.0.4-debian 已装，无需操作。
- **必做 udev 兜底**（当前唯一主机侧隐患：`50-android.rules` 通用 `ff420?` 规则引用不存在的 adbusers 组 → 节点变 root:root 0660 → "no permissions"）。二选一，**建议都做**：
  - A. `sudo groupadd adbusers && sudo usermod -aG adbusers $USER`（需重登生效）。
  - B. 新建 `/etc/udev/rules.d/99-z1-adb.rules`：
    ```
    SUBSYSTEM=="usb", ATTR{idVendor}=="0e8d", ATTR{idProduct}=="0c01", MODE="0666", GROUP="plugdev", SYMLINK+="z1-adb"
    SUBSYSTEM=="usb", ATTR{idVendor}=="0e8d", ATTR{idProduct}=="200e", MODE="0666", GROUP="plugdev", SYMLINK+="z1-adb-acm"
    ```
    应用：`sudo udevadm control --reload-rules && sudo udevadm trigger`（或重插）。
- 连接流程：正常开机（拔 USB）→ 内核完全起来**后再插 USB**（避开 preloader LineState 坑）→ `lsusb` 见 MediaTek → `adb kill-server; adb start-server` → `adb devices -l`（期望 `<serial> device`，`ro.adb.secure=0` 直接 device 不 unauthorized）→ `adb shell`。
- 排错状态字：空=没枚举/非 adb interface/权限；`no permissions`=udev；`offline`=adbd 在跑但握手没完成（旧 adbd vs 新 adb 34 协议，先 `adb kill-server` 再试，极端换官方 platform-tools）；`unauthorized`=RSA 未授权（设备侧 `ro.adb.secure=0` 或推 `adbkey.pub`）。
- 预验证（设备侧 ADB 就绪前也能做）：当前 g_serial 阶段插 Z1 应枚举为 CDC-ACM（0525:a4a7 / ttyACM*）→ 证明 musb 外设通路 OK，f_adb 就绪后主机侧零改动。

---

## 2. 工作量评估 + 依赖

| 阶段 | 工作量 | 风险 | 依赖 |
|---|---|---|---|
| 1 内核 config + 重编 | ~30 分钟（编译耗时为主） | 低。零驱动代码，vermagic 不变 | fork_linux 能编译（已通）；DEBUG_LL 若沿用需确认没被 olddefconfig 退掉 |
| 2 设备端 B（零编译复用） | ~1-2 小时（initramfs 脚本 + Android 残留缓解 + 上板调试） | 中。adbd 的 property//system/bin/sh 残留可控但需实测 | 阶段 1 内核（functionfs 挂载 + UDC 绑定） |
| 2 设备端 A（交叉编译） | ~半天-1 天（AOSP 克隆 + 补丁 + 静态依赖库 + 排错） | 中。仅 B 失败才走 | 网络拉 AOSP/meta-oe；armhf 静态依赖（apt 或自编 openssl） |
| 3 主机 udev | ~5 分钟 | 低 | 无 |
| 上板刷写 + 抓 log | 硬件会话（用户起 mtkclient + 插 download 线） | 中。复用既有 flash/capture 流程 | 用户操作；C3 桥在线 |

**依赖链**：阶段 1 →（阶段 2 内核侧验收）→ 阶段 2 设备端 → 阶段 3 主机。阶段 3 可与 2 并行。阶段 2 B 与 A 互斥（B 失败才走 A），但 initramfs setup 脚本共用。

**关键外部依赖**：A 路径需要网络拉 AOSP + meta-oe（报告已实测 URL 200 可达）；静态 armhf 库需 `dpkg --add-architecture armhf` 装包或自编 openssl。无其他不可控依赖。

---

## 3. 与"修 USB ACM termios"比哪个更划算

**先明确一个关键事实：两者不互斥。** configfs 方案里 `acm.usb0` 与 `ffs.adb` 同 composite 可共存（阶段 1 已保留 `USB_CONFIGFS_ACM`）。所以**修 ACM termios 不阻塞后续上 adbd**，是互补而非二选一。

| 维度 | ADB（本计划） | 修 USB ACM termios |
|---|---|---|
| 内核改动 | 3 CONFIG + 关 1，零驱动代码，+~40KB | 无（g_serial 已在，纯用户态 termios：raw/关 echo/波特率） |
| 设备端 | adbd（B 零编译 或 A 半天-1天编译） | 无 |
| 主机端 | adb 已装 + 5 分钟 udev | `screen`/`minicom`/`socat` 直连，零准备 |
| 落地时间 | ~1 天（含上板） | **~1 小时**（若 termios 只是 raw+关 echo） |
| 能力 | `adb shell` + **`adb push/pull` + `adb install`** + 标准工具链 + 未来 adb tcpip | shell + 文件传输（rzsz/zmodem 或 shell 硬拷），无 push/install |
| 风险 | 设备端 adbd Android 残留是主要风险区 | 极低 |
| 协议标准化 | ADB 标准协议，host 侧生态完整 | 自定义/串口，靠 socat/minicom |

**结论**：
- 若需求只是"**交互 shell + 抓 log**"→ 修 ACM termios 更划算，1 小时内可用，且**不阻塞后续 adbd**，应**并行先做**（ADBD_PLAN §4.2 明确建议）。
- 若需求是"**标准 adb 工具链 + push/pull/install + 未来扩展**"→ ADB 更划算：内核侧成本极小（30 分钟、零代码、vermagic 不变），设备端有零编译原厂 adbd 可先试，总成本可控在 ~1 天，换来的能力面远大于 termios。
- **推荐执行顺序**：先把 ACM termios 修好兜底（低风险 1 小时）→ 同轮或下一轮上阶段 1 内核（30 分钟）→ 阶段 2 B 路径试原厂 adbd → 失败转 A。这样任一环节失败都有可用交互通道，且 ACM/adb 共存不冲突。

---

## 4. 分步执行清单

### 内核（阶段 1）
- [ ] 1.1 备份当前 `.config` 到 `bridge_backups/`（带时间戳）
- [ ] 1.2 `scripts/config` 开 `USB_CONFIGFS` `USB_CONFIGFS_F_FS` `USB_CONFIGFS_ACM`、关 `USB_G_SERIAL`；grep 校验
- [ ] 1.3 `make olddefconfig` + 确认 `auto.conf` 仍含 `CONFIG_USB_CONFIGFS` + `CONFIG_DEBUG_LL`（若沿用 debugll 基线）
- [ ] 1.4 `make clean` + 重编 `mt6572-z1.dtb zImage`
- [ ] 1.5 cat 拼 dtb → mkbootimg 打包 `out/boot_mainline_adb.img`（appdtbdual 风格，≤6MB 校验）
- [ ] 1.6 产物归档 `bridge_backups/products/` + `git add/commit/push`
- [ ] 1.7 上板刷写 + 抓 log，验收：`/sys/class/udc/musb-hdrc`、`mount -t configfs`、`mount -t functionfs` 出 ep0/ep1/ep2、`echo musb-hdrc > UDC` 后 `lsusb` 见 VID:PID

### 设备端 adbd（阶段 2）
- [ ] 2.1 **B 路径**：拷贝原厂 `out/boot_extracted/rd/sbin/adbd` 进 initramfs；initramfs 加 configfs setup 脚本（建 gadget、`functions/ffs.adb`、挂 `/dev/usb-ffs/adb`、绑 `musb-hdrc`、起 adbd）
- [ ] 2.2 缓解 Android 残留：`adduser shell`（uid/gid 2000）、`mkdir -p /system/bin && ln -sf /bin/sh /system/bin/sh`、`mkdir /dev/log`
- [ ] 2.3 重打包 ramdisk（同步 initrd-end，busybox-safe 脚本既有）+ 归档 + git 上传
- [ ] 2.4 上板：`adb devices -l` 验证；若 `offline` → `adb kill-server` 重试、换官方 platform-tools
- [ ] 2.5 若 B 失败（property/log 残留导致不可用）→ **A 路径**：拉 meta-oe 配方 + AOSP 源（固定 SRCREV）+ 17 patch；装 armhf 静态库（glibc A1 或 musl A2，带 `adb_libssl_11.diff`）；`make -f adbd.mk … -static` 出干净 adbd；`file`/`readelf -l` 验证 static；替换 initramfs 里的 adbd；归档 + git 上传
- [ ] 2.6 上板复验 `adb devices` → `adb shell` → `adb push/pull`

### 主机（阶段 3，可与 2 并行）
- [ ] 3.1 `sudo groupadd adbusers && sudo usermod -aG adbusers $USER`
- [ ] 3.2 建 `/etc/udev/rules.d/99-z1-adb.rules`（0c01/200e → 0666 plugdev）
- [ ] 3.3 `sudo udevadm control --reload-rules && sudo udevadm trigger`
- [ ] 3.4 预验证：内核起来后插 Z1，`lsusb`/`dmesg` 确认 CDC-ACM 枚举（g_serial 阶段证明 musb 通路）
- [ ] 3.5 设备侧 ADB 就绪后：`adb kill-server; adb start-server` → `adb devices -l` → `adb shell`

### 兜底（并行，低风险）
- [ ] 4.1 先修 ACM termios（raw/关 echo/波特率）当交互通道，作为任一环节失败时的可用退路

---

## 5. 风险与纪律

1. **preloader LineState 坑**：Z1 接电脑 USB 干扰 boot（进 download 循环）。ADB 交互 = 正常开机（拔 USB）+ **内核完全起来后再插 USB**，与项目既有纪律一致。
2. **旧 adbd vs 新 host adb**：原厂静态 adbd 是 3.4.67 时代（Android ~4.x）产物，与 adb 34.0.4 向后兼容概率高但未验证；`offline` 时先换官方 platform-tools 再试。
3. **vermagic 纪律**：全程默认 make，**绝不**在命令行设 `LOCALVERSION=`；本次 CONFIG 变更不碰 UTS_RELEASE，现有 CONNSYS .ko 不重编。
4. **改前备份 + 每步 git**：任何改源/改 img 前备份到 `bridge_backups/`；每步修复完成立即 `git add/commit/push`；产物归档 `bridge_backups/products/`。
5. **改 config 后必须 make clean 重编**（增量不触发已关 .o 重生成）；配完 syncconfig 确认 DEBUG_LL 没被 olddefconfig 退掉。
6. **appdtbdual 打包纪律**：kernel 段 cat 拼 dtb（mainline APPENDED_DTB 用）+ mkbootimg 仍带 `--dt`（LK dt_size!=0，否则卡 SSSS/DFO）；ramdisk 用 `out/forensic/linux/ramdisk.gz` MTK ROOTFS blob，不用标准 gzip-cpio。
7. **上板时序**：先起 mtkclient 等待，再按开机键（顺序反了连不上）。

---

## 6. 参考文件索引

- 内核规划: `/home/<user>/claude/6572/mainline_recon/ADB_KERNEL_20260815.md`
- 设备端规划: `/home/<user>/claude/6572/mainline_recon/ADBD_PLAN_20260815.md`
- 主机规划: `/home/<user>/claude/6572/mainline_recon/ADB_HOST_20260815.md`
- 原厂静态 adbd: `/home/<user>/claude/6572/out/boot_extracted/rd/sbin/adbd`（同 `out/forensic/live/rd/sbin/adbd` / `out/forensic/rd_stock_extract/sbin/adbd`）
- 内核源: `/home/<user>/claude/6572/fork_linux/.config`、`drivers/usb/musb/mediatek.c`、`drivers/usb/gadget/Kconfig`（USB_CONFIGFS@228、USB_CONFIGFS_F_FS@369）、`arch/arm/boot/dts/mediatek/mt6572.dtsi:484`
- meta-oe 配方: `meta-openembedded/scarthgap/meta-oe/recipes-devtools/android-tools/android-tools`（`android-tools_5.1.1.r37.bb`、`adbd.mk`、`core/0001..0017`、`core/adb_libssl_11.diff`、`android-tools-conf/android-gadget-setup`）
- AOSP functionfs 证据: `system/core/2314b110…/adb/usb_linux_client.c`（`usb_init()` → `usb_ffs_init()`）
