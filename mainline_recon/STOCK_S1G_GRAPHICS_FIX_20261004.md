# Android 9 stock-kernel S1G image

## Runtime finding

The Oct 4 fastboot capture proves S1F passed SELinux policy load, `/init` restorecon and re-exec, the mmap sysctl compatibility path, MMC discovery, validated live `android/cache/usrdata` partition aliases, and `mount_all`. `surfaceflinger` then segfaulted at null after EGL initialized. Source and runtime ordering matched `RenderEngine::overrideUseContextPriorityFromConfig()` dereferencing a missing configstore 1.0 passthrough service. The stock product packaged 1.1, while HIDL lookup required the 1.0 library prefix.

## Changes

- Add a real `android.hardware.configstore@1.0-impl.z1stock` passthrough factory. It returns the 1.0 base interface of the existing 1.1 implementation.
- Guard missing/failed configstore calls in RenderEngine and retain its default context-priority behavior.
- On stock only, keep `wificond` on native Binder and do not request unavailable `/dev/hwbinder`; Wi-Fi HIDL hardware support is not claimed.
- On stock only, disable unsupported HIDL scan-offload lookup and report offload unavailable; the actual native-netlink scan path reports its real result.

## Validation

System image build PASS (final incremental build log: `stock_s1g_final_system_build_20261004.log`). Raw ext4 size 943,718,400 bytes matches fastboot system partition `0x38400000`. `e2fsck -fn` PASS. Pie init/service/classpath/ELF closure PASS: 59 services, 58 ELF roots, 315 closure members, 401 checked image files and 7 local factory exports. Source patch reverse checks PASS (24/24).

The new system has not yet been run on the device. Current testing boot keeps S1F diagnostic boot with a 15-second early-fatal capture window. SELinux remains permissive and graphics still use SwiftShader; reaching a stable desktop, GPU acceleration, sound and Wi-Fi require further device checks.

## Images

- Raw fastboot image: `out/A9_STOCK_S1G_20261004/system.img`
- Paired boot retained from the already flashed S1F: `out/A9_STOCK_S1G_20261004/boot.img`
- Archive: `bridge_backups/products/products_stock_s1g_final_20261004_012842/` (boot plus compressed system image and validation documents).

Only the `system` partition is changed in this step; preserve boot, userdata and all other partitions.
