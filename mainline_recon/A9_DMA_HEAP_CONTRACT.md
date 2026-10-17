# A9 ↔ mainline 内核接口契约（DMA-BUF / ION 与缓冲区分配）

面向：持有 LineageOS-16(Pie) 源树的构建机。本文件规定**内核侧交付物**与**userspace 侧必须配合的点**，避免两边各改各的、最后无法归因。

## 1. 内核侧事实（已在 `wip/a9-mainline` 落地）

- 内核版本串 / vermagic：`7.0.0-rc7-z1+`（`CONFIG_LOCALVERSION="-z1"` + `LOCALVERSION_AUTO=n`）。**任何 `.ko` 必须与本轮 zImage 同轮编译**，否则 vermagic 失配。
- `CONFIG_ANDROID_BINDER_IPC=y`，`CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"`，`CONFIG_ANDROID_BINDERFS=n`。
  ⇒ `/dev/binder`、`/dev/hwbinder`、`/dev/vndbinder` 由驱动静态注册，**AOSP 侧不需要 binderfs 挂载逻辑**，`init.rc` 里不要加 `mount binderfs`。这是相对 stock 线最大的解放：S1 系列为了绕开"没有 hwbinder"而做的 `ro.z1.stock_kernel` 门控、本地 passthrough 工厂、禁用 hwservicemanager 那一整套补丁，在这条线上**应当撤掉**，回到标准 HIDL 路径。
- `CONFIG_ANDROID_ASHMEM=y`（从 `origin/wip/android9` 回移 985 行实现，`drivers/android/ashmem.c`，uapi `__ASHMEMIOC=0x77`，`SET_PROT_MASK` 按值传参）。
- **没有 `/dev/ion`**：主线已删除 ION。本内核提供 `CONFIG_DMABUF_HEAPS=y` + `_SYSTEM` + `_CMA`，即 `/dev/dma_heap/system` 与 `/dev/dma_heap/system-uncached`/`linux,cma` 形态的 heap 字符设备，`DMA_HEAP_IOCTL_ALLOC = _IOWR('H', 0, struct dma_heap_allocation_data)`。
  DTS 里 CMA 池为 `default-pool`（96 MiB，`linux,cma-default`），是 `system-cma` 类分配的后端。
- 显示：本轮镜像走 LK 遗留的 `chosen/framebuffer0`（simple-framebuffer，240x320 r5g6b5），`&dsi` 与 panel 仍 disabled；`CONFIG_DRM_LIMA=y` 已编入但**本轮未上板复验**，不作为 A9 显示前置。
- `CONFIG_SYNC_FILE=y`、`CONFIG_DMA_SHARED_BUFFER=y`：Pie 的 `hardware/libhardware/include/hardware/sync.h` 走 `sync_file` 时用 `SYNC_IOC_WAIT`/poll，这条在本内核可用；旧的 `/dev/sw_sync` 主线已无（探针会报 `sw_sync_dev=FAIL`）。
- 存储：`FUSE_FS=y`（Pie 的 `sdcard` FUSE 视图可用，不需要 sdcardfs）、`EXT4_FS_SECURITY=y`、`TMPFS_XATTR=y`、`BLK_DEV_DM/DM_VERITY/BLK_DEV_LOOP=y`、`KEYS/FS_ENCRYPTION=y`。
- 网络：`NETFILTER=y`、`IPV6=y`、`XFRM_USER=y`；`NETFILTER_XT_MATCH_QUOTA2=m`、`NETFILTER_XT_TARGET_IDLETIMER=m`（**模块**，需随 rootfs 一起安装并由 netd/iptables 按需加载）。
- 内存：`ZRAM/ZSMALLOC/LZ4=y`、`MEMCG/CGROUP_FREEZER/CPUACCT/CPUSETS/CGROUP_SCHED/CGROUP_DEVICE=y`，命名空间齐全 ⇒ `libprocessgroup` 的 cpuset/freezer 路径成立。
- 自证：`IKCONFIG_PROC=y` ⇒ 板上 `zcat /proc/config.gz` 可直接核对，避免"到底编进去没有"的争论。

## 2. userspace 侧必须做的两件事（ION 缺口）

Pie 的 `hardware/libhardware` 与厂商 gralloc 用 ION ioctl。内核无 `/dev/ion`，二选一：

- **方案 A（推荐，改动面小且可回退）**：在 AOSP 侧加一个**ION→dma-heap 兼容层**，把 `ION_IOC_ALLOC` 翻译成 `open("/dev/dma_heap/system") + ioctl(DMA_HEAP_IOCTL_ALLOC)`，`ION_IOC_FREE` 退化为 `close(fd)`；保留 `ION_IOC_MAGIC` 与结构体定义仅为编译兼容。落点通常在 `system/core/libcutils` 或 `hardware/libhardware/gralloc*`。
- **方案 B**：把 gralloc 直接写成 GBM/`libdrm` prime 导出（`dma-buf` fd），彻底不碰 ION。工作量更大但更贴近 mainline。

无论哪种，**显示侧本轮先按 fbdev/simple-framebuffer 出图**（`gralloc` 可用 `map/unmap + CPU` 或 GBM→CMA 均可），lima/GBM 作为第二阶段。

## 3. 交付物接口（内核 → 构建机）

| 交付物 | 说明 |
|---|---|
| `boot.img` | 追加式 DTB（`zImage || dtb`，`CONFIG_ARM_APPENDED_DTB=y`），MTK boot 头 `--mtk 1`，`--base 0x10000000 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 --pagesize 2048`；**boot 分区上限 6,291,456 B** |
| `.config` | 完整符号表（同时进 `IKCONFIG`，板上可自证） |
| `.ko` 集 | `xt_quota2.ko`、`xt_IDLETIMER.ko`（+ 后续模块）需随 rootfs 安装，vermagic 必须为 `7.0.0-rc7-z1+` |
| uapi 头 | `linux/ashmem.h`（回移版）；`linux/android/binder.h` 用主线原版 |
| `linux,initrd-end` | 规则 `0x84100000 + ramdisk body 字节数`，与 ramdisk 512B 头 0x04 小端字段互镜像；换 ramdisk 必重算（`tools/z1_repack_a9_ramdisk.sh` 会打印） |
| bootargs | **必须写在 DTS `chosen` 里**：本启动链的 LK 不转发 boot.img 头 `--cmdline`。A9 需要的 `androidboot.hardware=z1 androidboot.selinux=permissive` 要进 dts 并重编 dtb |
| 分区 | `bootimg`（boot）、`android`=p4（system，943,718,400 B）、`cache`=p5（236,978,176 B）、`userdata`=p6（**几何未实测，禁止格式化**） |

## 4. 本轮探针会给出的判据（决定下一步）

| 探针项 | 预期 | 若 FAIL 的含义 |
|---|---|---|
| `binder` / `hwbinder` / `vndbinder` | PASS | binder 域没注册 → 检查 `ANDROID_BINDER_DEVICES` 与 `CONFIG_ANDROID_BINDER_IPC` |
| `ashmem_open` + `set_prot_mask` | PASS | 回移实现未编入或未 `mdev` 建节点 |
| `timerfd_boottime` | PASS（与 stock 3.4.67 的 EINVAL 形成对照，用来证明"换内核是对的"） | 内核时钟子系统异常，A9 全线不可行 |
| `cgroup_freezer/cpuset/cpuacct/memory` | PASS | `libprocessgroup` 无法工作，ANR/后台治理失效 |
| `xattr_security_selinux` | PASS（tmpfs） | `TMPFS_XATTR`/`EXT4_FS_SECURITY` 缺失 → vold/installd 打标签失败 |
| `selinux_securityfs_mount` | PASS | SELinux 未编译 |
| `dma_heap_system_open` / `dma_heap_alloc` | PASS | dma-heap 未开 → 方案 A 的兼容层没有后端，必须先解决 |
| `ion_dev` | **FAIL(-2) 属预期** | — |
| `sw_sync_dev` | **FAIL 属预期** | Pie 需走 `sync_file`，不能用旧 staging fence |
| `binderfs_registered` | ABSENT（与 `ANDROID_BINDERFS=n` 一致） | 若 PRESENT 说明配置漂移 |
| `config_gz_present` | PASS | 无法板端自证配置 |

## 5. 已知遗留风险

1. `&mmc0` 目前是 `bus-width = <1>`（1-bit eMMC）。本轮把 4-bit 修复单独隔离成 B 镜像，A/B 差分判定其影响；历史上有 CMD1 楔死（修复在 `b1380bf32`），不得在重构中丢失。
2 boot 分区余量实测约 **1.08 MB**（按当前 v13.7 体积 ramdisk 计），A9 若要求更大的 initramfs/更多内建驱动，须优先裁 ramdisk（如外置 WiFi 固件）而非裁符号。
3. 本机构建产物只能证明"内核能力面"；A9 语义（init/SELinux policy/HIDL 注册）仍须在持有 LOS16 树的机器上验证后回灌。
