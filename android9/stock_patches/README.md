# Stock S1 Android source changes

Selected patches relative to the pre-S1 workspace baselines. These are already applied in the local LOS16 tree. manifest.json records base/result hashes; these are not claimed to apply to arbitrary upstream revisions.

Build: `Z1_KERNEL_VARIANT=stock BUILD_JOBS=6 ./android9/build_z1.sh Home services systemimage ramdisk`.
Package the exact A7/factory kernel with tools/z1_make_stock_android9.py and apply tools/z1_stock_system_profile.py to a new raw system image. This is an experimental permissive diagnostic product; stock libbinder protocol7 must never be paired with Linux7 boot.

Native HIDL clients use local factories; Java hardware Binder services and remote codec/DRM/WiFi registrations are unavailable in S1. Stock battery uses measured power_supply sysfs. Software keymaster retains its actual security level; physical hardware acceleration remains unadapted.
