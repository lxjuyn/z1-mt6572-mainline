# Stock S1 actual userspace Binder ABI audit

Read-only artifact audit; not device runtime proof. Exact snapshots/hashes: `out/android9_stock_s1_abi/PRODUCT_HASHES.json`. All copies checked unchanged mtime during copying; root must compare final packaged artifacts to these hashes.

## Confirmed ABI

- Actual system/lib/libbinder.so: BINDER_VERSION=0xc0046209 assembled at0x540ee/0x540f8; returned version compared to7 at0x5410e. IPCThreadState::talkWithDriver assembles BINDER_WRITE_READ=0xc0186201 at0x3d3d8/0x3d3de, so ioctl payload length24 bytes, not48.
- Actual system/bin/servicemanager force-Thumb disassembly: BINDER_VERSION at0x14e8, returned version cmp7 at0x14fe; WRITE_READ assembled at0x1640/0x1646 as0xc0186201. Further actual WRITE_READ callsites consistently use24 bytes.
- Actual libhwbinder.so also checks7 at0x159e8 and uses0xc0186201 at0x16584/0x16594. Protocol agreement DOES NOT add SG/multiple Binder contexts to stock kernel; this library is not a supported stock HIDL transport. S1 must avoid HIDL HAL activation until stock SG/context support is independently proven.
- These match the exact stock kernel evidence in STOCK_REUSE_BINDER_AUDIT_20261001.md §6: kernel flat offset0x3309d0 returns7,0x33087c/0x330880 constructs0xc0186201 and0x33088c checks24. Kernel SG support remains absent.

## Actual compile commands and consumers

`COMPILE_COMMANDS.txt` is ninja -t commands -s for actual existing libbinder ProcessState.o, servicemanager binder.o, and libandroid_runtime android_util_Binder.o; `APP_PROCESS_COMMAND.txt` records actual app_main.o. No builds launched by audit. `NINJA_FLAGS.txt` narrow exact module bindings confirm -DBINDER_IPC_32BIT=1 for libbinder, libhwbinder, servicemanager. libandroid_runtime/app_process are actual ARM32 ELF snapshots and call through libbinder; source contains no direct binder_size_t/binder_uintptr_t/write_read ioctl layout construction. RUNTIME_NEEDED.json records their actual dependencies. Soong Binder32bit=true alone was not used as proof.

## Kernel log reader ownership

First-stage init.cpp forks /z1_bootdiag --acm-stock only when /z1_stock_kernel exists. Helper uses /proc/kmsg O_NONBLOCK; its --logcat reader excludes kernel buffers. Other stock services must not consume /proc/kmsg.

Found incorrect product key logd.kernel=false: logd/main.cpp:441 reads ro.logd.kernel, and liblog/properties.c:439-455 has no unprefixed fallback for those flags. Root notified to correct to ro.logd.kernel=false. Existing ro.config.low_ram=true currently defaults klogd off via properties.c:478-484, but explicit correct property is required for intended stable ownership. logd.rc opening /proc/kmsg as inherited FD alone does not consume data: LogKlog is constructed/started only under klogd=true. /dev/kmsg write-only health/init/logger descriptors are not competing readers. Final build.prop gate remains root responsibility after rebuilding.

## Final staged follow-up

All5 snapshotted ELF hashes still match final staging (FINAL_STAGE_MATCH.json). Final build.prop now contains `ro.logd.kernel=false` and `ro.config.low_ram=true`; explicit reader ownership gap is corrected. Only staged logd.rc references /proc/kmsg, and inherited open is not read when its false property disables LogKlog. No other staged init service kernel reader was found. Runtime activation remains unverified.
