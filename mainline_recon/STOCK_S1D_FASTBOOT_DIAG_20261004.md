# Stock S1D: early init fatal diagnostics

Use the existing paired S1 system.img. Manual SPFlash Download Only: replace only BOOTIMG/bootimg with this boot.img; preserve all other partitions and userdata.

Exact factory/A7 3.4.67 kernel retained. Only ramdisk init executable changed. Stock early mount/seccomp/SELinux/restorecon/reexec print Z1STOCK-DIAG BEGIN/END. A fatal PID1 signal has a bounded 15-second window for the independent ACM logger before the original bootloader reboot. No fatal checks, policy loading or errors bypassed. Hardware resets may still interrupt this window.

Observed Oct 4: exact S1 ACM identity appeared, disappeared in the same second; preloader followed, then fastboot 0bb4:0c01. Fastboot product J72_Z1 confirmed. No device log payload was captured; boot-reason was empty. This does not identify the fatal root cause yet.

Build init PASS (28 seconds). Boot format, exact kernel identity, partition size and ramdisk-only-init change checks PASS. Device runtime unverified.

Capture: python3 tools/z1_capture_android9_acm.py --stock --seconds 1800 --wait 1800 --status --tombstones
Start capture before boot. Unplug USB for power-on, then connect after display lights up.

Candidate: /home/lxj/claude/6572/out/A9_STOCK_S1D_20261004
Archive: /home/lxj/claude/6572/bridge_backups/products/products_stock_s1d_final_20261004_004430
