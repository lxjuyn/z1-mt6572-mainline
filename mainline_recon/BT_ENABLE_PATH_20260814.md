# Z1 (MT6572) CONNSYS 蓝牙 enable 完整路径 — 只读分析报告 (2026-08-14)

> 板子关机，纯只读源码分析 + 树内参考实现核对。联网工具 (WebSearch/WebFetch) 在当前 runtime
> 全部返回 provider 错误，无法用联网；但 **/tmp/connsys_build 树内就带一份"同硅片已验证可跑"的完整
> 参考实现**（README 明示 BT "working — pairing, HID keyboard, inbound reconnect"，验证机
> Prestigio PAP5500 DUO / Lenovo A369i 与 Z1 同为 MT6572 + MT6627N CONNSYS），其权威性高于任何
> 二手网页资料。本文档以树内代码 + 既有 mainline_recon 报告为主，联网参考结论摘录自
> `CONNSYS_MAINLINE_REFERENCE_20260814.md`（此前用 tavily+firecrawl 做过联网，URL 见该文）。

---

## 0. 结论一句话

**Z1 BT enable 的"底层"（BTIF 传输 + WMT/STP 控制面 + /dev/stpbt + WMT patch + launcher）已在树内
全部就绪并打进 ramdisk，且 BTIF/WMT/上电链已借 "func-on WiFi 偶发成功" 在板上跑通过一次；缺的
全部是"上层"：内核 BT/HID 模块没编（`CONFIG_BT_HCIVHCI/HIDP/UHID` 没开）、BT 用户态工具
（stpbt-vhci-bridge / stpbt-hci-test 等）没编、BlueZ 用户态（bluetoothd/bluetoothctl/dbus）没入
ramdisk。**把 BT 拉起来的剩余工作主要是编模块 + 打包，不涉及驱动源码改动。**

---

## 1. Z1 BT 拓扑（三层）

| 层 | 组件 | 节点/模块 | 状态 |
|---|---|---|---|
| 传输层 | BTIF（SoC 块 0x1100E000，APDMA 0x11000380/0x11000400） | `mtk_btif_drv.ko`，绑 dts `btif@1100e000` | ✅ 编好（z1 vermagic） |
| 控制面 | WMT/STP（复用 BT/GPS/FM/WMT 通道，含 WMT patch 下载） | `mtk_stp_wmt_soc.ko`，绑 dts `connectivity@18070000` | ✅ 编好 + 稳定性修复 A+C+D 已入树 |
| BT 通道 | `/dev/stpbt`（major 192，H4 帧直通） | `mtk_stp_bt_soc.ko` | ✅ 编好 |
| WiFi func ctrl | `/dev/wmtWifi`（major 155） | `mtk_wmt_wifi_soc.ko` | ✅ 编好 |
| wlan | `wlan_gen2.ko`（绑 `wifi@180f0000`） | + `cfg80211.ko` | ✅ 编好，func-on 偶发成功 |
| 固件守护 | `mtk_stp_launcher -m 3 -p /system/etc/firmware/` | 用户态静态 ARM 二进制 | ✅ 已编（522212 B） |
| BT 上层 | 内核 BT core + `/dev/vhci` + BlueZ | bluetooth.ko / hci_vhci.ko / bluetoothd 等 | 🔴 **全部缺** |

树内参考实现（`/tmp/connsys_build/README.md`）明示：**BTIF 同时承载 WiFi 和 BT 数据，BTIF 挂 =
WiFi 也起不来**。所以 Z1 "func-on WiFi 偶发成功" 已经间接证明 BTIF + WMT + chip 上电 + STP 通道
这条 BT 的必经底链是活的。

---

## 2. `/dev/stpbt` 实现细节（`/tmp/connsys_build/conn_soc/linux/pub/stp_chrdev_bt.c`）

- 驱动名 `mtk_stp_BT_chrdev`，`register_chrdev_region` **major 192**（L47 `BT_DEV_MAJOR 192`），
  `module_param(BT_major,...)` 可改。`class_create("stpbt")` + `device_create(...,"stpbt")`
  （`WMT_CREATE_NODE_DYNAMIC=1`）→ `/dev/stpbt` 自动建。
- **本 build 未编 `MTK_BT_HCI`**：内核态 HCI 设备（`hci_register_dev`，bus=`HCI_SDIO`，L457-493
  的 `mtk_bt_hci_init`）整个 `#ifdef MTK_BT_HCI`。out-of-tree 用的 `conn_soc/Kbuild` 只列了
  `linux/pub/stp_chrdev_bt.o`，没喂 `-DMTK_BT_HCI=1`（那个 flag 在 `linux/pub/Makefile` 里，但
  Kbuild 优先、子目录 Makefile 不被读；`.stp_chrdev_bt.o.cmd` 复核无该 define）。→ **走字符设备
  路径**，与已验证的参考实现一致。
- `BT_open`（L731）：`mtk_wcn_wmt_func_on(WMTDRV_TYPE_BT)`（BT PALDO on + WMT FUNC_CTRL 命令）→
  若 `mtk_wcn_stp_is_ready()` 不真 → `func_off` + 返回 -ENODEV；然后注册 STP 事件回调
  （`mtk_wcn_stp_register_event_cb(BT_TASK_INDX, BT_event_cb)`）与整芯复位消息回调
  （`bt_cdev_rst_cb`，rstflag=1/2 时 read/write 返回 -88/-99）。
  **注意：open 不负责上电芯片** —— 芯片上电 + WMT patch 下载由 launcher 的
  `WMT_IOCTL_LPBK_POWER_CTRL 1` 完成，STP 必须 already ready 才行。
- `BT_write`（L566）：`mtk_wcn_stp_send_data(buf, len, BT_TASK_INDX)` —— **H4 帧原样直通**
  （如 `01 03 0c 00` = HCI_Reset）。单缓冲 2048 B，count>2048 截断。
- `BT_read`（L620）：`mtk_wcn_stp_receive_data(i_buf, count, BT_TASK_INDX)`，阻塞等
  `wait_event(BT_wq, flag)`；O_NONBLOCK 返回 -EAGAIN。
- `BT_poll`（L540）：BT rx 队列非空 → POLLIN。
- ioctl：`COMBO_IOC_BT_HWVER`/`COMBO_IOCTL_BT_IC_HW_VER`/`COMBO_IOCTL_FW_ASSERT` 等（次要）。
- **单开**（无多 open 计数保护），阻塞 read 会把控制台搞挂 → 必须用非阻塞工具。
- close：`mtk_wcn_wmt_func_off(WMTDRV_TYPE_BT)`。

## 3. BT func-on 路径（`wmt_func.c` → `wmt_core.c` → `mtk_wcn_consys_hw.c`）

`mtk_wcn_wmt_func_on(WMTDRV_TYPE_BT)` →
- `opfunc_func_on`（`wmt_core.c` L1003）：若 WMT 未上电先 `opfunc_pwr_on`（`WMT_CTRL_HW_PWR_ON`
  → `mtk_wcn_consys_hw_reg_ctrl(1,...)`：VCN18/VCN28 LDO、`reset_control_reset(connsys)`、
  `pm_runtime_get_sync`、poll CONN MCU chipID=0x6572、**强制 AP2CONN_OSC_EN**（26M 授权，PSM 关键）；
  再 `wmt_core_stp_init`（STP open → BTIF MAND mode → hw_check 读 chip id → sw_init 下 patch））。
- `wmt_func_bt_on`（`wmt_func.c` L269）：`WMT_CTRL_SOC_PALDO_CTRL, BT_PALDO, PALDO_ON` →
  `mtk_wcn_consys_hw_bt_paldo_ctrl`（`mtk_wcn_consys_hw.c` L478：`regulator vcn33_bt` + pwrap
  `regmap 0x416[5]`）→ `wmt_core_func_ctrl_cmd(BT, TRUE)` 发 WMT FUNC_CTRL cmd/evt。
- 失败路径会 `WMT_CTRL_EVT_ERR_TRG_ASSERT` 触发整芯复位。

**STP 通道**（`stp_core.c`）：BT_TASK_INDX=0。`mtk_wcn_stp_send_data` 在 BTIF-MAND 模式用 4 字节
头（`0x80 | (type<<4) | len`），BTIF-FULL 模式用完整 STP 头（seq/ack/checksum + CRC16）——Z1 在
open 时设 MAND，`sw_init` 切 FULL。RX 解析后入 BT 队列 → `BT_read` 读走。

## 4. 数据路径全图（/dev/stpbt 怎么变 HCI 口）

```
BlueZ (bluetoothd/bluetoothctl)
   │ mgmt / HCI socket
   ▼
kernel BT core (bluetooth.ko) → hci0 (bus VIRTUAL)
   ▲                          │
   │ H4 packets (01/02/03/04) │
   ▼                          ▼
/dev/vhci (hci_vhci.ko, CONFIG_BT_HCIVHCI)
   ▲                          │
   └──── stpbt-vhci-bridge ───┘   ← 用户态泵：H4 重定帧 + PSM 治理 + MTK quirk shim
        ▲                   │
        │ read/write        │
        ▼                   ▼
   /dev/stpbt (major 192, mtk_stp_bt_soc.ko)
        ▲                   │
        │ STP BT_TASK_INDX  │  (mtk_wcn_stp_send_data/receive_data)
        ▼                   ▼
   STP core ── BTIF ── CONNSYS MCU (BT 固件在 ROM/或 ROM+WMT patch)
```

- `stpbt-vhci-bridge`（`/tmp/connsys_build/tools/stpbt-vhci-bridge.c`）是关键组件：open `/dev/stpbt`
  触发 BT func-on → open `/dev/vhci` 自动建 hci0 → 双向 H4 泵。它做三件额外的事：
  1. **H4 重定帧**（stpbt read 不保证包边界，/dev/vhci 要求一次一个完整 H4 包）；
  2. **MTK quirk shim**：丢弃 hw-error evt (0x04 0x10)、伪造 ReadLocalExtFeatures 空页
     （MT6572 fw 对 max_page>=2 响应会让 BT core 卡住）；
  3. **PSM 治理**：HCI cmd 未决 / 长 RF 操作（inquiry≈10s、page≈5s）/ 距上次流量<1s 时 hold PSM off，
     空闲才允许 30ms idle 睡眠；通过 `/proc/driver/wmt_dbg` 写 `0 1e`/`0 0`。

## 5. BlueZ 怎么接 — 关键结论：**不用 hciattach / hci_uart / btuart_ldisc**

- MT6572 CONNSYS 的 BT 在 **SoC 内部 BTIF 总线**上，不是 UART。`hciattach`、`hci_uart`（H4）、
  `btuart_ldisc`（N_MTKSTP, ttyMT2）都是给 **外置 combo 芯片（MT6620/6628 走 UART/SDIO）** 用的；
  postmarketOS/BPI-R2 参考里的 `6620_launcher -d /dev/ttyMT2` 也是 combo 时代。
- 同硅片参考实现（PAP5500）验证的接法就是 **`/dev/stpbt` + `stpbt-vhci-bridge` ↔ `/dev/vhci`**，
  由内核 BT core 注册 hci0，BlueZ 正常走 mgmt/HCI socket。README 明确此路 "working — pairing,
  HID keyboard, inbound reconnect"。
- 备选（未采用）：把 `stp_chrdev_bt.c` 编成 `-DMTK_BT_HCI=1`，由内核 `hci_register_dev`
  （bus=HCI_SDIO，`mtk_bt_hci_open` 内 func-on + `mtk_wcn_stp_set_bluez(1)`）直接注册 hci0，STP RX
  直通 `mtk_bt_hci_receive`。这条路少一个用户态进程，但 PSM 治理、H4 重定帧、quirk shim 都要搬进
  内核，**且未被本硅片验证**。建议维持字符设备 + bridge 路线。

## 6. 固件需求：**BT 不需要独立固件**

- CONSYS ROM 自带 BT 栈；`mt6572_82_patch_e1_{0,1}_hdr.bin`（launcher fallback 名
  `ROMv1_patch_{0,1}_hdr.bin`）是 BT/WiFi/GPS/FM **合一**的 WMT patch，芯片上电时由内核 WMT 驱动
  下载，launcher 只负责把 patch 文件名经 ioctl 喂给内核（`WMT_IOCTL_SET_PATCH_NUM/INFO`）。
- **没有 patch（launcher 无文件可喂 → patchNum=0）BT 照常工作**（裸 ROM），只有 WiFi RAM code
  会在 func-on 入口死（README 明示）。所以 BT-only 冒烟对 patch 文件的依赖是"launcher 必须在线应答
  `srh_patch`"，patch 文件本身可有可无。
- WiFi 额外需要 `/lib/firmware/WIFI_RAM_CODE_MT6582`（wlan_gen2 `request_firmware`，func-on 时下）。
- `/system/etc/firmware/WMT_SOC.cfg` 是**内核 insmod 时** `wmt_conf_read_file` 用 filp_open 绝对
  路径读的板级配置（缺失 → insmod 失败，上板实测过）。

## 7. 关键问题：Z1 BT 要工作需要什么 — 就绪 vs 缺口

### ✅ 已就绪（树内 / ramdisk 内已核实）

1. **内核** `7.0.0-rc7-z1+`（`CONFIG_LOCALVERSION="-z1"`），全部 .ko vermagic 精确匹配。
2. **dts**（`/home/<user>/claude/6572/fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts`）：
   - `btif@1100e000`（compatible `mediatek,btif`，**含空 `ranges;`**（BTIF DMA 修复①），
     `btif_tx@11000380`/`btif_rx@11000400` 子节点 + IRQ 52/41/42）；
   - `connectivity@18070000`（`mediatek,mt6572-consys`，pwrap-regmap、power-domains CONN、
     `resets=<&watchdog MT6572_TOPRGU_CONNSYS_SW_RST>`、vcn18/vcn28/vcn33_bt/vcn33_wifi supply、
     IRQ 122 CONN2AP / 124 CONN WDT）；
   - `wifi@180f0000`、`connsys@be000000` reserved-memory carveout（EMI 修复②，AP 侧共享 EMI 迁
     0xbe080000）。
3. **模块**（z1 vermagic，`/tmp/connsys_build` 构建树）：`mtk_btif_drv.ko` → `mtk_stp_wmt_soc.ko`
   （depends: mtk_btif_drv）→ `mtk_stp_bt_soc.ko`（depends: mtk_stp_wmt_soc）→ `mtk_wmt_wifi_soc.ko`
   → `wlan_gen2.ko`（depends: cfg80211,mtk_stp_wmt_soc,mtk_wmt_wifi_soc）+ `cfg80211.ko`。
4. **CONNSYS func-on 稳定性修复 A+C+D** 已落树（`wmt_core.c` 默认 BTIF hifType、`stp_exp.c`
   `g_stp_if_type=STP_BTIF_IF_TX`、脚本等 launcher 就绪）——launcher 缺席/迟到不再撞 "no hif info"。
5. **launcher 静态 ARM 二进制**已编（`/tmp/connsys_build/tools/launcher/mtk_stp_launcher`，522212 B，
   glibc 静态，非 bionic 版）。
6. **固件 blob**：`/home/<user>/claude/6572/out/firmware_backup/firmware/` 齐全
   （`WIFI_RAM_CODE_SOC`[MTKW magic, SOC 模式=Z1 用] / `mt6572_82_patch_e1_{0,1}_hdr.bin` /
   `WMT_SOC.cfg`）。
7. **当前 ramdisk**（`/home/<user>/claude/6572/out/ramdisk_mainline_connsys.gz`，512B MTK ROOTFS 头 +
   XZ，实测解出）已含：6 个 .ko + `mtk_stp_launcher` + `z1_modprobe_connsys.sh` +
   `z1_connsys_onboard_test.sh` + `lib/firmware/WIFI_RAM_CODE_MT6582` +
   `system/etc/firmware/{ROMv1_patch_0,1_hdr.bin,WMT_SOC.cfg}` + `etc/firmware/nvram/WIFI` + `iw`。
8. **节点 major 已修正**：`z1_modprobe_connsys.sh` 建 `/dev/stpwmt c 190`、`/dev/stpbt c 192`、
   `/dev/wmtWifi c 155`（与驱动源码一致）。
9. **func-on WiFi 已偶发成功** → BTIF/WMT/chip 上电/patch 下载这条 BT 底链在板上活过。

### 🔴 缺口（BT 实测前的全部拦路项）

1. **内核 BT/HID 模块没编、config 没开**（最大缺口）：
   - `fork_linux/.config`：`CONFIG_BT=m` 有，但 **`CONFIG_BT_HCIVHCI`（/dev/vhci 必需）、
     `CONFIG_BT_HIDP`、`CONFIG_UHID` 全没开**；`CONFIG_CRYPTO_LIB_AES=m`（配对加密，需先 insmod
     libaes.ko）。
   - 树内**没有任何 BT .ko**（只有 cfg80211.ko）：`bluetooth.ko`/`hci_vhci.ko`/`ecc.ko`/
     `ecdh_generic.ko`/`hidp.ko`/`uhid.ko` 全部缺，vermagic `7.0.0-rc7-z1+` 版本一个没有。
   - 需：`make ARCH=arm CROSS_COMPILE=... M=net/bluetooth modules`（bluetooth.ko/ecc.ko/
     ecdh_generic.ko）+ `M=drivers/bluetooth`（hci_vhci.ko）+ `M=net/bluetooth/hidp`（hidp.ko）
     + `M=drivers/misc/uhid`（uhid.ko）或改 config 后全量编；再打进 ramdisk `/root/connsys/bt/`。
2. **BT 用户态工具没编、没入 ramdisk**（全在 `/tmp/connsys_build/tools/` 只有 .c）：
   `stpbt-vhci-bridge`（hci0 创建 + PSM 治理，**必需**）、`stpbt-hci-test`（HCI_Reset 冒烟）、
   `hci-localver`、`btup-scan`（裸 HCI inquiry，可不要 bluetoothd）、`btif-lpbk-test`。
   构建命令（README）：
   ```
   arm-linux-gnueabihf-gcc -static -O2 -o stpbt-vhci-bridge stpbt-vhci-bridge.c
   arm-linux-gnueabihf-gcc -static -O2 -o stpbt-hci-test     stpbt-hci-test.c
   ```
3. **BlueZ 用户态没入 ramdisk**：bluetoothd / bluetoothctl / dbus-daemon + lib 未部署（参考实现装
   在 `/opt/bt`）。若 Z1 rootfs 是 Alpine，需 apk 装 bluez/dbus 或静态版。
4. **BT 未实测**：整个 BT 上层没在 Z1 板上跑过（缺 1-3 自然没测）。

### 🟡 注意项（不挡基本工作）

- **BT MAC**：CONNSYS BT 地址来自芯片/efuse 或 `BT_Addr` NVRAM（stock 在
  `/data/nvram/APCFG/APRDEB/BT_Addr`）。参考实现没配 BT NVRAM 也能配对，仅 MAC 可能随机/固定，
  不影响 HCI reset/inquiry/pairing。需要固定 MAC 再单独提取。
- `wlan_gen2` 的 NVRAM `/etc/firmware/nvram/WIFI` 本轮已入 ramdisk（随机 MAC 隐患解除，属 WiFi）。
- 整芯复位消息：BT open 后 `bt_cdev_rst_cb` 会把复位期间 read/write 返回 -88/-99，bridge 需容忍。

---

## 8. 上板验证命令（BT 实测，按序）

```sh
# 1) CONSYS 底链：insmod btif→wmt→bt→wifi→cfg80211→wlan_gen2 + 启动 launcher + 建节点
sh /root/connsys/z1_modprobe_connsys.sh          # 等 "WMT HIF info added" / "patch total num"
# 期望 dmesg: chip id 0x6572, patch total num = 2, STP ready

# 2) BT 冒烟（直接开 /dev/stpbt 发 HCI_Reset，不依赖 BlueZ）
/root/connsys/stpbt-hci-test
#   期望 stdout: "HCI RESET OK - radio answers, BT link is LIVE"（= BT func-on + 固件应答）
#   若失败: reboot 后重试，绝不 retry/rmmod（radio 会半死不活）
#   /root/connsys/hci-localver  可再看 HCI Read Local Version (04 0e evt)

# 3) 内核 BT/HID 模块（需先补齐缺口 1）
insmod bluetooth.ko 2>/dev/null || insmod libaes.ko && insmod bluetooth.ko
insmod ecc.ko ; insmod ecdh_generic.ko ; insmod hci_vhci.ko ; insmod hidp.ko ; insmod uhid.ko

# 4) PSM 前置（bridge 启动前）：quick-sleep off + PSM off
echo '1 0' > /proc/driver/wmt_dbg
echo '0 0' > /proc/driver/wmt_dbg
[ $(( $(devmem 0x10001800) & 0x10000 )) -ne 0 ] && echo "AP2CONN_OSC_EN ok"

# 5) hci0 上线（bridge 常驻，创建 hci0 并治理 PSM）
/root/connsys/stpbt-vhci-bridge > /tmp/bridge.log 2>&1 &
sleep 2
hciconfig hci0 up        # 或 bluetoothctl power on

# 6) 扫描验证（可不用 bluetoothd）
/root/connsys/btup-scan                     # 期望打印发现的 bdaddr
#    或全 BlueZ:
dbus-daemon --system --fork
bluetoothd -n &
bluetoothctl power on ; bluetoothctl scan on ; bluetoothctl pair <BDADDR>
```

**PSM 治理规则**（bridge 拥有运行时 PSM）：bridge 未跑时保持 `echo '0 0' > /proc/driver/wmt_dbg`
（PSM off）；bridge 会按 HCI 未决/RF 操作自动切。**WiFi 会话不要跑 bridge 的 PSM governor**
（它只看 BT 流量，对 WiFi 失明，WMT SLEEP 会杀固件）。

---

## 9. 风险与教训（直接抄参考实现的踩坑史）

1. **HCI 冒烟失败 = 必须 reboot，绝不 retry / 绝不 rmmod `mtk_stp_wmt_soc`**（rmmod 不可中断挂死）。
   崩溃自恢复（dmesg "evt err trigger assert fail, do chip reset to recovery"）后的 radio 是
   "半活"：HCI/inquiry 正常但 **outbound paging 静默死**（配对 120s 超时）。只有 reboot 恢复。
2. `/dev/stpbt` **单开 + 阻塞 read 会挂死控制台** → 只用非阻塞工具。
3. **BTIF 是 BT 和 WiFi 的公共底座**：BTIF 修不好 BT 不可能好；先单独验 BTIF（`btif-lpbk-test`/
   dmesg 无 Oops）再 func-on。
4. 上电时序：**先起 launcher，等两个 dmesg 标志，再 open /dev/stpbt**（STP 必须 already ready，
   `BT_open` 里 `stp_is_ready()` 不过会直接 -ENODEV）。
5. `CONNSYS_EMI_FW_PHY_BASE=0xf0080000` 未随 AP 侧 EMI 迁到 0xbe080000 —— 若共享 EMI 握手异常，
   属唯一未上板证实的隐患（`BTIF_EMI_FIX_REVIEW_20260814.md` §3.1）。
6. 本 runtime WebSearch/WebFetch 不可用（provider 400），联网结论以
   `CONNSYS_MAINLINE_REFERENCE_20260814.md`（tavily+firecrawl 当时可用的产出）为准；其核心结论
   与本树内参考实现一致：**MT6572 CONNSYS 无现成 mainline 先例，Z1 是自己点亮，且 BT 走
   /dev/stpbt+vhci 桥是本硅片唯一验证过的接法。**

---

## 附：关键文件路径索引

- BT 通道驱动：`/tmp/connsys_build/conn_soc/linux/pub/stp_chrdev_bt.c`（major 192，H4 直通）
- STP 通道注册：`/tmp/connsys_build/conn_soc/linux/pri/stp_btif.c`；STP 核心：`conn_soc/core/stp_core.c`
- WMT func-on：`conn_soc/core/wmt_func.c` / `wmt_core.c` / `wmt_ctrl.c` / `wmt_ic_soc.c`
- 平台/电源：`conn_soc/mt6572/mtk_wcn_consys_hw.c` / `wmt_plat_alps.c` / `plat_stub.c`
- BTIF：`/tmp/connsys_build/btif/mtk_btif.c`（PIO+APDMA，ENABLE_BTIF_TX_DMA=1）
- 参考工具：`/tmp/connsys_build/tools/{connsys-up.sh,bt-up.sh,stpbt-vhci-bridge.c,stpbt-hci-test.c,hci-localver.c,btup-scan.c,kbd-pair.md,S99bt}`
- launcher：`/tmp/connsys_build/tools/launcher/{stp_uart_launcher.c,wmt_ioctl.h}`
- 参考 README（硅片级验证状态）：`/tmp/connsys_build/README.md`
- Z1 dts：`/home/<user>/claude/6572/fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts`
- Z1 内核 config：`/home/<user>/claude/6572/fork_linux/.config`
- Z1 ramdisk 打包：`/home/<user>/claude/6572/tools/repack_ramdisk_connsys.sh`；当前 ramdisk
  `/home/<user>/claude/6572/out/ramdisk_mainline_connsys.gz`
- 上板脚本：`/home/<user>/claude/6572/scripts/z1_modprobe_connsys.sh`、`z1_connsys_onboard_test.sh`
- 固件源：`/home/<user>/claude/6572/out/firmware_backup/firmware/`
- 相关既有报告：`mainline_recon/{CONNSYS_FW_READINESS,CONNSYS_STABLE_FIX,CONNSYS_MAINLINE_REFERENCE,CONNSYS_MISSING_BUILT,BTIF_EMI_FIX_REVIEW}_20260814.md`
