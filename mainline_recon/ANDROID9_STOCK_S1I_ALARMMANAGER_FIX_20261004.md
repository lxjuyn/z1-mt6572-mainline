# Z1 stock-kernel Android 9 S1I

## Fixes included

S1I retains S1H's two runtime-derived changes: defer BatteryService's optional Settings-based discharge diagnostics until system providers are installed, and suppress audioserver's two restart hooks for the unavailable remote HIDL audio service while retaining local audioserver.

This new revision also fixes the next observed fatal startup error. S1G/S1H boot logs repeatedly showed AlarmManagerService aborting SystemServer because the original 3.4.67 timerfd rejects CLOCK_BOOTTIME with EINVAL. In the framework JNI, only that exact failure now retries the same BOOTTIME time base with CLOCK_BOOTTIME_ALARM. This counts suspend time correctly; it can wake the device for alarms that normally would not wake it and may use more battery. SystemServer is explicitly given CAP_WAKE_ALARM by the Android 9 ZygoteInit code. The fallback error path reports the actual clock that failed.

The S1F boot.img remains byte-for-byte identical and carries the original Z1 3.4.67 kernel. SP Flash Tool only needs this system.img in ANDROID. Use Download Only and preserve userdata.

## Checks

- `libandroid_servers.so` built successfully for ARM/Cortex-A7; installed image bytes match its compiled output.
- A host C++ fixture extracts and executes the actual JNI creation loop with mocked timerfd syscalls. Original success, EINVAL fallback, unrelated errors, fallback errors, descriptor cleanup and actual-clock diagnostics all pass.
- ext4 `e2fsck` is clean. Init/service/ELF/JAR dependency closure passes with 59 services and 58 executable roots.
- Runtime on this exact S1I revision has not been tested. The next serial-log boot is needed to check timerfd_settime and any later boot stages.

The fallback changes the power behavior of non-wakeup alarms. The minimal kernel-side upstream fix is one line accepting CLOCK_BOOTTIME in `fs/timerfd.c`; this workspace does not contain the exact Z1 3.4.67 kernel source to build that kernel fix safely.
