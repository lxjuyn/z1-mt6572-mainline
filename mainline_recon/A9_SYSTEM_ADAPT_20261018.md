# A9 system 镜像的镜像级改造（2026-10-18，本机可做的那部分）

## 1. 为什么这件事不需要 AOSP 源码树

stock 线那一整套"绕开 3.4.67 缺能力"的代偿，不是散落在二进制里不可触碰，而是
**由镜像内的门控开关驱动的**：`/z1_stock_kernel` 标记文件 + `ro.z1.stock_kernel` 属性
（`ServiceManagement.cpp` 先走本地 factory、`hwservicemanager`/`vndservicemanager` 被
`disabled`、configstore 用本地 passthrough 工厂）。开关一关，这些分支就落回 AOSP 默认路径——
而 mainline 线要的就是默认路径。

所以**在 ext4 上就地改这几个文件**就能撤代偿，不必重编。可行性不是推测：stock 分支
`tools/z1_stock_system_profile.py` 已经用同一手法（debugfs 原地重写单个已分配数据块，
保住 inode 与 SELinux xattr，不碰块设备）改过真实 A9 镜像。

本机没有 `simg2img`，所以工具内置了 sparse→raw 只读转换器（chunk 类型
RAW=0xCAC1 / FILL=0xCAC2 / DONT_CARE=0xCAC3 / CRC32=0xCAC4）。

## 2. `tools/z1/z1_a9_mainline_system.py` 做的五步（每步都读回校验）

1. 删 `*/z1_stock_kernel`（**由遍历结果找真实路径**，不猜）；
2. 去掉 `hwservicemanager`/`vndservicemanager` 的 rc 里的 `disabled` 行 → 恢复真 HIDL 服务；
3. 投递本仓 mainline device 层（`ueventd.z1.rc`/`init.z1.rc`/`fstab.z1`/`manifest.xml`/
   `init.z1.network.rc`/`z1_usb_setup.sh`）到 `/system/etc/`；
4. 从 `build.prop` 里移除 `ro.z1.stock_kernel=1`；
5. `e2fsck -fn` 复检 + 尺寸不得超过 android 分区 943,718,400 B；输出 `ADAPT_REPORT.json`
   （逐条前后 sha256），直接接 `z1_a9_flashpkg.sh --boot … --system …`。

关键一条：**光删标记文件不够**。属性若由 `build.prop` 写死，`ro.z1.stock_kernel` 仍是 1，
门控照走 passthrough 分支——这一步是工具自己以 WARN 报出来后我补的（原先只警告不处理）。

## 3. 实测证据

- **真实 Android 镜像**：`1/system.img`（原厂 4.4，943,718,400 B ext4）遍历到 1450 条路径，
  并正确定位 `/build.prop` 在镜像根而非 `/system/` —— 证明"靠猜路径"必然漏，也证明
  本机的 debugfs 工具链能读真实 Android 镜像。
- **A9 形状夹具**（`mke2fs -O ^metadata_csum,^64bit -b 4096`，含标记文件、
  带 2 行 `disabled` 的单块 rc、含 `ro.z1.stock_kernel=1` 的 build.prop）跑完整改造：
  标记已删 / `disabled` 归零 / build.prop 只剩其它属性 / 6 份 device 层文件投递并读回一致 /
  `e2fsck rc=0` / 9 条动作 0 条备注。
- **端到端**：改造后的 system + P4 D1 boot → `z1_a9_flashpkg.sh` 出完整包，
  只放行 `bootimg,android`，强制关闭 9 个原本 `is_download:true` 的分区
  （preloader/mbr/ebr1/uboot/recovery/sec_ro/logo/cache/usrdata）。

## 4. 本机不能替它保证的事（别当成已完成）

- 手里**没有真实 A9 system.img**（在持 LOS16 源树的那台机器上），上面全部验证跑在夹具 +
  原厂 4.4 真镜像上：证明**手法与工具链可用**，不证明那份具体镜像改造后能起。
- 不改任何 `.so`/`.jar`：S1 系列打在源码里的补丁仍在二进制里，只是分支被门控关掉。
  如果那台机器的 system 是用**已撤销这些补丁**的方式重建的，本工具的第 1/2/4 步就是空操作
  （工具会如实报"没找到标记"）。
- SELinux 仍靠 dts 的 `androidboot.selinux=permissive` 首轮收 denial；`metadata_csum` 开启的
  ext4 会被工具**直接拒绝**原地改块（校验和会坏）。
- 未上板。

## 5. 一旦拿到 A9 system.img 的完整流程

```sh
python3 tools/z1/z1_a9_ramdisk_accept.py <generic ramdisk>          # 验收 ramdisk
A9_RAMDISK=… tools/z1/z1_a9_p2.sh pack 1b out/boot_mainline_a9p5.img # 出 boot（预算/闸门/载荷核验）
python3 tools/z1/z1_a9_mainline_system.py --system <system.img> --out out/a9sys   # 镜像级改造
tools/z1/z1_a9_flashpkg.sh --boot out/boot_mainline_a9p5.img --system out/a9sys/system_mainline.img
```
