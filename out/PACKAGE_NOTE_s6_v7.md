# PACKAGE_NOTE_s6_v7（2026-08-30，任务 #45：回放灌 shell 根因修复）

## 一、现场问题（用户每次开机都撞）

s6_v6（/init v13.6）上，`[klog]` setsid 后台镜像（pstore 回放 + dmesg +
`tail -f /proc/kmsg`）与 ttyGS0 上的交互 getty/shell **并发**启动。回放文本
写进 /dev/ttyGS0 的同时，getty 已把同一 tty 交给 shell 作 stdin——回放字节
被 shell 当命令吞下，刷出数万 `~ #` 提示符与 `sh: syntax error`，最终把
shell 打死、ACM 口交互全灭。

## 二、根因与修复（/init v13.7，源码留档 `out/init_v13_7.src`）

根因：**回放与 shell 的时序竞争**——同一 tty 上"回放写端"与"shell 读端"
同时存在。

修复（回放先行、阻塞完成、再起 getty）：
1. 等待 /dev/ttyGS0 出现：轮询 20×1s + 超时软失败（与现版一致）。
2. 依次**阻塞**执行：`stty -F /dev/ttyGS0 -echo -echoe -echok`（防主机回显
   倒灌；该 busybox 组合不可用时回退 `stty raw`，注释已说明）→
   `[pstore]` 上一轮崩溃 console-ramoops-0 吐回（若存在）→ `[klog]` 横幅 +
   `dmesg` 全量 → `[klog] === replay done ...` 收尾。
3. 回放全部完成后才启动 `tail -f /proc/kmsg >> /dev/ttyGS0`（setsid 后台）
   与 ttyGS0 getty 重生循环（`while :; do getty -n -l /bin/sh 115200 ttyGS0
   vt100; sleep 1; done`，setsid 后台）。
4. 回放字节永远不可能与尚不存在的 shell stdin 竞争。

其余一律未动：PID1 console getty 重生循环、tty0 getty、mdev、hostname/
banner、configfs gadget 参数（0e8d:2006）、pstore 早挂载位置。

## 三、终值表（逐项实测）

| 项 | 值 |
|---|---|
| zImage（复用，未重编） | `fork_linux/arch/arm/boot/zImage`，sha256 `e099f0cddd1ef7912658089345352404533a7e07ec02994db26b049a569cbba0` |
| ramdisk body 长度 | 1,270,523 字节 (0x1362fb) |
| dts `linux,initrd-end` | `0x84100000 + 0x1362fb = 0x842362fb`（mt6572-s6.dts，已重编 + dtc 核对） |
| `out/ramdisk_v13_7_acm.gz`（1,271,035 B = 512B MTK ROOTFS 头 + body） | sha256 `f19ccb0f8269d626ac23fdecf0c5867df1d7b7b439942d198d6cadfdc45d360c` |
| `out/boot_mainline_s6_v7.img`（5,083,136 B ≤ 6,291,456 B） | sha256 `44de3ea5e7f6a823af477137f8ae06363ffc21205f9ba49d67d332eb7a867131` |
| `mt6572-s6.dtb`（18,993 B） | sha256 `0dfd07afa72c7dd1a5896c7c75d89c94691b16ee1fb17ec7300545ce27553753` |
| 改前备份 | `bridge_backups/pre_s6_v7_20260830_025011/`（旧 dts/dtb/ramdisk_v13_6/init_v13_6.src） |
| 重打脚本 / 自验脚本 | `tools/rebuild_ramdisk_v13_7.sh` / `tools/verify_s6_v7.py` |

## 四、全链自验（实测通过，`tools/verify_s6_v7.py`）

- 镜像 ≤ 6,291,456 B 且头部 magic `ANDROID!`。
- kernel 段剥 512B MTK KERNEL 头后 == cat(zImage + 新 dtb)（cmp 逐字节）。
- appended dtb magic `0xd00dfeed` 位于 zImage 边界；dtb 内含新值 0x842362fb。
- ramdisk 段 == `out/ramdisk_v13_7_acm.gz` 逐字节；头内长字段 1,270,523 == body。
- 从镜像再提取 /init：v13.7 标记、回放块（pstore 吐回 + dmesg）**全部位于**
  `tail -f` 与 ttyGS0 getty 之前（索引序断言）；等待轮询在回放之前；
  `stty -echo -echoe -echok` 在；`busybox sh -n` 通过；模式 755。
- dtc 反编译：initrd-end=0x842362fb、memory 512MB（0x20000000）、
  ramoops@86000000 在、无 simplefb、bootargs 逐字不变、mmc0 disabled
  （节点被编译器剔除）。

## 五、上板判据

刷写后主机侧认 0e8d:2006，按 by-id 抓 ACM 口（勿硬编码 /dev/ttyACM0）：

```bash
ls -l /dev/serial/by-id     # 先确认哪个口是 Z1 ACM（usb-Z1_Z1_ACM_*）
cat /dev/serial/by-id/<Z1 ACM 口>
```

- 应先看到**完整回放**（`[pstore]` 吐回或 "no previous..." → `[klog]` 三横幅
  + dmesg 全量 → `replay done`），**之后**才出 `~ #` 提示符；
- 提示符出现后输入 `id` 应有正常回应（不再被回放灌死）；
- 回放期间不应再出现成片 `sh: syntax error`。

## 六、刷写命令（回滚件 = s6_v6）

```bash
cd /home/<user>/claude/6572
# 先起 mtkclient（脚本轮询等待 preloader/BROM 窗口），再由用户按开机键；
# 下载线直插主板后置 USB，勿经 hub。
tools/flash_with_retry.sh bootimg out/boot_mainline_s6_v7.img out/boot_mainline_s6_v6.img 8
```
