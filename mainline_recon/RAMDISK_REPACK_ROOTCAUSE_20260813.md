# Z1 ramdisk 重打包 VFS boot 失败根因（2026-08-13）

> 来源：workflow `wgmq0h9pm`（6 agent 联网搜索 + 源码深挖）+ 独立根因 agent
> 修复已实施：`bd9d5590d`（repack 同步 initrd-end）

## 根因（定案）

**dts chosen 硬编码 `linux,initrd-end` 截断重打包 body**：
- `mt6572-z1.dts:42` `linux,initrd-end = <0x8420ac7d>`（=1092733B，只匹配原版 body 1068865B）
- 重打包 connsys body 1627523B 超区间 53% → 内核只读前 1092733B → gzip EOF 截断 → initramfs 解包失败 → `/dev/ram` 挂载失败 → VFS panic
- Z1 LK 不传 ATAG_INITRD2，内核**只信 DT chosen 区间**读 initrd（drivers/of/fdt.c early_init_dt_check_for_initrd）
- 改 MTK ROOTFS 头 size 字段无效（那是 LK 用的，内核不读）

## 证据链
- 4 份失败日志全部 `Freeing initrd memory: 1068K`（= DT 区间 1092733B，非 connsys 1590K）
- `rootfs image is not initramfs (uncompression error)` + `RAMDISK: EOF while reading compressed data`
- 原版 body 1068865B ≤ 1092733B 能 boot；connsys 1627523B 超 → 截断
- 原版与重打包 body 字节级同构（65 条 newc 同序），格式非问题

## 修复（已实施 bd9d5590d）
repack 脚本自动同步：`initrd-end = 0x84100000 + 新 body 长` + 重编 dtb。connsys_mod 镜像带 `initrd-end=0x8428d583` dtb。

## 可靠性补充（workflow 建议）
- Z1 板级 ramdisk 加载**随机不可靠**（历史 1068K 有 fail 率），connsys 1590K 更超边界
- **方案 D**：`CONFIG_INITRAMFS_SOURCE` 把 rootfs 编进内核 → 外置 ramdisk 失效无害，cmdline 去 root=/dev/ram 留 rdinit=/init，尺寸仍 <6MB
- **方案 C（最稳）**：ramdisk 零重打包（用原版），CONNSYS 模块放 eMMC（p5/p4），启动后 mount + insmod
- 方案 A（atags_to_fdt）无效；方案 B（只改头）不解决

## 关键文件
- `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts:41-42`（chosen initrd-end）
- `tools/repack_ramdisk_connsys.sh`（含 initrd-end 同步）
- 产物 `out/boot_mainline_z1_connsys_mod.img`（含匹配 dtb）
