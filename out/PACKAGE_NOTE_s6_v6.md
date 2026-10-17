# PACKAGE NOTE — s6_v6（boot_mainline_s6_v6.img / ramdisk_v13_6_acm.gz）

日期：2026-08-29（任务 #35）
前序：s6_v5 刷入 S6 后 ~25s 周期无限重启，ACM gadget 从未枚举 → 内核死在 userspace 前。

## 一、三处 dts 改动（只改 `fork_linux/arch/arm/boot/dts/mediatek/mt6572-s6.dts`）及其根因依据

### 1. memory 节点：1GB → 512MB（hedge + 判别双用途）
- 改动：`reg = <0x80000000 0x20000000>`（原 0x40000000）。
- 依据：S6 实际 RAM 容量从未证实；"声明少于物理永远安全，声明多于物理必死"
  （内核触碰不存在/未上电的行 → 用户态前静默复位，正是 s6_v5 的死法：
  0 USB 枚举 + ~25s 复位周期）。本启动链内核不消费 ATAG（Z1 实证 "No ATAGs?"），
  appended DTB 是唯一内存视图，写错即无任何纠错通道。
- 判别：若 s6_v6 启动成功 → 锁定 "1GB 写死" 为 s6_v5 根因；之后可向上安全测量真实容量。

### 2. reserved-memory 新增 ramoops@86000000（崩溃黑匣子）
- 依据：.config 中 CONFIG_PSTORE/CONFIG_PSTORE_RAM/CONFIG_PSTORE_CONSOLE/
  CONFIG_PSTORE_PMSG/CONFIG_REED_SOLOMON/CONFIG_OF_RESERVED_MEM 全部已开，
  只差 dts 节点，零 config 改动。
- 选址 0x86000000+0x100000：避开内核（0x80008000+）、initrd
  （0x84100000-0x84235fe4）、fb（0xbfd00000）、CONN（0xbe000000），且落在
  512MB 界内（< 0xA0000000）→ 即使 S6 只有 512MB 也被映射。
- 参数：record-size 0x20000 / console-size 0x80000 / pmsg-size 0x10000 / ecc-size 16。

### 3. &mmc0 status = "disabled"（隔离变量）
- 依据：mtk-sd 驱动按 `of_machine_is_compatible("jbo,z1")` 门控，S6
  （"alps,s6"）上 mmc0 probe 必然失败/defer，会污染崩溃调查。
- 待办：修复驱动机器兼容门控（接受 S6）后恢复 `status = "okay"`。

## 二、/init（v13.6）改动（`out/init_v13_6.src`）
= v13.5 init + 两处：
1. proc/sysfs 挂载之后立即 `mount -t pstore pstore /sys/fs/pstore` 并自报告。
2. [klog] 转发块内、`dmesg` 之前：若 `/sys/fs/pstore/console-ramoops-0` 存在，
   先以横幅 `[pstore] === previous crash console log follows ===` 把上一轮
   崩溃的串口录像从 USB（ttyGS0）吐出。其余（gadget、getty、PID1 重生循环）不动。

## 三、产物哈希与尺寸

| 文件 | 大小 (B) | sha256 |
|---|---|---|
| out/boot_mainline_s6_v6.img | 5,083,136 | `bead8e3dff59063069e8fdd2eddab931668f9e0c3f5d60e3e3cb506454a3db0a` |
| out/ramdisk_v13_6_acm.gz | 1,270,244（body 1,269,732 = 0x135fe4） | `dc7e2623334073c8e0ce48fd168fd1f01e43e6d3391dbd8d50834c708002864b` |
| out/init_v13_6.src | — | 见仓库 |
| fork_linux .../mt6572-s6.dtb | 19,230 | `52fd96b39a595f24446ba7e36f91cd8a9390a0aa3b3a43b69ab1bd172acf9e6d` |
| fork_linux .../zImage（复用，未重编） | 3,789,312 | `e099f0cddd1ef7912658089345352404533a7e07ec02994db26b049a569cbba0` |

- s6.dts `linux,initrd-end = 0x84235fe4`（= 0x84100000 + 0x135fe4）。
- 镜像 ≤ 6,291,456 B ✅，ANDROID! magic ✅，kernel 段剥 512B MTK 头 ==
  cat(zImage+dtb) ✅，ramdisk 段逐字节 == ramdisk_v13_6_acm.gz ✅，
  内嵌 /init 含 pstore 挂载/[pstore] 吐回/[klog] 块/重生循环，`sh -n` 通过 ✅。

## 四、刷写命令（回滚件 = S6 原厂 boot）

```bash
cd /home/<user>/claude/6572
# 先起 mtkclient（脚本轮询等待 preloader/BROM 窗口），再由用户按开机键；
# 下载线直插主板后置 USB，勿经 hub。
tools/flash_with_retry.sh bootimg out/boot_mainline_s6_v6.img /home/<user>/claude/6572/mt6572-2/extracted/boot.img 8
```

刷后主机侧认 0e8d:2006，按 by-id 抓 ACM 口（勿硬编码 /dev/ttyACM0，可能与
C3 桥撞口）：`ls -l /dev/serial/by-id` 先确认。

## 五、验证判据

- **判据 A（启动成功）**：USB 上出现 0e8d:2006 ACM → 打开端口应依次收到
  `[pstore]` / `[acm]` / `[klog]` 块。**根因 = "1GB 写死" 锁定**；后续把
  memory 向上试（如 0x30000000）找真实容量。若 /sys/fs/pstore 下还有上一轮
  崩溃的 console-ramoops-0，也会先吐出来（本轮是首刷，无旧记录）。
- **判据 B（仍 ~25s 循环）**：重启间隙（设备进 preloader 窗口）用主机
  mtkclient 直读 ramoops 区：
  ```bash
  cd /home/<user>/claude/6572/mtkclient_v2141   # 或实际 mtkclient 路径
  python3 mtk.py da peek 0x86000000 0x100000 --filename /tmp/ramoops_s6_v6.bin
  # 备选语法：python3 mtk.py peek 0x86000000 0x100000
  ```
  拿到 1MB 后：`strings`/`hexdump` 找内核行（console-size=0x80000 区在前
  0x90000 内，dmesg records 在尾部 3×0x20000）。有内容 → 定位死点；
  全 0/乱码 → ramoops 未被写入（死得太早或节点未生效），转 DEBUG_LL 路线。

## 六、待办
1. mtk-sd `of_machine_is_compatible("jbo,z1")` 门控缺陷修复（接受 alps,s6），
   之后恢复 &mmc0。
2. 判据 A 命中后，memory 容量向上测量。
3. Z1 侧镜像（v13.5 及更早）与 z1.dts 本轮零改动。

## 七、构建复现
重打脚本：`tools/rebuild_ramdisk_v13_6.sh`；自验脚本：`tools/verify_s6_v6.py`。
改前备份：`bridge_backups/pre_s6_v6_20260829_232911/`。

## 八、审计拆雷记录（2026-08-30，任务 #39，重打同名覆盖）

### 根因（审计定案的阻塞项）
mt6572-s6.dts chosen 里的 `framebuffer0: framebuffer@bfd00000`
（compatible simple-framebuffer）位于 512MB 界（0xA0000000）之外；而内核
FB_SIMPLE=y + FRAMEBUFFER_CONSOLE=y + bootargs `console=tty0` → 启动期 fbcon
绑定该 fb 后写屏访问不存在的内存 → abort/挂死 → `panic=10` 无限重启。
这是 s6_v6 原镜像（bead8e3d…）上板前必须拆除的雷。

### 修复
- 删除 chosen 中 `framebuffer0` 节点（含注释，留拆除说明）。
- 顺带删除 `reserved-memory` 中 `framebuffer_reserved@bf400000`（12 MiB no-map
  死重，同在 512MB 界外）。
- 其余全部不动：memory=0x20000000、ramoops@86000000、&mmc0 disabled、
  bootargs 逐字不变、initrd-end 仍 0x84235fe4。
- 复用已验证 zImage（sha256 e099f0cddd1ef7912658089345352404533a7e07ec02994db26b049a569cbba0，
  未重编）；ramdisk 仍为 out/ramdisk_v13_6_acm.gz
  （sha256 dc7e2623334073c8e0ce48fd168fd1f01e43e6d3391dbd8d50834c708002864b，未动）。

### 新哈希与产物终值表
| 项 | 值 |
|---|---|
| 新镜像 out/boot_mainline_s6_v6.img | sha256 `ec4097189b47160e6ede94418fd48d93af49a84ca710c1229ee2746042987bf0`，5,083,136 B（≤6MB） |
| 旧镜像（bead8e3d…）归档 | `bridge_backups/products/products_s6_v6_fbfix_20260830_005048/boot_mainline_s6_v6.img_bead8e3d_pre_fbfix` |
| 新 dtb（fbfix） | sha256 `bc35d98390bc4d1da2192db5b62485d297c2856dc5379b7dbfb85f5823ab9d22` |
| dts 改前备份 | `bridge_backups/pre_s6_v6_fbfix_20260830_005048/mt6572-s6.dts`（sha256 dec68f47…） |

### 全链自验（实测通过）
- 镜像 ≤6,291,456 B 且头部 magic `ANDROID!`。
- 剥 512B MTK KERNEL 头后，kernel 段 == cat(zImage + 新 dtb)（cmp 逐字节一致）。
- ROOTFS 段 body 1,269,732 (0x135fe4)，与 dc7e2623… ramdisk 逐字节一致。
- 新旧镜像 cmp：差异仅在头部字段（页 0/1）与 dtb 段尾部（页 1851–1860），
  ramdisk 段（页 1861+）零差异。
- dtc 反编译：chosen 无 framebuffer0/simple-framebuffer、无 0xbfd00000/0xbf400000
  残留；memory=0x20000000；ramoops@86000000 在；mmc@11120000 disabled；
  bootargs 逐字不变；initrd-end=0x84235fe4。

### 已知盲区（审计保留）
1. ramoops 注册前的崩溃不可观测（若死在 ramoops 初始化之前，黑盒无记录，
   需转 DEBUG_LL 路线）。
2. S6 PMIC 型号未实证（dts 按 MT6323 配置，未上板确认）。

