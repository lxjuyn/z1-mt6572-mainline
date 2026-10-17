# PACKAGE_NOTE v13.5 (Z1) + s6_v5 (S6) — 2026-08-29, host-side, no flash（klog-over-ACM 版）

本轮 = 任务 #29：实现 USB 内核日志通道并打包新镜像。**未刷写任何设备。**

## 与 v13.4r 的差异（唯一差异）
/init 在第二次 `mdev -s` 之后、`[ -e /dev/ttyGS0 ]` 块内、getty setsid 之前
插入 **[klog] 内核日志转发**（后台化，语义逐字）：

```
echo "[klog] starting kernel-log mirror on /dev/ttyGS0"
$BB setsid $BB sh -c '
  echo "[klog] === dumping kernel ring buffer to ACM ==="
  /bin/busybox dmesg > /dev/ttyGS0
  echo "[klog] === live tail /proc/kmsg begins ==="
  exec /bin/busybox tail -f /proc/kmsg >> /dev/ttyGS0
' </dev/null >/dev/null 2>&1 &
echo "[klog] mirror launched (pid $!)"
```

- 先 `dmesg` 全量历史（128K printk ring），再 `tail -f /proc/kmsg` 实时，三行横幅自报。
- 后台化必须：`open(/dev/ttyGS0)` 阻塞到主机侧打开 ACM 口。
- `/proc/kmsg` 单读者，当前无其他 kmsg 读者，安全。
- 目的：S6 无串口触点，靠 USB ACM 吐内核日志。
- 其余 /init 逻辑一律不动（含末段 getty 重生循环）。
- zImage 复用源树已验证件，未重编；两 dts 仅 `linux,initrd-end` 0x84235bf0 → 0x84235d32
  （= 0x84100000 + 新 body 1,269,042 = 0x135d32）。

## 产物（实测，2026-08-29）
| 文件 | 大小 | sha256 |
|---|---|---|
| out/boot_mainline_v13_5.img | 5,083,136 B (≤6MB ✓, ANDROID! ✓) | 84d5f1e5610825e2c86ea1660a0fb30ed88da7ead25b2988657710039a692bc6 |
| out/boot_mainline_s6_v5.img | 5,081,088 B (≤6MB ✓, ANDROID! ✓) | 99dbae278476a7d8bbc69121ccd815cbe050d7951bfc32a49153cfd01a4a7c80 |
| out/ramdisk_v13_5_acm.gz | 1,269,554 B (512B MTK 头 + body 1,269,042 = 0x135d32) | 82aafaf3b5bf914239f87308670794968b60614dd63958891b39c6b9dafb44f4 |
| out/init_v13_5.src（/init 源，98 行） | 4,432 B | 7bc1c6ec84e47098838e52519c4be2e66b8c83e60c9d859b985b97bbef9af358 |

复用件（未变）：
- zImage: sha256 `e099f0cddd1ef7912658089345352404533a7e07ec02994db26b049a569cbba0`（源树 `fork_linux/arch/arm/boot/zImage`，打包前后均复核）
- sbin/i2cprobe: sha256 `34905dbd20e75506d57a5a9c2b5be959a9b41d27c8d43fae092ecff988e97bb2`（= tools/i2cprobe）
- dtb: mt6572-z1.dtb `dd76c4ec9fc8f160703e566101c349e48156dc8903b730e64a2b9dedcd4aaa4b`、mt6572-s6.dtb `e2073c8f4adab1dbedbe71f3c5fe777109d8a367f081b155b6522b1fb93ce46b`

新三件（两镜像 + ramdisk）已登记进工作区根 `SHA256SUMS` 尾部。
旧件 `out/ramdisk_v13_4_acm.gz` / `out/boot_mainline_v13_4.img` / `out/boot_mainline_s6_v4.img` **未动**
（复核：旧镜像内嵌 ramdisk 段 sha 仍为 a8c05ed6…）。

## 自验结论（S5 全过）
- 两镜像 ≤6,291,456 B 且头 `ANDROID!` ✓
- 剥 512B MTK KERNEL 头后 kernel 段 == cat(zImage+对应dtb) 逐字节 ✓
- 内嵌 ramdisk 段 == out/ramdisk_v13_5_acm.gz 逐字节 ✓（头长度字段 == body 1,269,042）
- 提取新镜像内嵌 /init：含 [klog] 三横幅 + `tail -f /proc/kmsg`，末段仍是 `while :; do getty; sleep 1; done` 重生循环 ✓
- 新旧镜像逐段对比：zImage 段逐字节相同；dtb 段反编译 diff 仅 `linux,initrd-end` 一行
  （0x84235bf0 → 0x84235d32，S3 预期改动）；ramdisk 段为新 payload；其余区域相同 ✓
- 反编译核对：bootargs 未变（末位 `console=ttyS0,921600n8`），mmc@11120000 / phy@11110800 status okay，usb@11100000 无 disabled ✓

## 刷写命令（待办 #30 上板时执行，勿提前）
Z1：
```
cd /home/<user>/claude/6572
tools/flash_with_retry.sh bootimg out/boot_mainline_v13_5.img /home/<user>/claude/6572/1/boot.img 8
```
S6（回滚件用 S6 自己的原厂件，**禁复用 Z1 原厂件**）：
```
cd /home/<user>/claude/6572
tools/flash_with_retry.sh bootimg out/boot_mainline_s6_v5.img /home/<user>/claude/6572/mt6572-2/extracted/boot.img 8
```
时序：先起脚本（轮询 preloader/BROM 窗口）再按开机键；下载线直插主板后置 USB，勿经 hub；
刷写才插下载线，正常开机拔掉。

## 主机侧抓日志方法（klog 通道）
1. `lsusb` 认出 `0e8d:2006`（Z1 ACM gadget）。
2. 按 by-id 抓口（勿用硬编码 `/dev/ttyACM0` 的旧脚本——可能与 C3 桥抢口）：
   ```
   cat /dev/serial/by-id/usb-Z1_Z1_ACM_Z1MT65720001-if00
   # 或对应的 /dev/ttyACM*（先 ls -l /dev/serial/by-id 确认）
   ```
3. CDC-ACM 无需设波特率。与 C3 桥（Espressif/GigaDevice/1a86 by-id）按 by-id 区分。
4. 打开端口即解除 /init 里 klog 后台进程的 ttyGS0 open 阻塞。

## 验证点（上板预期）
ACM 口打开后应依序见到：
1. `[klog]` 三横幅（starting → dumping ring buffer → live tail begins）
2. dmesg 全量历史日志（内核启动全程）
3. 实时日志（后续任何 printk）
4. getty 提示（ttyGS0 getty 与 klog 共用该口，交互时注意交错）
