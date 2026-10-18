# Z1 (MT6572) BoardConfig —— mainline 变体专用（本文件不再带 stock 分支）
#
# 分区/页大小全部取自本机实测：原厂 scatter 与已验证的打包链一致。
# 关键提醒写在下面：本启动链的 LK **不转发** boot.img 头的 cmdline，
# 所以 BOARD_KERNEL_CMDLINE 在这里是无效功 —— 真实 bootargs 在 dts chosen，
# 改引导参数必须改 arch/arm/boot/dts/mediatek/mt6572-z1.dts 并重编 dtb。

DEVICE_PATH := device/jiabo/z1

# MT6572 是 32 位 armv7-a(带 NEON)。mainline binder 暴露协议 8（64 位传输字段），
# Pie 支持，与 CPU 位宽无关 —— 本树已核实 arch/Kconfig:660 的 SECCOMP_FILTER 不再依赖
# BPF_SYSCALL，故内核侧 eBPF 已摘除，userspace 仍走 binder 8。
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := cortex-a7
TARGET_BOARD_PLATFORM := mt6572
TARGET_BOOTLOADER_BOARD_NAME := z1
TARGET_NO_BOOTLOADER := true
TARGET_USES_64_BIT_BINDER := true

# 内核与追加式 DTB 在 LOS 树外编译（fork_linux/a9_wt），LOS 只负责 system.img 与
# generic ramdisk；MTK 的 KERNEL/ROOTFS 包装头由本机 z1_a9_p2.sh 打包时补上。
TARGET_NO_KERNEL := true

# 旧式 root ramdisk：/system 由 init.z1.rc 单独挂，不做 system-as-root。
BOARD_BUILD_SYSTEM_ROOT_IMAGE := false
BOARD_KERNEL_BASE := 0x10000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_MKBOOTIMG_ARGS := --kernel_offset 0x8000 --ramdisk_offset 0x1000000
# 仅作"头里内容"的语义占位，不生效；真值见 dts chosen/bootargs。
BOARD_KERNEL_CMDLINE :=

# bootimg 分区 0x600000 = 6,291,456 B（原厂 scatter）。本机 pack 在打包**前**就会按
# kernel+dtb+ramdisk 投影判死 —— 超了不会给你一张静默丢 ramdisk 的镜像。
BOARD_BOOTIMAGE_PARTITION_SIZE := 6291456
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 6291456
BOARD_FLASH_BLOCK_SIZE := 131072

TARGET_USERIMAGES_USE_EXT4 := true
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
# p4 = 0x38400000, p5 = 0xE200000（scatter 实测）。p6(userdata)=0x9DDC0000 容量已知，
# 但其上现有文件系统/加密状态未实测 —— 不设 USERDATA 尺寸、不加任何 format 标志。
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 943718400
BOARD_CACHEIMAGE_PARTITION_SIZE := 236978176

TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/fstab.z1

USE_XML_AUDIO_POLICY_CONF := 1
DEVICE_MANIFEST_FILE := $(DEVICE_PATH)/rootdir/manifest.xml

# 非 Treble：不设 BOARD_VNDK_VERSION / 独立 vendor 镜像 / TARGET_COPY_OUT_VENDOR 覆盖。
# 本仓 android9/mainline/sepolicy/vendor/ 里是 mainline 版 file/genfs contexts；
# genfs_contexts 能否经 BOARD_SEPOLICY_DIRS 生效需在构建机的 Pie 规则上确认（本机无源树）。
BOARD_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy/vendor
