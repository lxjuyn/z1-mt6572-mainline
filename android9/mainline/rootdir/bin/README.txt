把 Android 9(arm32) 的 `init` 与 `ueventd` 两个 ELF 放这里（文件名就叫 init、ueventd），
z1_a9_mainline_system.py 会把它们投进 system 的 <P>/bin，交棒链就不再依赖外部 ramdisk。
要求：EM_ARM(0x28) 的动态可执行文件，linker 为 /system/bin/linker；
从任意一份 arm32 Android 9(LOS 16.0 / AOSP 9 aosp_arm) 的 system.img 里取即可。
校验方式（本机）：readelf -h <文件> | grep Machine   -> 应为 "ARM"
