# A9 最后一段链收成一个命令（2026-10-18）

`tools/z1/z1_a9_finalize.sh --ramdisk <A9 generic ramdisk> [--system <A9 system.img>] [--variant 1b|4b] [--dir <出>]`

四步（外加一支条件步），任一步非零即停并打印"停在第几步"：
1. `z1_a9_ramdisk_accept.py` 内容级验收（形状/红线不过就不往下走）；
1b. 裸 gzip 的 ramdisk 先重打成带 512B MTK ROOTFS 头的封装（pack 会拒收无头件）；
2. `z1_a9_p2.sh pack` 出 boot.img：按实际 body 现算 `linux,initrd-end`、回写 dts、
   重编 dtb、打包**前**做 6MB 预算投影闸门、打包后反解内嵌 DTB + 两份载荷逐字节核验；
3. `z1_a9_mainline_system.py` 对 system 做镜像级改造（删 stock 标记、解 hwservicemanager
   的 disabled、投递 mainline rootdir、剥 build.prop 里的 `ro.z1.stock_kernel=1`、e2fsck+尺寸闸门）；
4. `z1_a9_flashpkg.sh` 装配可刷包（scatter 只放行 bootimg,android + 预检 + HASHES.txt）。

## 实测（夹具，因为真实 A9 system/ramdisk 不在本机）
- 正路：`--ramdisk <裸 gzip A9 形状件> --system <A9 形状 ext4>` → 四步全过，
  `RESULT: VALID` + `VERIFY=OK` + `PAYLOAD-OK(ramdisk 156907 B 逐字节完整)` +
  `e2fsck rc=0` + `放行下载的分区: bootimg, android` → `FINALIZE=OK`。
- 失败即停：喂真探针 ramdisk(`ramdisk_a9p6.gz`) → 第 1 步四条 REJECT
  （/init 是 busybox 脚本非 ARM 静态 ELF、无 first_stage_*/init.*.rc、缺 sepolicy、缺 fstab.*）
  → `停在第 1 步`，不产出任何镜像。这是对的：它本来就不是 Android。
- 顺带被链抓到的一次真夹具错误：`/init` 权限 0775，`validate_mtk_boot.py --require-init-root`
  按 AOSP 规矩要 0755，于是 `RESULT: INVALID`。**校验器比夹具严，是好事**。

## 修掉的一处自身 bug
`z1_a9_p2.sh` 的前置预算块里用了 `local proj`，而它在 `case` 分支内（不在函数里），
运行时报 `local: 只能在函数中使用`。已改为普通变量。

## 边界
跑的是夹具，不是真实 A9 件；`FINALIZE=OK` 只证明"文件一到就能一条命令出可刷包"，
不证明 A9 能在 Z1 上起（那要上板）。
