# Z1 CONNSYS WiFi func-on 成功记录 (2026-08-14)

> **里程碑**: Z1 (MT6572) mainline 内核上 WiFi 硬件栈完整打通 —— 从 insmod 到 func-on 到 wlan0 接口 + 真实 MAC。
> 这是 CONNSYS WiFi 在 mainline 上首次完整工作的实证。

## 成功证据 (boot log `z1_boot_mainline_20260814_124449.log`, 内核 7.0.0-rc7-z1+)

```
[  435.198067] glSetHifInfo:(INIT INFO)[WiFi/HIF] ChipID = 0x6572          ← CONN MCU 芯片识别
[  435.223224] kalFirmwareOpen:(INIT INFO)Open FW image done, 160464 bytes  ← WiFi 固件加载 (WIFI_RAM_CODE, 160464B)
[  436.004951] wlanUpdateNetworkAddress:(INIT INFO)Using host-supplied MAC address  ← NVRAM 生效
[  436.051050] wlanProbe:(INIT INFO)MAC address: <MAC>          ← Z1 真实出厂 MAC
[  436.071637] [MTK-WIFI] WIFI_write: WMT turn on WIFI success!             ← func-on 成功
lo     wlan0                                                               ← WiFi 接口出现
~ # cat /sys/class/net/wlan0/address
<MAC>
```

## 本次修复链 (全部生效)

| # | 修复 | git commit | 作用 |
|---|---|---|---|
| 1 | vermagic 固定 `7.0.0-rc7-z1+` | `a00567cbe` | .ko 与内核匹配，能 insmod |
| 2 | btif dts 加 `ranges;` | `e3198bbc3` | BTIF DMA base 不再 NULL，func-on 不 Oops |
| 3 | EMI base 0x80080000→0xbe080000 | `e3198bbc3` | 共享内存映射正确，固件握手成功 |
| 4 | 固件内置 (WIFI_RAM_CODE+patch+WMT_SOC.cfg) | `20b0fc40f` | WMT_init 不再因缺 cfg 失败 |
| 5 | NVRAM 内置 (stock 512B) | `c450d5d23` | wlan0 使用真实 MAC <MAC> |

## 模块加载全部 OK (busybox-safe 脚本)

```
OK: mtk_btif_drv.ko
OK: mtk_stp_wmt_soc.ko
OK: mtk_stp_bt_soc.ko
OK: mtk_wmt_wifi_soc.ko
OK: wlan_gen2.ko
launcher 启动 pid 72
```

## 已知待完善

1. **func-on 偶发不稳定**: 重启后第二次 func-on 卡在 `nicAllocateAdapterMemory: Allocating 12528` 后 (固件下载/握手时序敏感)。成功过一次即可复现路径,稳定性待上板调。
2. **`cannot get HIF clk_wifi_dma clock`**: HIF DMA 时钟缺失警告 (两次 func-on 都有),未阻止成功,但可能是偶发卡顿诱因之一。dts `wifi@180f0000` 可补 `clocks` 属性。
3. **shell 偶发卡死**: func-on 后 shell 偶发不响应命令 (busybox 管道问题),重启恢复。
4. **BT 未实测**: BT chrdev (major 192) 已建,func-on 未测 BT scan。

## 归档

- 完整 log: `bridge_backups/products/products_wifi_func_on_20260814_130450/z1_boot_mainline_20260814_124449.log`
- 证据: `.../wifi_func_on_evidence.txt` (grep 提取的成功行)
- 待刷镜像: `out/boot_mainline_z1_connsys_mod.img` (6270976B, 含全部修复)

## 下次上板测 scan

重启后 func-on 成功立即 (趁 shell 活着):
```
sh /root/connsys/z1_modprobe_connsys.sh
echo 1 > /dev/wmtWifi
# 立即 (成功 log 出现后):
iwlist wlan0 scan 2>&1 | head -30    # 或 iw dev wlan0 scan
ifconfig wlan0 up
```
