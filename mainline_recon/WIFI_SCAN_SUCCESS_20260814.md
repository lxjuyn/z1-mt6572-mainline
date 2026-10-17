# Z1 WiFi scan 成功 — 完整打通记录 (2026-08-14)

> **里程碑**: Z1 (MT6572) mainline Linux 上 WiFi 从硬件栈到主动扫描完整工作。
> `iw dev wlan0 scan` 成功发现 8 个真实 AP（含 SSID/信道/信号强度）。
> 这是 Z1 CONNSYS WiFi 在 mainline 上的完全可用实证。

## 成功证据 (boot log `z1_boot_mainline_20260814_202853.log`)

```
[92.104198] [MTK-WIFI] WIFI_open: WIFI_open: major 155 minor 0
[92.113188] [MTK-WIFI] WIFI_write: WIFI_write 1
[OK] func-on 指令已下发
[OK] wlan0 接口注册
无线接口: wlan0
mt-wifi 180f0000.wifi wlan0: Current addr: 20 72 0d 39 0d 1f ...
```

### iw scan 结果（8 个 AP）
```
BSS <MAC>(on wlan0)  freq 2462  signal -48 dBm  SSID 64KG-2
BSS <MAC>(on wlan0)  freq 2462  signal -39 dBm  SSID 1        (最强)
BSS <MAC>(on wlan0)  freq 2437  signal -71 dBm  SSID ChinaNet-NAhe-iTV
BSS <MAC>(on wlan0)  freq 2437  signal -71 dBm  SSID 毕府
BSS <MAC>(on wlan0)  freq 2437  signal -86 dBm  SSID 三楼
... 共 8 个 BSS
```

## 本次修复链（全部生效）
| # | 修复 | git | 作用 |
|---|---|---|---|
| 1 | vermagic 稳定 `-z1+` | `a00567cbe` | 模块可加载 |
| 2 | btif `ranges;` | `e3198bbc3` | BTIF DMA base 不 NULL |
| 3 | EMI base 0xbe080000 | `e3198bbc3` | 共享内存映射正确 |
| 4 | 固件内置 | `20b0fc40f` | WMT_init 不因缺 cfg 失败 |
| 5 | NVRAM 内置 | `c450d5d23` | MAC <MAC> |
| 6 | A: 删 wmt_exp u4InfoBit 回归 | `fe6b5726f` | func-on 不覆盖 hifType |
| 7 | C: 内核默认 BTIF | `fe6b5726f` | STP f_mode 不依赖 launcher 时序 |
| 8 | D: 脚本等 launcher | `fe6b5726f` | func-on 不撞竞态 |
| 9 | iw (nl80211) 工具 | `2ff48844a` | scan 可用（wlan_gen2 只支持 nl80211）|

## 状态
- **WiFi**: 硬件栈 + scan 完整工作 ✅
- **蓝牙**: 待测（底层 BTIF/WMT/STP 已就绪，需补 BT 内核模块 + 工具）
- 已知小问题: `dev_addr_check` WARNING（wlan0 up 时 netdev->dev_addr 全 0，不影响 scan/连接，后续修）

## 归档
- 完整 log: `bridge_backups/products/products_wifi_scan_success_20260814_203703/`
- 最终镜像: `out/boot_mainline_z1_connsys_mod.img` (5654528B)
