# CONNSYS 五模块重编 (vermagic 匹配当前内核) — 2026-08-14

## 原因
旧 CONNSYS .ko (products_connsys_modules_20260812_022121) 是旧内核编的，
vermagic=`7.0.0-rc7-gb8bf01d5ab5e-dirty`，当前内核 #90 (SND 清版)
=`7.0.0-rc7-g2cd361bd40c7-dirty`，insmod 报 vermagic 不匹配。

## 重编产物（不入 git，二进制太大）
新目录: `bridge_backups/products/products_connsys_modules_rebuilt_20260814_004806/`
vermagic 全 = `7.0.0-rc7-g2cd361bd40c7-dirty SMP mod_unload ARMv7 p2v8`

| 模块 | 大小 |
|---|---|
| btif/mtk_btif_drv.ko | 114000 |
| conn_soc/mtk_stp_wmt_soc.ko | 369712 |
| conn_soc/mtk_stp_bt_soc.ko | 12632 |
| conn_soc/mtk_wmt_wifi_soc.ko | 14376 |
| wlan/wlan_gen2.ko | 958920 |

SHA256 见该目录 SHA256SUMS。构建日志 /tmp/connsys_build2.log。

## 构建命令（在 /tmp 复制源码编，未动原归档）
```
make -C fork_linux ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
  M=/tmp/connsys_build EXTRA_SYMS=fork_linux/Module.symvers \
  KBUILD_MODPOST_WARN=1 modules
```
注意：modpost 自动读根 Module.symvers，**不要再传 KBUILD_EXTRA_SYMBOLS=同一文件**，
否则 `exported twice` 报错 (net/nfc、net/qrtr)。

## 配套改动
- `scripts/z1_modprobe_connsys.sh`: 去掉 tee/lsmod（busybox 无），改 echo 重定向 + cat /proc/modules
- `scripts/repack_ramdisk_connsys.sh`: MODS_SRC 指向重编目录（支持 Z1_MODS_SRC 覆盖）
- `arch/arm/boot/dts/mediatek/mt6572-z1.dts`: initrd-end 同步重打包 body 长 (0x8428d6a2)

## 重打包产物
- `out/ramdisk_mainline_connsys.gz` (1628322B, 含 5 .ko + 修好的加载脚本)
- `out/boot_mainline_z1_connsys_mod.img` (5439488B ≤ 6MB, validate VALID, SHA1 header 匹配)

## 上板验证（待做）
刷 boot_mainline_z1_connsys_mod.img → insmod 5 .ko 应无 vermagic 错误。
