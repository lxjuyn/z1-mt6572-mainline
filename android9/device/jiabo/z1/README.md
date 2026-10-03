# Z1 LineageOS 16 staging device tree

This is a 32-bit, non-Treble skeleton. It contains no proprietary HALs,
graphics implementation, or flashable boot image. The factory LK ignores the
Android boot header command line, so `androidboot.hardware=z1` and other
required parameters must be added to the Z1 DTS `chosen/bootargs` before an
Android ramdisk can import `init.z1.rc`. The packaged DTB
`linux,initrd-end` must match the exact MTK ROOTFS body length each time the
ramdisk changes.

`TARGET_NO_KERNEL := true` leaves kernel packaging to this project's external
MTK packer while LOS builds `ramdisk.img` and `system.img`. The
standard LOS build does **not** create a flashable Z1 boot image. The
project's `build/mtkbootimg/mkbootimg --mtk 1` step must append the DTB to the
kernel and use a ramdisk with a 512-byte MTK ROOTFS header. After LOS builds
`ramdisk.img`, run `tools/z1_prepare_android9_ramdisk.py ramdisk.img
<new-output-dir>` to wrap it and patch a copy of the DTB's initrd end. Then
pass the resulting `ramdisk-mtk.img` and `mt6572-z1.dtb` to
`tools/z1_make_android9_bootimg.py`. The source DTS/DTB are left intact.
Check the 6 MiB limit and retain a known-good rollback image before any
device test.

From the workspace root, link this staging tree into a completed LOS16
checkout before running `lunch`:

```sh
mkdir -p android9/los16/device/jiabo
ln -s ../../../device/jiabo/z1 android9/los16/device/jiabo/z1
cd android9/los16
source build/envsetup.sh
lunch lineage_z1-userdebug
m ramdisk systemimage
```

The relative link resolves to `android9/device/jiabo/z1`; do not replace an
existing device tree. LOS16 product discovery follows symlinks under `device/`
and reads `AndroidProducts.mk`. The product inherits AOSP `aosp_base.mk`, the
Z1 device rules, and LineageOS `vendor/lineage/config/common.mk`; the latter
is expected in the LOS manifest. Measure the resulting `system.img` against
the 900 MiB partition before treating it as usable.

The fstab partition numbers follow the on-board Linux MBR/EBR enumeration,
not scatter names. The p6 table entry uses a sentinel size that Linux truncates
at the eMMC end. Measure its actual size and inspect existing filesystem state
before assigning a userdata image size or formatting it.

USB bringup uses ADB only through mainline configfs and FunctionFS. Board init
creates `/config/usb_gadget/g1` and mounts `/dev/usb-ffs/adb` with shell UID/GID
before adbd starts. A disabled oneshot service launched at boot polls for the
MUSB UDC for at most 20 seconds, detects its changing `musb-hdrc.N.auto` name,
then selects configfs and requests ADB. Failures log `[z1-usb]` to the kernel
log. Generic Pie init binds the UDC only after adbd writes descriptors and
sets `sys.usb.ffs.ready=1`. The default `persist.sys.usb.config=adb` keeps the
framework's default mode consistent with this setup; existing userdata may
retain a previously stored different value. Secure ADB authentication is
retained; accept the host key through the normal authorization flow.

This setup needs board enumeration and host authorization verification.
It uses an init SELinux context for the helper in permissive bringup; policy
must be audited before enforcing. Other USB modes, including MTP, have no
board implementation. The known Linux ACM baseline dynamically detects the
UDC in `out/init_v13_3_acm.sh`; its review records `musb-hdrc.2.auto`, while
older logs record `.3.auto`, so a fixed controller name is unsuitable.
See `mainline_recon/REVIEW_v13_3b_CORRECTNESS_20260829.md` section 1c.

Reference shapes: [LineageOS 16 hammerhead BoardConfig](https://github.com/LineageOS/android_device_lge_hammerhead/blob/lineage-16.0/BoardConfig.mk),
[Pie init mount stages](https://android.googlesource.com/platform/system/core/+/refs/tags/android-9.0.0_r45/rootdir/init.rc),
and `mainline_recon/P6_EOD_VERDICT_20260815.md` in this workspace.

## Symlink module discovery

Soong skips symlink directories. With this staged tree linked into LOS16, keep the real `android9/los16/device/jiabo/Android.mk` bridge that explicitly includes both `device/jiabo/z1/graphics/*/Android.mk` files. Without it, PRODUCT_PACKAGES may list gralloc.z1/hwcomposer.z1 while no compile or install rules exist. Confirm the final system actually contains both libraries.
