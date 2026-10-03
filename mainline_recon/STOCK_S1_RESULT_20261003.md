# Z1 Android9 stock S1 result

Independent diagnostic pair built with the exact factory/A7 3.4.67 kernel. No device flashed, no runtime success claimed.

Final bundle: `out/A9_STOCK_S1_20261003/`. Final archive: `bridge_backups/products/products_stock_s1_final_20261003_2021/`.

Boot SHA256: 20e0667b5483f5e6fb9bc7b8d5943578a024617e9df68e38fe5a5d5389493e01
System SHA256: b180922ee23510140dc452cca381e003df37951a20fb5ec449bf57bf65dadb40

Native, full Android, final incremental, final zram ramdisk builds PASS. Boot5320704B <=6291456B; rawsystem943718400B. Kernelbody SHA f9afaa2a38d64e5a6d6877ccb73ede2250b7efe6389fa60e66ee500cb885cc07 exactly equals A7/factory; no DTB.

Static actual-image validation: 59 services,58 ELF roots,315 ELF closure members,401 image files,21 classpath JARs and7 local HIDL factory exports. Ext4 read-only e2fsck PASS. Actual compiled Binder protocol7/write24 verified, final stage snapshots match.

Fixes: policy26/context compatibility; native local HAL transport; skip unsupported remote HIDL server/publication paths; stock health sysfs and guard missing capacity; keystore/vold direct KM3 and truthful software security level; stock framebuffer16/32bit boundedcopy+PAN; bounded KD_GRAPHICS takeover; early legacyUSB ACM; live dumchar partition aliases+EXT4/geometry+MMCregistration retries; Home wallpaper SecurityException; A7RAM-onlyzram.

19 partition hostchecks,5 diagnosticstream checks,24 stockbattery Java fixtures,pixel ASan/UBSan checks and source18patch reversecheck PASS. Build source is archived in stock-s1-sources.tar.gz.

Experimental limitations: SELinux permissive/allow_unknown; software SwiftShader rendering; oldMali/ION/coreblobABI unadapted; JavaHIDL and remote codec/DRM/WiFi services unavailable; real policy load,partition enumeration,displayPAN,KDmode,battery measurements,existingkeyblob compatibility and desktop stability still require this candidate's device logs.

Manual SPFlash DownloadOnly bootimg+android paired; preserve userdata and all other partitions. Capture tool now --stock matches17ef:7439 serialZ1STOCK20261003 only.

Generated system RC staging targets were restored after image auditing to avoid affecting later mainline builds. The rawimage's RC snapshot is archived independently.
