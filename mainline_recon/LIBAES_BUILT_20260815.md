# libaes.ko 编译完成 — BT 栈缺依赖补齐 (2026-08-15)

## 背景
BT 栈 6 个 .ko 上板 insmod 时 `bluetooth.ko` 报 `Unknown symbol aes_prepareenckey/aes_encrypt`。
根因: `CONFIG_CRYPTO_LIB_AES=m` 但 `lib/crypto/libaes.ko` 未编出。本次补编并归档。

## 产物
- **源码树路径**: `/home/<user>/claude/6572/fork_linux/lib/crypto/libaes.ko` (11656 bytes)
- **归档路径**: `/home/<user>/claude/6572/bridge_backups/products/products_bt_stack_20260814_204345/ko/libaes.ko`
- **SHA256**: `176153b068d6fc6d8a884403f2e15c1348dbbe1093007ec7132d52fff1f45369  ko/libaes.ko`
- 归档 SHA256SUMS 已生成 (全归档 21 文件, 见 `SHA256SUMS`)

## vermagic (已匹配)
```
vermagic:   7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8
```
与现有 `bluetooth.ko`/`ecc.ko` 等一致。编译命令**未设 LOCALVERSION 环境变量** (纪律: 避免消掉 `+`)。

## 导出符号 (bluetooth.ko 所需, 已确认提供)
```
T aes_encrypt
T aes_prepareenckey
```
`bluetooth.ko` 未定义符号 `U aes_encrypt / U aes_prepareenckey` 均由此 .ko 提供。

## 编译命令
```bash
cd /home/<user>/claude/6572/fork_linux
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- M=lib/crypto KBUILD_MODPOST_WARN=1 -j4 modules
```
- `.config` 已有 `CONFIG_CRYPTO_LIB_AES=m` (auto.conf 确认), 无需改配置。
- 同轮额外编出: `libarc4.ko / gf128mul.ko / libsha512.ko / libsha3.ko` (M=lib/crypto 整目录产物, 非 BT 依赖, 未入 BT 归档)。
- 未 `make clean` (整树此前已按 z1 vermagic 编过, 增量编即可)。

## BT insmod 依赖序 (modinfo depends 推导)
```
insmod ecc.ko            # depends: (无)  — core elliptic curve
insmod ecdh_generic.ko   # depends: ecc
insmod libaes.ko         # depends: (无)  — AES block cipher
insmod bluetooth.ko      # depends: ecdh_generic,libaes  ← 上一轮缺 libaes 即这里失败
insmod hci_vhci.ko       # depends: (无)
insmod hidp.ko           # depends: bluetooth
insmod uhid.ko           # depends: (无)
```
验证: 7 个 .ko 全部 vermagic = `7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8`, `modinfo_all.txt` 已补 libaes 条目。

## 状态
只编译 + 归档, 未刷写/碰硬件。未 git commit (主会话统一提交)。
