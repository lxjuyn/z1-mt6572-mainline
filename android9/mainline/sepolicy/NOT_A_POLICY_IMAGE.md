# 这份 384 KB 的 `sepolicy` 不是内核策略镜像（别投进 precompiled_sepolicy）

来源：私有仓 z1-android9 release Linux7-20261006-r2 的 boot.img ramdisk 里的 `/sepolicy`。
sha256 `e01a4c589f1363bbaf0b6149be60fe81a6dd30af101f8215eb34b8eca4674f9c`，384,652 B。
本机核验（2026-10-18）：头 8 字节 = magic `0xF97CFF8C` + version `8`，且全文件不含
SELINUX_MAGIC `0x45524f53`、不含任何 `u:object_r:` 串 —— 这是 **libselinux 编译后的
file_contexts 二进制**（`sefcontext_compile` 的格式），不是 secilc 产出的策略镜像。

内核这侧接受的是 version 15..35（`security/selinux/include/security.h` 的
POLICYDB_VERSION_BASE..POLICYDB_VERSION_NEVERAUDIT），A9 的真件是 28/30，所以**只要拿到真件就能载入**；
但把这个 file_contexts 喂给 `/sys/fs/selinux/load` 只会被拒（板上表现为
`RESULT_STEP_selinux=FAIL(load 被拒…)`），比老实的 NOPOLICY 更坏。

真件的正确来源：r2 那份 `system.raw.img` 里的 `/vendor/etc/selinux/precompiled_sepolicy`
（与同一套 init/策略同源），到货后由 adapter 的魔数+版本闸门验过再投。
`tools/z1/z1_a9_mainline_system.py` 现在会拒绝任何 magic/版本不合格的策略输入件。
