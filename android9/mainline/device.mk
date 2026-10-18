# Z1 (MT6572) device 层 —— mainline 变体专用（本文件不再带 stock 分支）
#
# 落地位置：AOSP 机的 device/jiabo/z1/device.mk（本仓这份是给它的成品底稿）。
# 与 stock 分支那份的差异，都是本轮 z1_a9_devlayer_check.py 对账出来的**真实缺陷**：
#  1) ro.hardware.gralloc / ro.hardware.hwcomposer 原先只写在 `ifeq stock` 里 ——
#     allocator@2.0-service 靠这两个属性决定 dlopen 哪个 HAL 模块；mainline 上缺它们
#     就会去找 gralloc.mt6572.so / hwcomposer.mt6572.so（不存在）-> SurfaceFlinger 拿不到
#     任何 buffer，bootanimation 直接黑屏。
#  2) mainline 分支不复制 ueventd.z1.rc -> /dev/dma_heap/*、/dev/net/tun、/dev/watchdog、
#     /dev/input/* 的权限没人设，uid=system 的进程 DAC 层就打不开。
#  3) 撤掉 stock 的 configstore 假工厂后，必须补 AOSP 默认实现：manifest 里声明了
#     configstore@1.1，却没有服务实例 -> RenderEngine 查 ISurfaceFlingerConfigs 拿不到，
#     表现就是 S1G 那次 SurfaceFlinger 段错的原地复现。
#  4) media.omx@1.0 同理：manifest 声明了，包没装。
#  5) cas 我们不提供实现 —— 已从 rootdir/manifest.xml 移除（声明而不实现=客户端 NOT_FOUND）。

DEVICE_PATH := device/jiabo/z1

# 非 Treble 的 legacy 路径：vendor 内容落在 /system/vendor，不 imposing Treble 启动要求。
PRODUCT_SHIPPING_API_LEVEL := 19
PRODUCT_CHARACTERISTICS := nosdcard

# 内核 DT chosen/bootargs 必须给出 androidboot.hardware=z1，init 才会 import init.z1.rc
# （本启动链的 LK 不转发 boot.img 头的 cmdline，写 BOARD_KERNEL_CMDLINE 是无效功）。
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/rootdir/init.z1.rc:root/init.z1.rc \
    $(DEVICE_PATH)/rootdir/fstab.z1:root/fstab.z1 \
    $(DEVICE_PATH)/rootdir/ueventd.z1.rc:root/ueventd.z1.rc \
    $(DEVICE_PATH)/rootdir/init.z1.network.rc:root/init.z1.network.rc \
    $(DEVICE_PATH)/rootdir/z1_usb_setup.sh:system/bin/z1_usb_setup.sh

# 本体内核没开的 netfilter 项走模块；清单由本机 tools/z1/z1_a9_stage_modules.py 重算。
PRODUCT_COPY_FILES += $(foreach module,$(wildcard $(DEVICE_PATH)/network/modules/*.ko),\
    $(module):system/lib/modules/z1-network/$(notdir $(module)))

# 图形：LK 遗留的 simple-framebuffer 经 FB_SIMPLE 暴露 /dev/fb0（240x320 RGB565），
# gralloc.z1 用 ashmem 分配 + CPU 映射，hwcomposer.z1 走 HWC1 软合成；EGL/GLES 用 SwiftShader。
PRODUCT_PROPERTY_OVERRIDES += \
    ro.hardware.gralloc=z1 \
    ro.hardware.hwcomposer=z1 \
    ro.hardware.egl=swiftshader \
    ro.config.low_ram=true \
    ro.sf.lcd_density=160 \
    ro.vold.z1_fuse=true

PRODUCT_PACKAGES += \
    gralloc.z1 \
    hwcomposer.z1 \
    libEGL_swiftshader \
    libGLESv1_CM_swiftshader \
    libGLESv2_swiftshader \
    android.hardware.graphics.allocator@2.0-service \
    android.hardware.graphics.allocator@2.0-impl \
    android.hardware.graphics.mapper@2.0-impl \
    android.hardware.graphics.composer@2.1-service \
    android.hardware.graphics.composer@2.1-impl \
    android.hardware.configstore@1.1-service \
    android.hardware.configstore@1.1-impl \
    android.hardware.health@2.0-service.z1

# 音频：物理 MT6572 驱动未移植，先用 AOSP 静音软件实现；但 Pie 的客户端需要可发现的
# HIDL 工厂，不能像 stock 线那样整块停掉。Keymaster 为软件后端，不虚报 TEE。
PRODUCT_PACKAGES += \
    audio.primary.default \
    audio.r_submix.default \
    android.hardware.audio@2.0-service \
    android.hardware.audio@2.0-impl \
    android.hardware.audio.effect@2.0-impl \
    android.hardware.media.omx@1.0-service \
    android.hardware.media.omx@1.0-impl \
    android.hardware.keymaster@3.0-service \
    android.hardware.keymaster@3.0-impl \
    sdcard.z1_fuse

PRODUCT_COPY_FILES += \
    frameworks/av/services/audiopolicy/config/audio_policy_configuration_generic.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/primary_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/primary_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml

# ADB：mainline 上 FunctionFS 走 configfs(USB_F_FS=y)，ACM 只留给调试镜像用。
PRODUCT_PROPERTY_OVERRIDES += persist.sys.usb.config=adb

# 已知未完成（别当成已验证）：
#  - WiFi(mt6620) 的固件/HAL 未接；manifest 里也不再声明 cas。
#  - sepolicy 收紧要等首轮 permissive 的 avc denial；genfs_contexts 是否可从
#    BOARD_SEPOLICY_DIRS 生效，需在构建机的 Pie 构建规则上确认（本机没有源树，无法证实）。
#  - 全部结论未经上板。
