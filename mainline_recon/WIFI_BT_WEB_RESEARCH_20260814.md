# Z1 (MT6572) CONNSYS WiFi/BT bring-up 联网调研报告 (2026-08-14)

> 本报告 = 联网调研（tavily/firecrawl）+ 对照 `/tmp/connsys_build` 移植树源码级核对（只读）。
> 结论核心: **func-on "偶发成功"不是玄学, 是"launcher 的 LPBK 整片 power-on 线程" 与 "echo 1 > /dev/wmtWifi 的 WIFI func-on" 之间的竞态 + STP f_mode/HIF_CONF 状态残留 + 固件文件路径必须是 kernel filp_open 可见的绝对路径**。下面给出正确时序、每步的源码依据和 URL。

---

## 0. 一句话结论（给主会话）

Z1 的 CONNSYS（MT6572）func-on 正确流程是 **三段, 严格先后**:

1. **insmod** `mtk_btif_drv` → `mtk_stp_wmt_soc` → `mtk_wmt_wifi_soc` → `wlan_gen2`（造出 `/dev/stpwmt`、`/dev/stpbt`(192)、`/dev/wmtWifi`）
2. **起 launcher**: `stp_uart_launcher -p <firmware目录> &` —— 它查 chip id(0x6572)、`WMT_IOCTL_SET_STP_MODE`=BTIF_FULL、然后 pwr_on 线程发 `WMT_IOCTL_LPBK_POWER_CTRL=1` → 内核整片 power-on（hw_check→ROM patch 下载→STP ready）
3. **等 launcher 完成整片 power-on 之后**, 才 `echo 1 > /dev/wmtWifi`（WIFI func-on, 此时只下载 `WIFI_RAM_CODE_MT6582`、出 wlan0）

**当前"偶发成功"的最可能根因**（按嫌疑排序）:
- (A) `echo 1 > /dev/wmtWifi` 抢在 launcher pwr_on 线程完成前 → WIFI func-on 自己触发整片 power-on, 与 launcher 的 LPBK power-on 并发跑 `wmt_core_stp_init()` → STP f_mode 被写乱 / hw_check Tx 失败。内核 WMT 状态机**没有串行化互斥锁**, 双线程并发=不确定。
- (B) 移植代码已修过一个**正主 bug**（`wmt_exp.c` 注释白纸黑字）: FUNC_ON op 若带 `WMT_OP_HIF_BIT`, `opfunc_hif_conf()` 会把 launcher 写的 hifType=BTIF(2) 覆写掉 → `wmt_core_stp_init()` 跳过 `MTKSTP_BTIF_MAND_MODE` → f_mode 停留 0 → `mtk_wcn_stp_send_data()` 返回 0 → hw_check Tx REG_CMD 失败。**请确认当前上板的 .ko 里这个 fix 真的编进去了**（注释在 `conn_soc/linux/pri/wmt_exp.c:104-116`）。
- (C) 固件路径: 内核 WMT 用 `filp_open()`（非 request_firmware）读 `WMT_SOC.cfg` 和 ROMv1_patch, 路径是 **绝对路径且必须在 Z1 rootfs 真实存在**。`WMT_SOC.cfg` 默认硬编码 `/system/etc/firmware/WMT_SOC.cfg`（`wmt_conf.h:33-34`）——Alpine 上没有这个目录就是静默失败。ROMv1_patch 的路径由 launcher 的 `-p` 目录 + `WMT_IOCTL_SET_PATCH_INFO` 传给内核, 内核再 filp_open 那个路径, 所以 `-p` 目录必须是设备上的真实目录（如 `/etc/firmware`）。
- (D) EMI 别名窗口风险（上板无法离线排除）: `CONSYS_EMI_FW_PHY_BASE=0xf0080000` 是 CONN MCU 侧共享 EMI 视图, AP 侧已迁到 `0xbe080000`, FW 侧没动。若 MT6572 的 0xf0000000 别名窗口固定落在旧 0x80080000, 固件读写共享 EMI 打在普通 RAM 上 → 握手/下载偶发失败。这是 `BTIF_EMI_FIX_REVIEW_20260814.md` 唯一未闭合的隐患, 与"偶发"高度吻合。

---

## 1. 关键架构: CONNSYS vs 独立 combo 芯片 (MT6620/6625/6628/6630)

| 维度 | CONNSYS (MT6572/6582/6589/6592/8127/6580/6752) | 独立 combo (MT6620/6625/6628/6630/6630) |
|---|---|---|
| 芯片位置 | 集成在 SoC 内 (CONN MCU), 无外置 UART/SDIO 引脚 | 独立芯片, 走 UART (或 SDIO) 与 AP 通信 |
| AP↔CONN 总线 | **BTIF** (内部串行总线, 0x1100e000) + WFDMA/EMI 共享内存 | UART (ttyMTx) 或 SDIO |
| WMT launcher 模式 | `STP_BTIF_FULL` (0x03) | `STP_UART_FULL` (0x01) / `STP_SDIO` |
| ROM patch 下载 | kernel 经 `WMT_CTRL_GET_PATCH` 读文件 (filp_open) 再走 BTIF 下发; launcher 只告诉内核 patch 文件名/路径 | launcher 自己经 /dev/stpwmt 逐片下发 patch 字节 |
| WiFi 固件 | wlan_gen2 `request_firmware("WIFI_RAM_CODE_MT6582")` | 同, 但 `WIFI_RAM_CODE` 命名不同 |
| 电源序列 | PMIC(MT6323)+CONNSYS clk+reset, 平台层 `wmt_plat_alps.c` | 外部 LDO + 使能脚 |

**判据（源码铁证）**: launcher `stp_uart_launcher.c` 里
```c
if((0x6735==chipId)||(0x6752==chipId)||(0x6582==chipId)||(0x6592==chipId)||
   (0x6572==chipId)||(0x6571==chipId)||(0x8127==chipId)||(0x8163==chipId)||(0x6580==chipId)) {
    ALOGI("run SOC chip flow\n");
    gStpMode = STP_BTIF_FULL;   /* CONNSYS 走 BTIF */
```
对照同文件 combo 分支是 UART/SDIO。所以 Z1 必须走 SOC 流。MT6572 的 ChipID=0x6572 在 `wmt_core_hw_check()` 的 switch 里明确映射到 `wmt_ic_ops_soc`。

---

## 2. 正确 func-on 时序（launcher + kernel 完整链路，源码级）

### 2.1 launcher 侧 (stp_uart_launcher.c main, SOC 流)

顺序执行, **顺序就是时序**:
1. `open("/dev/stpwmt")` — 失败则 usleep 300ms 重试（`/dev/stpwmt` 是 STP/WMT 命令通道, 不是 UART）— `launcher:1406`
2. `query_chip_id()` → 先查 property(Alpine 桩头无 Android property → 返回 0 走 ioctl) → `ioctl(WMT_IOCTL_WMT_QUERY_CHIPID)` 读内核 chip id = 0x6572
3. SOC 流分支: `gStpMode = STP_BTIF_FULL`
4. `ioctl(WMT_IOCTL_SET_PATCH_NAME, gPatchName)` — 默认 patch 名(空)
5. **`ioctl(WMT_IOCTL_SET_STP_MODE, (gFmMode&0x0F)<<4 | (gStpMode&0x0F))`** → 内核 `wmt_lib_set_hif()` → `hifType=WMT_HIF_BTIF` + `mtk_wcn_stp_set_if_tx_type(STP_BTIF_IF_TX)`, 并排队一个 `WMT_OPID_HIF_CONF` op — **这就是"HIF_CONF 何时发"的答案: launcher 在 pwr_on 之前发**
6. `ioctl(WMT_IOCTL_SET_LAUNCHER_KILL, 0)`
7. `pthread_create(launcher_pwr_on_chip)`: 循环 `ioctl(WMT_IOCTL_LPBK_POWER_CTRL, 1)` 最多 20 次, 失败 usleep 1s 重试; 成功后置 `wcn.launcher.ready=yes`(Alpine 桩头下是 no-op)
8. 主线程进入 `poll(/dev/stpwmt)` 循环, **持续应答内核下发的 patch/命令请求** — 这个 poll 循环必须在 pwr_on 完成前就绪, 否则内核 `WMT_CTRL_GET_PATCH` 等不到 patch 数据而超时

**关键: step 5 必须先于 step 7**。若跳过 launcher 直接用 `echo 1 > /dev/wmtWifi`, WMT_OP_HIF_BIT 未设置 / HIF_CONF 未入队, 内核 `wmt_core_stp_init()` 开头 `if (!(pctx->wmtInfoBit & WMT_OP_HIF_BIT)) assert`。

### 2.2 内核侧 (WMT_IOCTL_LPBK_POWER_CTRL → mtk_wcn_wmt_func_on(LPBK))

`wmt_dev.c` ioctl → `mtk_wcn_wmt_func_on(WMTDRV_TYPE_LPBK)` → `opfunc_func_on` → WMT 状态非 FUNC_ON → `opfunc_pwr_on`:

```
opfunc_pwr_on (wmt_core.c:882)
 ├─ 若 op 带 WMT_OP_HIF_BIT 先执行 opfunc_hif_conf (从 wmt_lib 拿 HIF_CONF)
 ├─ WMT_CTRL_HW_PWR_ON → wmt_plat_pwr_ctrl(FUNC_ON)   ← MT6323 PMIC + CONNSYS clk (wmt_plat_alps.c)
 ├─ wmt_core_stp_init (wmt_core.c:636):
 │    ├─ wmt_conf_read_file() → filp_open("/system/etc/firmware/WMT_SOC.cfg")  ← 路径必须存在!
 │    ├─ WMT_CTRL_STP_OPEN (stp_btif open)
 │    ├─ STP disable + un-ready
 │    ├─ (hifType==BTIF) set STP mode = MTKSTP_BTIF_MAND_MODE   ← f_mode 第 1 档
 │    ├─ STP enable
 │    ├─ wmt_core_hw_check() (wmt_core.c:810):
 │    │    读 GEN_HCR 0x80000008 (经 STP-BTIF wmt_core_reg_rw_raw) → 0x6572
 │    │    → 选中 wmt_ic_ops_soc → ic_ver_check() (mtk_wcn_soc_ver_check)
 │    ├─ co_clock_ctrl (按 WMT_SOC.cfg co_clock_flag)
 │    ├─ mtk_wcn_soc_sw_init() (wmt_ic_soc.c:980):
 │    │    ├─ init_table_1_2 (查 chip STP 默认)
 │    │    ├─ init_table_4 (设 chip STP 选项)
 │    │    ├─ 设 host STP-BTIF-FULL 模式 + enable        ← f_mode 第 2 档
 │    │    ├─ osal_sleep_ms(10)  ← 机制切换等待, 至少 2ms
 │    │    ├─ init_table_5 (再查)
 │    │    ├─ 若 CFG_WMT_MULTI_PATCH(=1):
 │    │    │    mtk_wcn_soc_patch_info_prepare() → WMT_CTRL_PATCH_SEARCH (launcher 侧搜索 patch)
 │    │    │    WMT_CTRL_GET_PATCH_NUM → patch_num (0 = launcher 没服务)
 │    │    │    for each patch: mtk_wcn_soc_patch_dwn(idx) → WMT_CTRL_GET_PATCH
 │    │    │        (kernel filp_open 读 launcher 传的 ROMv1_patch 路径) → WMT_PATCH_CMD 逐片下发
 │    │    │    init_table_3 (WMT reset)
 │    │    └─ (非 multi-patch: mtk_wcn_soc_patch_dwn + init_table_3)
 │    └─ set STP ready
 └─ WMT DRV_STS_FUNC_ON   ← 整片 power-on 完成
```

**ROM patch 下载流程（问题 2 的答案）**: 对 CONNSYS, ROM patch 不是 launcher 一字节一字节吐, 而是 **launcher 把 patch 文件全路径通过 `WMT_IOCTL_SET_PATCH_INFO` 告诉内核, 内核 `wmt_ctrl_get_patch()`(wmt_ctrl.c:675) 用 `filp_open()+kernel_read()` 自己读文件**, 再经 STP-BTIF 的 `WMT_PATCH_CMD` 协议下发到 CONN MCU。所以:
- `-p` 目录必须是 Z1 rootfs 上的真实路径
- patch 文件名以 `ROMv1` 开头（launcher 对 0x6572/6582/6592 把 chipName 映射为 `ROMv1_patch` 前缀, 在 `-p` 目录里 glob `ROMv1*`）
- 内核读文件失败（路径/权限）→ `wmt_ctrl_get_patch` 返回 -1 → sw_init 失败 → pwr_on 重试

### 2.3 WiFi func-on（`echo 1 > /dev/wmtWifi`）

`wmt_chrdev_wifi.c` `WIFI_write()`: `local[0]=='1'` → `mtk_wcn_wmt_func_on(WMTDRV_TYPE_WIFI)`; `'0'`→off; `'A'`→AP 模式(改 ap0); `'S'/'P'`→STA/P2P。
→ `opfunc_func_on(WIFI)`: **若 WMT 已 FUNC_ON 则只做 WiFi 子系统 on** → wlan_gen2 `gl_kal.c:816` `request_firmware("WIFI_RAM_CODE_MT6582")`(fallback `WIFI_RAM_CODE`) → 下载 → cfg80211 注册 wlan0。
**若 WMT 还没 FUNC_ON, WIFI func-on 会自己触发整片 power-on** —— 这就是竞态源。

---

## 3. 时序稳定性诊断与修复建议（核心交付）

### 3.1 竞态 (A): 双线程并发整片 power-on
`echo 1 > /dev/wmtWifi` 与 launcher 的 LPBK pwr_on 线程若重叠, 两个线程同时在 `opfunc_pwr_on→wmt_core_stp_init` 里操作 STP 状态。内核侧无互斥（`g_es_lr_lock` 只包 LPBK workqueue 的 on/off, 不包 WMT 状态机）。
**修复**: 严格等待 launcher 整片 on 完成。非 Android 上 `wcn.launcher.ready` property 是桩, 用以下任一替代信号:
- `dmesg | grep "WMT-CORE: WMT \\[FUNC_ON\\]"`（opfunc_pwr_on 末尾打的）
- 轮询 `/proc/driver/wmt_dbg`（有就绪状态可读）
- 起 launcher 后固定 sleep ≥ 2~3s 再 `echo 1`（BPI-R2 实测在 launcher 后 **12s** 才 `echo A`——它保守, 但说明整片 on + patch 下载要秒级时间）

### 3.2 已修 bug (B): FUNC_ON op 不能带 WMT_OP_HIF_BIT
`wmt_exp.c:104-116` 注释完整描述了故障链: FUNC_ON op 带 HIF_BIT → opfunc_hif_conf 把 `au4OpData[0..2]`(=type 3/6) 当 HIF_CONF 覆写 → hifType 变非法 → stp_init 跳过 BTIF_MAND → f_mode=0 → `mtk_wcn_stp_send_data()` 返回 0 → **hw_check Tx REG_CMD fail**。
**行动**: 确认上板 .ko 是含此 fix 的构建; 若 func-on 又见 `hw_check fail` / `f_mode` 异常日志, 优先重编重刷, 别在旧 .ko 上调时序。

### 3.3 f_mode 状态残留 (C)
`mtk_wcn_soc_sw_init` 把 f_mode 从 MAND 切到 FULL 并等 10ms。失败重试不会干净回退。**重试前先做整片复位**: 用 `/proc/driver/wmt_dbg` 的 chip rst, 或 `rmmod/insmod mtk_stp_wmt_soc`（BTIF/STP 一起重来）。launcher 自带 20×1s 重试只适用干净状态, 不适用脏残留。

### 3.4 固件路径 (C, 部署级)
- `WMT_SOC.cfg` → 内核 `filp_open("/system/etc/firmware/WMT_SOC.cfg")`（`wmt_conf.h:33-34` 硬编码）。Alpine 上**要么建该目录, 要么改宏**成 `/etc/firmware/` 或 `/lib/firmware/`。
- `ROMv1_patch*` → launcher `-p` 目录里 glob, 路径经 SET_PATCH_INFO 回传内核再 filp_open。目录必须是设备真实路径, 内核权限可读。
- `WIFI_RAM_CODE_MT6582` → wlan_gen2 `request_firmware()` → **/lib/firmware/WIFI_RAM_CODE_MT6582**（gl_kal.c 先试 `_MT6582` 后缀）。
- 三个文件缺任何一个 = 偶发失败。（Z1 固件 blob 已确认有 `WIFI_RAM_CODE_MT6582`、`ROMv1_patch`、`WMT_SOC.cfg`, 见 `WIFI_FW_BLOB.md`; 但要核对 ROMv1_patch 的真实文件名, launcher 按 `ROMv1` 前缀 glob。）

### 3.5 EMI 别名窗口 (D, 唯一离线不能排除)
`BTIF_EMI_FIX_REVIEW_20260814.md` §3.1: AP 侧共享 EMI 迁到 0xbe080000, CONN 固件侧 `CONSYS_EMI_FW_PHY_BASE=0xf0080000` 未动。若 0xf0000000 别名固定映射旧 0x80080000（现在那里是普通 RAM）, 固件写共享区 = 踩内存 → 偶发握手失败。**上板验证法**: 整片 power-on 后读共享 EMI 区（`connsys-regdump.sh`）看是否有固件写回的握手数据流; 若没有, 连同 FW 侧地址一起迁（受 32 位表达限制, 若别名固定则需确认硬件行为）。

### 3.6 PSM / host wakeup
`mtk_wcn_wmt_func_ctrl` 排队 op 前会 `wmt_lib_host_awake_get()` + `DISABLE_PSM_MONITOR()`。若 chip 已睡, STP 发送失败。Z1 `bt-up.sh` 已用 `echo '1 0' > /proc/driver/wmt_dbg` 强制 quick-sleep off; **WiFi func-on 前同样需要 PSM 处于 off**, 否则偶发。

---

## 4. 蓝牙 enable 路径（问题 3 的答案）

MT6572 BT 控制器跑 HCI over **STP-BTIF** 端口。移植树 `stp_chrdev_bt.c` 已内含**下游式内核 HCI 注册**:
- `hci_alloc_dev()` → `mtk_hci.hdev->bus = HCI_SDIO` → `hci_register_dev()`（stp_chrdev_bt.c:461-485）
- open/close → `mtk_wcn_wmt_func_on/off(WMTDRV_TYPE_BT)`（开 `/dev/stpbt` 即触发 BT func-on）
- send/flush/notify 走 STP-BTIF; rx 用 `hci_recv_fragment`(H4)
- `/dev/stpbt` major=192（`BT_DEV_MAJOR 192`）

三条可行路径:
1. **内核 HCI 直注（= BPI-R2 mainline 成功做法）**: `modprobe stp_bt_chrdev` → BlueZ 直接见 hci0 → `hciconfig hci0 up`。BPI-R2 实测日志: `hci0: Type: Primary Bus: SDIO BD Address: ...`, 且**BT 模块必须在 wmt-tools(launcher) 跑完之后才加载**: `wifi.sh` → `modprobe stp_bt_chrdev` → `hciconfig hci0 up`。
2. **用户态 vhci 桥（Z1 当前方案）**: `stpbt-vhci-bridge` 把 `/dev/stpbt`(H4) ↔ `/dev/vhci`(hci_vhci) 双向泵, BlueZ 见 hci0; bridge 兼管 PSM。可行且不动内核 HCI。
3. **hciattach 不可直接用**: `/dev/stpbt` 是字符设备不是 tty, hciattach 需要 tty + N_HCI line discipline（bluez hciattach wiki）。除非把 stpbt 包一层 pty, 否则放弃。

**BT enable 顺序**: 必须等整片 WMT FUNC_ON（launcher 完成）之后。BPI-R2 的次序 `wmt_loader → stp_uart_launcher -p ... & → echo 1 > /dev/wmtWifi → modprobe stp_bt_chrdev → hciconfig hci0 up` 就是官方参考。

---

## 5. NVRAM / regulatory（问题 4 的答案）

- **MTK CONNSYS wlan 不需要 NVRAM 文件**: MAC 地址来自 efuse（Z1 已见 `<MAC>`）, 校准在 CONN MCU 内, 驱动只吃 `WIFI_RAM_CODE_MT6582` + efuse。没有像 Broadcom 那样的 NVRAM blob。
- **scan 前置**: `ip link set wlan0 up` 后 `iw dev wlan0 scan` 即可。regulatory 用 cfg80211:
  - 默认 world domain（2.4G ch1-13, 部分 NO-IR 被动）; `iw reg set CN`（或 wpa_supplicant `country=CN`）解除信道限制。
  - 若 scan 只出被动信道, 就是 world domain NO-IR, 属正常。
- wlan_gen2 驱动侧 channel 表在固件里, 没有独立 regulatory blob; `CONFIG_CFG80211` 的 REGDB/CRDA 按标准走。

---

## 6. 联网发现清单（全部带 URL）

### 6.1 权威/参考源码
- **stp_uart_launcher.c（launcher 源码, Z1 移植蓝本）**: https://github.com/LIFECorp/mediatek/blob/master/external/combo_tool/src/stp_uart_launcher.c
  - SOC 流判据、STP_BTIF_FULL、`ROMv1` patch 前缀映射（0x6572/6582/6592→`ROMv1_patch`; 8127/6571→`ROMv2`; 6752/6735/8163/6580→`ROMv2_lm`）
  - pwr_on 线程 `WMT_IOCTL_LPBK_POWER_CTRL` 20×1s 重试
- **MT6582 kernel 3.4.67（与 Z1 同家族同内核版本）WMT 源码**: https://github.com/ferhung-mtk/android_kernel_huawei_h30t00/tree/master/kernel-3.4/mediatek/platform/mt6582/kernel/drivers/wmt
  - `mtk_wcn_consys_hw.c` + `wmt_plat_alps.c` 是 CONNSYS 电源/EMI 参考
- **android.googlesource kernel/mediatek wmt_stp_exp.c（WMT STP 导出层）**: https://android.googlesource.com/kernel/mediatek/+/android-mtk-3.18/drivers/misc/mediatek/connectivity/common/common_detect/wmt_stp_exp.c

### 6.2 BPI-R2 / MT7623 CONNSYS（同栈最接近的完整 bring-up 实证）
- **MT6625L on mainline kernel 完整流程（含 launcher 日志、ROMv2_lm_patch 下载、WLAN-GEN2 init、BT stp_bt_chrdev 次序）**: https://forum.banana-pi.org/t/bpi-r2-internal-wifi-bt-mt6625l-kernel/4307
  - 关键日志: `stp_uart_launcher -p /etc/firmware &` → `chiId:0x7623, run SOC chip flow, create pwr on thread ok` → `enter power on connsys flow, patch name pre-fix:ROMv2_lm_patch, /etc/firmware/ROMv2_lm_patch_1_0_hdr.bin`
  - p20: **BT 模块须在 wmt-tools 之后加载**: `wifi.sh; modprobe stp_bt_chrdev; hciconfig hci0 up`（hci0 Bus: SDIO）
- **BPI R2 LEDE/openwrt 源码帖（wmt_loader + launcher + echo 的实测时间线）**: https://forum.banana-pi.org/t/topic/3878?page=4
  - 实测时序: `wmt_loader`[62.4s] → `stp_uart_launcher -p /etc/firmware/ &`[74.9s] → **12s 后** `echo A >/dev/wmtWifi`[86.9s] → `wpa_supplicant`[176.5s]
  - `wmt_loader` 打印 `failed to init module` 是正常（GPS 未开）, 忽略
  - 另帖: https://forum.openwrt.org/t/openwrt-lede-how-to-connect-to-wifi/5738 — `wmt_loader; stp_uart_launcher -p /etc/firmware &; sleep 2; echo 1 > /dev/wmtWifi`
- **BPI-R2 LEDE 源码（frank-w）**: https://forum.banana-pi.org/t/topic/3878?page=3 （`utils/wmt` 目录在 6.12-main 已删: https://github.com/frank-w/BPI-Router-Linux — 404, 需翻 git history）

### 6.3 MT6572/MT6582 mainline 状态确认
- **github.com/mt6572-mainline 组织（6 repos: linux/u-boot-pub/da-boot/kernel-compat+2, 无 connectivity 驱动）**: https://github.com/mt6572-mainline — **确认 Connectivity 无独立 repo/驱动**; kernel-compat 只是 U-Boot FIT: https://github.com/mt6572-mainline/kernel-compat
- **postmarketOS MT6572 wiki（WiFi/Bluetooth 状态 = Untested）**: https://wiki.postmarketos.org/wiki/MediaTek_MT6572
- **MT6582 mainline fork（Swer15, 纯 mainline + DT, 无 connectivity）**: https://github.com/Swer15/mt6582-mainline
- **MT6589 mainline（TeamYogaBlade2, 近 mainline, 亦无 connectivity）**: https://github.com/TeamYogaBlade2
- **MT65xx mainline 现状笔记（arzam16/empty-sixty-five: 只有 CPU/GIC/timer/UART/WDT; 无 clock driver 是最大瓶颈）**: https://github.com/arzam16/empty-sixty-five

### 6.4 固件 / regulatory
- **armbian firmware 里 WMT_SOC.cfg（独立 combo 版, 与 Z1 的 SOC 版不同）**: https://github.com/armbian/firmware/blob/master/mediatek/WMT_SOC.cfg
- **MediaTek 固件命名惯例（WIFI_RAM_CODE_MTxxxx / BT_RAM_CODE）**: https://github.com/morrownr/USB-WiFi/blob/main/home/How_to_Install_Firmware_for_Mediatek_based_USB_WiFi_adapters.md
- **MTK Wi-Fi 软件指南（CountryCode 配置字段）**: https://www.scribd.com/document/830114635/MTK-Wi-Fi-SoftAP-Software-Programming-Guide-v4-12-1
- **cfg80211 regulatory 处理规则（world domain 信道/NO-IR）**: https://wireless.docs.kernel.org/en/latest/en/developers/regulatory/processing_rules.html
- **hciattach 需要 tty 的官方说明**: https://github.com/bluez/bluez/wiki/hciattach

---

## 7. 给主会话的下一步动作清单

1. 核对上板 .ko 是否含 `wmt_exp.c` HIF_BIT fix; 没有就重编 `mtk_stp_wmt_soc`（模块层回归的最短验证: `echo 1 > /dev/wmtWifi` 后 dmesg 出现 `WMT [FUNC_ON]` 且无 `hw_check fail`）。
2. Z1 rootfs 上补齐固件文件与路径:
   - `/system/etc/firmware/WMT_SOC.cfg`（或改 `wmt_conf.h` 宏为 `/etc/firmware/`）
   - `/etc/firmware/ROMv1_patch*`（launcher `-p` 指向同一目录, 文件名 `ROMv1` 前缀）
   - `/lib/firmware/WIFI_RAM_CODE_MT6582`
3. 改 bring-up 脚本为严格三段 + 就绪等待:
   ```
   insmod mtk_btif_drv.ko
   insmod mtk_stp_wmt_soc.ko
   insmod mtk_wmt_wifi_soc.ko
   insmod wlan_gen2.ko        # 或 wlan.ko
   stp_uart_launcher -p /etc/firmware &
   # 等 dmesg 出现 "WMT-CORE: WMT [FUNC_ON]"（或 sleep 3~5s）
   echo 1 > /dev/wmtWifi
   ip link set wlan0 up
   iw dev wlan0 scan
   ```
4. 失败重试前先整片复位（`/proc/driver/wmt_dbg` chip rst 或 rmmod/insmod stp_wmt_soc）, 别在同一脏状态上盲重试。
5. 上板验证 EMI: 整片 on 后 dump 共享 EMI（`tools/connsys-regdump.sh`）, 确认固件握手数据流在 0xbe080000; 没有则按 BTIF_EMI_FIX_REVIEW §3.1 评估迁 `CONSYS_EMI_FW_PHY_BASE`。
6. BT: 在 WMT FUNC_ON 之后加载内核 HCI 路径（`stp_chrdev_bt` 内含 hci_register_dev, 等价 BPI-R2 的 stp_bt_chrdev）, 或继续用 stpbt-vhci-bridge; BlueZ 顺序 `hciconfig hci0 up`。
