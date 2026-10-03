# Stock S1 Android9 startup audit — 2026-10-03

Scope: exact factory/A7 3.4.67 startup packaging, stock product/init selection, diagnostic policy26 entry, service startup closure and M7 ext4 format. No shared source edits, builds or flashing. This report does not claim device boot or policy load success.

## Result

The implemented boot packer passes a bounded temporary-output test using the stock ramdisk generated at 20:09. No confirmed boot-header/kernel/ROOTFS format defect was found. Two changes are recommended before the device attempt: remove the remaining explicit vndservicemanager start, and resolve stock partitions from the original kernel's live named partition table rather than assume the Linux7 partition numbers. The latter is an unresolved mount gate, not proof that p4/p5/p6 are wrong.

## Test evidence

Inputs: `out/fuquan_z1_review_20261001/boot.img`, `1/boot.img`, `android9/los16/out/target/product/z1/ramdisk.img`, `out/android9_stock_s1_diag/z1_bootdiag`, `out/android9_stock_s1_policy/sepolicy`.

`tools/z1_make_stock_android9.py` completed in a temporary directory, exit 0; generated image and validation JSON were read back and temporary files removed. Test image size 5,316,608 bytes (< 6,291,456 limit), SHA256 `e16318c35bf3fb0ba56eba86ea756e8ae908fd67b36ba14c39c13efaf146b703`. This is a test identity, not a released/archived flash candidate.

- Both A7 and factory parse successfully; complete 3,337,288-byte MTK kernel segments are byte identical. Kernel payload SHA256 `f9afaa2a38d64e5a6d6877ccb73ede2250b7efe6389fa60e66ee500cb885cc07`.
- A7 ROOTFS length field 1,385,764 equals actual compressed body; factory 831,327 likewise.
- Tags address is 0x10000100; page 2048, kernel 0x10008000, ramdisk 0x11000000, second/dt size zero. Packer preserves A7 first-page header except ramdisk size and digest, and appends no DTB (packer lines 33–48, 62–66, 125–144).
- New cpio contains 61 entries; exact static ARM init retained, `import /init.z1.rc` present, mt6572 ueventd alias equals z1 data, stock marker present. Actual init SHA256 `4d7a740da2cf60977f4b89b180c4c456f8f96e5fd596d0129c986cf479a4ffd6`.
- Policy26 SHA256 `c3ffbe20a780f61713b79cb00269a1b5dd8c6cb7ec8ceae6b142e913ba5613ca` is installed at `/sepolicy`. Full compiler/round-trip/context validation is recorded separately in STOCK_S1_POLICY_20261003.md. Packer's own check verifies the header/version only (lines 71–77); it does not independently prove the policy is loadable on the real kernel.

## Stock partition gate and safe schema

Stock fstab currently assumes mmcblk0p4 system, p5 cache, p6 data (`android9/device/jiabo/z1/stock/fstab.z1:2–4`). A7 fstab uses `/emmc@android`, `/emmc@cache`, `/emmc@usrdata`. Existing mainline MBR evidence and the 3.4.5 reference table do not independently establish the running factory 3.4.67 numbering. Do not copy the special `/emmc@` syntax into Pie fs_mgr.

The exact factory/A7 decompressed kernel `out/android9_stock_s1_a7/kernel-decompressed.bin` independently contains:

- Offset 0x849268: `dumchar_info`; 0x849318: `Part_Name\tSize\tStartAddr\tType\tMapTo`; 0x8493b0: `/dev/block/mmcblk0p%d`.
- Offset 0x8a4940: `partno:    start_sect   nr_sects  partition_name` and `emmc_p%d: %8.8x %8.8x "%s"`.

Therefore use `/proc/dumchar_info` first. Read five whitespace fields: name, hexadecimal size bytes, hexadecimal start address bytes, decimal type, mapped device. Accept unique names android/cache/usrdata, type 2, and a strict `/dev/block/mmcblk0pN` mapping. The source schema is `out/stock_reuse_binder_compile_20261001/mediatek/platform/mt6572/kernel/drivers/dum-char/dumchar.c:660–688`; this source is 3.4.5 and is only a schema explanation, with the actual 3.4.67 strings establishing availability.

Fallback `/proc/emmc` lines match `emmc_pN: HEXSTART HEXCOUNT "android|cache|usrdata"`; start/count are hexadecimal **512-byte sectors**, not bytes. Reference schema `.../mmc-host/sd.c:6871–6907`. It iterates real block partitions and resolves the table name by part number. Check unique names, nonzero count, actual block-node stat, `/sys/class/block/mmcblk0pN/dev`, and sysfs start/size against this row. If both interfaces provide entries, disagreeing identities should fail with a diagnostic rather than select one silently. PARTNAME in sysfs may be absent on MBR; it is optional extra evidence, not the only resolver.

Suggested dedicated aliases `/dev/block/z1-stock-system`, `/dev/block/z1-stock-cache`, `/dev/block/z1-stock-data`; create them during board `on fs` before mount_all, after all three entries validate. On failure leave aliases absent and log the failure. No formatting, numbering fallback, or filesystem-check repair. A helper exit failure alone does not cancel following init commands; absent aliases ensure mount_all reports failure rather than touching a guessed partition.

## Init and service chain

- BoardConfig.mk:5–10 selects Binder32 only for stock; device.mk:9–15 selects stock init/fstab/ueventd and hardware suffix properties; build_z1.sh:9–18 validates variant and lunches userdebug. Keep an isolated stock build identity to avoid mixing existing mainline system binaries.
- Stock board init (`stock/init.z1.rc:2–22`) mounts on fs, launches board diagnostic actions post-fs, starts relay and bounded graphics takeover post-fs-data. No mainline modules are imported. Forced board import (`packer:83–87`) avoids dependence on original ro.hardware value. ueventd.mt6572 alias (`packer:102`) covers expected vendor hardware name.
- `android9/los16/system/core/init/init.cpp:610–638` starts static stock diagnostic helper after /dev/sys/proc initialization and before SELinux. `init_first_stage.cpp:479–497` skips first-stage mounting on missing DT fstab; no DTB is intentionally compatible here.
- `init/selinux.cpp:91–98` honors marker only when ALLOW_PERMISSIVE_SELINUX is built in. `init/Android.bp:30,37–42` makes this true only for debuggable products. Current ramdisk default.prop contains ro.debuggable=1 and the production init contains marker string. Retain userdebug. A stock user build would ignore marker and report enforcing to zygote; all-permissive policy alone is not equivalent to the enforcement-state workaround.
- Generic `system/core/rootdir/init.rc:279–304` still triggers fs/post-fs/post-fs-data/zygote-start/boot; unencrypted or unsupported crypto starts zygote at 565–574. `class_start core/main/late_start` remains at 675–681. No generic trigger waiting for hwservicemanager.ready or vndservicemanager.ready was found. Native servicemanager explicit start is preserved at 321.
- Packer removes explicit hwservicemanager at line 87 but currently leaves `start vndservicemanager` from generic init.rc:323. Remove this in the stock transformed ramdisk too. With removed declarations it is merely a missing-service diagnostic; with declarations marked disabled it **would explicitly start the disabled service**, so this is necessary for the parent's updated disabled-RC scheme.
- Disabled only suppresses class_start, not explicit start or interface-triggered start. Verify final stock service closure includes no other explicit starts for disabled HIDL servers. Parent retains RCs but marks all HIDL manager/HAL declarations disabled in system raw; preserve native servicemanager and framework Binder services. Retained native libraries permit the separately implemented passthrough path; framework Java HIDL calls still require their specific compatibility handling.

## M7 ext4 format

Read-only dumpe2fs of `out/A9_LINUX7_M7_20261003/system.img`: clean ext4, 4096-byte blocks, 230400 blocks (900 MiB), features `ext_attr resize_inode dir_index filetype extent flex_bg sparse_super large_file huge_file uninit_bg dir_nlink extra_isize`. No metadata_csum, 64bit, encryption, casefold, orphan_file, or journal-replay requirement.

Reference 3.4 ext4 accepts the incompat and ro-compat bits present: `out/stock_reuse_binder_compile_20261001/kernel/fs/ext4/ext4.h:1449–1464`. Original factory system ext4 already uses extents/uninit_bg. There is no evidenced feature-bit reason to recreate M7 system. Actual factory config and a real mount remain unverified; this is a format audit, not proof of successful mount. Stock Binder32/HIDL modifications require a newly matched system image even though the M7 filesystem format itself is suitable.

## Remaining device evidence

Actual legacy android_usb ACM enumeration, policy26 load, live named partition rows and ext4 mounts, original Binder32 native servicemanager startup, zygote/framework survival, and software display are device gates. The early helper child has no automatic restart if it exits, and sys.usb.state=acm is currently set before real enumeration; host USB discovery/log contents are the evidence of connectivity, not that property. No independent watchdog failure is established by this audit.

## Follow-up: keystore/vold HIDL registry fatal (concurrent fix reviewed)

Initial source inspection found registry CHECKs before any passthrough fallback in `hardware/interfaces/keymaster/4.0/support/Keymaster.cpp` enumerateAvailableDevices and `system/security/keystore/keystore_main.cpp` initializeKeymasters. Stock transport deliberately returns no default hardware registry (`system/libhidl/transport/ServiceManagement.cpp:175–179`); requesting the manager interface through getService also cannot supply a registry. These were real fatal entry points if reached, not evidence from an actual stock device boot. Relevant support file is **Keymaster.cpp**, not keymaster_utils.cpp.

Concurrent edits by the owning agent are now visible and cover these entry points:

- `Keymaster.cpp:110–123`: stock gated KM3 default direct getService, retains real wrapper metadata, empty set and explicit log on lookup failure. Standard manager enumeration remains outside stock branch.
- `keystore_main.cpp:111–153`: stock direct local KM3 with real software fallback; retains both usable slots required by existing keystore logic. Existing SOFTWARE-as-default slot convention is retained, but underlying halVersion reports SOFTWARE rather than falsely manufacturing TEE security.
- `keystore_main.cpp:191–199`: skips stock WiFi HIDL registry publication, logs limitation, preserves native android.security.keystore Binder publication.

Do not substitute getPassthroughServiceManager into unchanged enumerateDevices: its `listByInterface` is LOG(FATAL) (`ServiceManagement.cpp:432–435`). Direct named device lookup avoids this.

Real KM3 default factory (`hardware/interfaces/keymaster/3.0/default/KeymasterDevice.cpp:67–75`) creates the existing software-only implementation if no legacy keystore hardware module exists. `device.mk:63–64` packages KM3 service+implementation (service RC must remain disabled in stock). No claim is made that the library loaded on-board; ELF closure/build results belong to the implementation owner. On KM3 load failure, vold's constructor can retain no device and consumers such as `KeyStorage.cpp:132–133` explicitly return false; this preserves failure rather than pretending encryption succeeded. Stock fstab enables no encryption or formatting, so this audit did not establish that those vold consumers will execute during the first stock boot.
