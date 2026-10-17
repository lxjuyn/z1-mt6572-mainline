# BT 栈编译报告 (2026-08-14) — 内核 BT 模块 + 用户态工具 全编通

> 状态: **全部编译完成 + 归档完成**。板子未碰、内核源码未改(仅 .config 开 3 个 CONFIG)。待上板验证。
> 归档: `/home/<user>/claude/6572/bridge_backups/products/products_bt_stack_20260814_204345/`

## 1. 产物清单

### 内核 BT 模块 (6 个 .ko, 全部 vermagic `7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8`)

| 模块 | 大小 | depends | 作用 |
|---|---|---|---|
| `ko/bluetooth.ko` | 641676 | `ecdh_generic,libaes` | BT 协议栈核心 (HCI/L2CAP/SMP) |
| `ko/ecc.ko` | 36876 | (无) | ECC 椭圆曲线库 (配对用, crypto 依赖) |
| `ko/ecdh_generic.ko` | 9304 | `ecc` | ECDH 算法 (SMP 配对 key exchange) |
| `ko/hci_vhci.ko` | 14860 | (无) | **VHCI 虚拟 HCI** → 建 `/dev/vhci` + hci0 (vhci 桥必需) |
| `ko/hidp.ko` | 26468 | `bluetooth` | HIDP 协议 (蓝牙键盘/鼠标) |
| `ko/uhid.ko` | 13268 | (无) | 用户态 HID 驱动 (uinput 类 HID 透传) |

### BT 用户态工具 (5 个, 全部 ELF 32-bit ARM 静态链接, ~450KB)

| 工具 | 大小 | 作用 |
|---|---|---|
| `tools/stpbt-vhci-bridge` | 453320 | **必需**。`/dev/stpbt`(H4) ↔ `/dev/vhci` 双向泵 + H4 重定帧 + WMT PSM 治理 + MTK quirk shim; open `/dev/stpbt` 触发 BT func-on |
| `tools/stpbt-hci-test` | 448296 | HCI_Reset 冒烟: 直接开 `/dev/stpbt` 发 `01 03 0c 00`, 等 Command Complete |
| `tools/hci-localver` | 448652 | HCI Read Local Version (`01 01 10 00`), 打印 `04 0e` evt 内容 |
| `tools/btup-scan` | 448460 | 裸 HCI inquiry scan: 内核 socket+ioctl (HCIDEVUP/HCIINQUIRY), 免 BlueZ 打 bdaddr |
| `tools/btif-lpbk-test` | 448932 | BTIF 块 loopback 自检 (open→lpbk on→写读比对), 不涉 CONSYS |

源码 (.c) 也一并归档进 `tools/`。日志: `logs/modinfo_all.txt` / `logs/tools_file.txt` / `logs/olddefconfig.log`。

## 2. .config 改动 (已备份, vermagic 不变)

原 `.config` 已有 `CONFIG_BT=m / CONFIG_CRYPTO_ECDH=m / CONFIG_CRYPTO_LIB_AES=m / CONFIG_BT_BREDR=y / CONFIG_BT_LE=y`,
**缺 hci_vhci / hidp / uhid**。用 `scripts/config` 补开 3 项:

```
./scripts/config -m BT_HCIVHCI -m UHID -m BT_HIDP
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- olddefconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- syncconfig   # 刷新 auto.conf
```

- **备份**: `bridge_backups/dot.config_pre_bt_20260814_204113.bak` + `auto.conf_pre_bt_*` + `kernel.release_pre_bt_*`
- **vermagic 确认不变**: `include/config/kernel.release` 仍 `7.0.0-rc7-z1+`。`CONFIG_LOCALVERSION_AUTO` 未设(=n), `CONFIG_LOCALVERSION="-z1"` 未动。**不设 LOCALVERSION 就不改 vermagic, 已实测确认**。
- 注意: 本树 mainline 7.x 中 `uhid` 在 `drivers/hid/`(不是任务描述的 `drivers/misc/uhid`), `ecdh` 是单文件 `crypto/ecdh.c`(不是目录)。`CONFIG_CRYPTO_ECC` 被 `CRYPTO_ECDH` select 自动 =m, 无需手开。

## 3. 编译命令 (全部成功)

```
cd /home/<user>/claude/6572/fork_linux
# BT 核心 (自动下沉编出 hidp.ko)
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- M=net/bluetooth KBUILD_MODPOST_WARN=1 -j4 modules   # 13.8s
# crypto ECC/ECDH (in-tree 目标, crypto/ 无 ecdh 目录)
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- crypto/ecc.ko crypto/ecdh_generic.ko                # 6.8s
# hci_vhci (drivers/bluetooth, 顺带编出 hci_uart/btmrvl/btbcm)
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- M=drivers/bluetooth KBUILD_MODPOST_WARN=1 -j4 modules # 6.1s
# uhid
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- drivers/hid/uhid.ko                                   # 3.3s

# BT 用户态工具 (源码 /tmp/connsys_build/tools/*.c, 只需标准头)
cd /tmp/connsys_build/tools
arm-linux-gnueabihf-gcc -static -O2 -I /tmp/connsys_build/ -o stpbt-vhci-bridge stpbt-vhci-bridge.c
arm-linux-gnueabihf-gcc -static -O2 -I /tmp/connsys_build/ -o stpbt-hci-test stpbt-hci-test.c
arm-linux-gnueabihf-gcc -static -O2 -I /tmp/connsys_build/ -o hci-localver hci-localver.c
arm-linux-gnueabihf-gcc -static -O2 -I /tmp/connsys_build/ -o btup-scan btup-scan.c
arm-linux-gnueabihf-gcc -static -O2 -I /tmp/connsys_build/ -o btif-lpbk-test btif-lpbk-test.c
```

modpost 的 undefined symbol warnings (`hci_suspend_dev`/`hci_resume_dev`/cfg80211/... 等) 是外编 M= 没喂 vmlinux
符号表所致, insmod 时由已加载的 bluetooth.ko / 内核符号表解析, 非 .ko 生成障碍。`hci_vhci.ko` modinfo 无
depends 字段同因 (bluetooth.ko 也外编), **insmod 顺序必须 bluetooth 先于 hci_vhci**。

## 4. 蓝牙 enable 上板步骤 (insmod 顺序 + vhci 桥 + hciconfig)

> 对齐 BT_ENABLE_PATH_20260814.md §8。既有 CONNSYS 底链模块均 z1 vermagic, 在 `/tmp/connsys_build/`
> (conn_soc/ 与 btif/) 和归档 `products_connsys_stable_20260814_161743/`。

```sh
# 1) CONSYS 底链 (既有, 已就绪): btif→wmt→bt→wifi→cfg80211→wlan_gen2 + launcher + 建 /dev/stpbt
sh /root/connsys/z1_modprobe_connsys.sh          # 等 "WMT HIF info added" / "patch total num = 2"
# 期望 dmesg: chip id 0x6572, patch total num = 2, STP ready

# 2) BT 冒烟 (免内核 BT, 直接开 /dev/stpbt 发 HCI_Reset)
/root/connsys/stpbt-hci-test                     # 期望 "HCI RESET OK - radio answers, BT link is LIVE"
/root/connsys/hci-localver                       # 可再看 HCI Read Local Version
# 失败: reboot 后重试, 绝不 retry/rmmod mtk_stp_wmt_soc (radio 半死不活, rmmod 不可中断挂死)

# 3) 内核 BT/HID 模块 (新编, 按依赖序; 顺序不可错)
insmod ko/ecc.ko
insmod ko/ecdh_generic.ko        # depends: ecc
insmod ko/bluetooth.ko           # depends: ecdh_generic, libaes (libaes=CONFIG_CRYPTO_LIB_AES=m, 若未编则先 insmod libaes.ko)
insmod ko/hci_vhci.ko            # 建 /dev/vhci (无 depends 字段但符号来自 bluetooth.ko, 必须后 insmod)
insmod ko/hidp.ko                # depends: bluetooth
insmod ko/uhid.ko                # 可选 (用户态 HID)

# 4) PSM 前置 (bridge 启动前): quick-sleep off + PSM off
echo '1 0' > /proc/driver/wmt_dbg
echo '0 0' > /proc/driver/wmt_dbg
[ $(( $(devmem 0x10001800) & 0x10000 )) -ne 0 ] && echo "AP2CONN_OSC_EN ok"

# 5) hci0 上线 (bridge 常驻, open /dev/stpbt 触发 func-on + open /dev/vhci 建 hci0)
/root/connsys/stpbt-vhci-bridge > /tmp/bridge.log 2>&1 &
sleep 2
hciconfig hci0 up                # 或 bluetoothctl power on

# 6) 扫描验证 (可不用 bluetoothd)
/root/connsys/btup-scan          # 期望打印发现的 bdaddr
# 或全 BlueZ: dbus-daemon --system --fork; bluetoothd -n &; bluetoothctl scan on
```

## 5. 验证记录

- 6 个 .ko: `modinfo` vermagic 全 = `7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8` (见 logs/modinfo_all.txt)
- 5 个工具: `file` 全 = `ELF 32-bit LSB executable, ARM, EABI5, statically linked` (见 logs/tools_file.txt)
- 既有 CONNSYS 模块 (mtk_btif_drv / mtk_stp_wmt_soc / mtk_stp_bt_soc) vermagic 同为 `7.0.0-rc7-z1+`, 无混版本风险
- 遗留: 内核源码未改 (只改了 .config 并已备份); 未 git commit (主会话统一提交); 板子未碰
