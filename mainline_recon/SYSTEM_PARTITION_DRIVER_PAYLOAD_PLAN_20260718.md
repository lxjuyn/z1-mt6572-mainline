# Z1 driver/firmware payload placement plan

Status: **OFFLINE PLAN / NO SYSTEM IMAGE MODIFIED**

## Capacity facts

- Factory boot partition image: 6,291,456 bytes (6 MiB).
- Current approved mainline boot image: 5,564,416 bytes.
- Boot slack: 727,040 bytes (710 KiB).
- Factory `system.img`: 943,718,400 bytes (900 MiB).
- `system.img` free ext4 blocks: 57,886 × 4096 = 237,101,056 bytes
  (approximately 226.1 MiB).

The boot image is already near its hard partition ceiling. It can hold small
built-in changes, but not the complete WiFi/BT stack plus firmware and user
tools.

## Placement policy

Keep in boot/initramfs:

- kernel and DTB;
- minimal USB/shell/init support;
- only drivers needed before block storage is mounted.

Place on system, after the mount path is implemented and tested:

- `/system/lib/modules/` — version-matched kernel modules;
- `/system/etc/firmware/` — WiFi/BT firmware and WMT configuration;
- `/system/bin/` — optional test/loader utilities.

The current mainline image boots with `root=/dev/ram`; it does not yet mount
the Android system partition automatically. Therefore system placement is not
usable merely by copying files into `system.img`. The initramfs must first:

1. identify the correct eMMC system block device;
2. mount it read-only for the first test;
3. verify filesystem and expected files;
4. load a module whose vermagic matches the running kernel;
5. use `request_firmware()` only after the mount is available.

## Safety rules

- Do not modify `1/system.img` in place.
- Preserve a byte-for-byte backup and SHA256 before any repack.
- First test a read-only mount and file listing only.
- Do not place a module in system until its source, kernel build, vermagic and
  undefined-symbol provenance are archived.
- Firmware must be selected by exact hash; names alone are insufficient.

The known SOC WiFi firmware is 160,464 bytes and fits either partition, but
the old vendor user-space loader is about 1.1 MiB and cannot fit in the
remaining boot slack. The complete old external MT6620 stack must not be
copied into system as if it were compatible; the integrated MT6572 path is
still being ported.

## Android removal plan after Linux acceptance

Yes, the Android payload can eventually be removed while retaining the Linux
payload, but this must be a **replacement image**, not an in-place deletion.
The original `1/system.img` remains the authoritative rollback image:

- size: `943,718,400` bytes (900 MiB);
- SHA-256: `0aad817b3f87e7b328378c0b147da1ea048a87f9aa2ecfa338178be7176c62f7`;
- scatter partition name: `android`;
- physical partition geometry must still be re-proven on the device before
  writing.

The current `out/system_alpine.img` is not yet a production replacement:

- it is `891,289,600` bytes (850 MiB), 50 MiB shorter than the partition;
- its root filesystem currently has empty module and firmware directories;
- `out/system_alpine.bak` is not byte-identical to it and is not a verified
  rollback.

The safe sequence is:

1. Keep `root=/dev/ram` and boot the known-good rescue image.
2. Prove Linux MMC enumeration and identify the 900 MiB `android` partition.
3. Mount that partition read-only with `noload`, list it, and unmount. Do not
   pivot or write during this gate.
4. Freeze/hash `1/system.img`, `1/boot.img`, the approved mainline boot image,
   scatter/partition metadata and every selected module/firmware file.
5. Generate a **new full-size 900 MiB ext4 Linux-only image** with a fresh
   label/UUID, version manifest, matching modules and selected firmware. Do
   not resize or overwrite either existing image during generation.
6. Run offline `e2fsck -fn`, `dumpe2fs`, `debugfs` and full-image SHA-256
   verification.
7. Flash only the `android`/system target, read back all 900 MiB and compare
   SHA-256. Do not use “format all” and do not touch preloader, NVRAM, MBR,
   EBR, PROTECT, SECCFG, userdata or cache.
8. Keep the rescue initramfs in the boot image. Only after the system image
   read-only manifest check passes should a separate boot candidate perform a
   `switch_root`/`switch_root`-equivalent pivot.

Until the MMC read-only mount gate and the three-cycle Linux acceptance test
pass, Android must remain intact. Removing it sooner would remove the easiest
on-device charging/rollback path without proving that Linux can access the
partition or load its modules.
