# Z1 (MT6572) WiFi/BT 排查汇总 — 可执行总结 (2026-08-14)

> 汇总来源（均已存在，全部只读）:
> 1. `WIFI_BT_WEB_RESEARCH_20260814.md` — 联网调研 + /tmp/connsys_build 源码级核对
> 2. `FW_DOWNLOAD_TIMING_20260814.md` — ROM patch/RAM code 下载时序 + func-on 前置条件
> 3. `BT_ENABLE_PATH_20260814.md` — 蓝牙 enable 完整路径 + 就绪/缺口清单
> 4. `HW_CHECK_FAIL_ROOTCAUSE_20260814.md` — func-on hw_check fail (-2) 根因
> 另参考: `BTIF_EMI_FIX_REVIEW_20260814.md`（EMI 别名窗口隐患）、`CONNSYS_MAINLINE_REFERENCE_20260814.md`（联网 BT 结论）
> 源码根: `/tmp/connsys_build/`。板子当前关机，本文档纯读汇总。

---

## 0. 一句话总览

**WiFi 的根因已锁定为一组 host 侧软件回归/时序问题（非硬件）**：上一轮为修 "no hif info" 竞态在 `wmt_exp.c` 给所有 FUNC op 无条件加 `WMT_OP_HIF_BIT`，导致 `opfunc_hif_conf` 把 launcher 写好的合法 `hifType=BTIF(2)` 覆盖成 3/6 → `wmt_core_stp_init` 跳过 `MTKSTP_BTIF_MAND_MODE` → f_mode=0 → STP 发不出 → hw_check fail (-2)。加上 `echo 1 > /dev/wmtWifi` 与 launcher LPBK power-on 的双线程竞态、固件文件绝对路径缺位。**BT 的底层（BTIF/WMT/STP//dev/stpbt/launcher）全部就绪且经 WiFi func-on 偶发成功间接证明活着；缺口全在上层**（内核 BT/HID 模块没编 + BT 用户态工具没编 + BlueZ 没入 ramdisk）。

---

## 1. WiFi 当前问题清单（按阻塞度排序）

### W1 [最高] func-on 时序竞态 — `echo 1` 抢在 launcher 整片 power-on 完成前
- **根因**: launcher 的 `launcher_pwr_on_chip` 线程发 `WMT_IOCTL_LPBK_POWER_CTRL 1` 做整片 power-on；若 `echo 1 > /dev/wmtWifi`（WIFI func-on）与之重叠，WIFI func-on 会**自己触发整片 power-on**，双线程并发跑 `wmt_core_stp_init()`。内核 WMT 状态机**无互斥锁**（`g_es_lr_lock` 只包 LPBK workqueue），STP f_mode/状态被写乱 → 偶发失败。
- **修复方向**: 严格三段时序 + 就绪等待（见 §3）。非 Android 无 `wcn.launcher.ready` property，用 `dmesg | grep "WMT-CORE: WMT [FUNC_ON]"` 或轮询 `/proc/driver/wmt_dbg`，或 launcher 后固定 sleep ≥3~5s（BPI-R2 实测 12s）。

### W2 [最高] HIF_BIT 回归 bug — f_mode=0 → hw_check fail (-2)（HW_CHECK_FAIL_ROOTCAUSE 确认根因）
- **根因**: `wmt_exp.c:108` 给每个 FUNC_ON/FUNC_OFF op 无条件加 `u4InfoBit=WMT_OP_HIF_BIT` → `opfunc_pwr_on`(wmt_core.c:897) 每次调 `opfunc_hif_conf`(wmt_core.c:877)，把 launcher 先前经 SET_STP_MODE→HIF_CONF op 写好的 `wmtHifConf` 用 FUNC op 的 `au4OpData[0..2]`（=type 3/6）覆盖 → hifType=3/6（非法，合法 BTIF=2）→ `wmt_core_stp_init` L669 `hifType==BTIF` 为假，跳过 `MTKSTP_BTIF_MAND_MODE` → **f_mode 停留 0** → `mtk_wcn_stp_send_data()` MAND/FULL 分支全不命中直接 return 0 → `Tx REG_CMD fail!(0) len (0,20)`（3×360ms 重试）→ hw_check 读 0x80000008 失败 -2 → stp_init -4 → pwr_on fail。
- **修复方向**: FUNC_ON/FUNC_OFF op **不要携带 WMT_OP_HIF_BIT**（删除/条件化 `wmt_exp.c:108`）；或无损回写——同时把 `wmt_lib_get_hif()` 合法配置拷进 `au4OpData` 让 `opfunc_hif_conf` 变 no-op。**上板前必须先确认当前 .ko 已含此 fix**（注释在 `conn_soc/linux/pri/wmt_exp.c:104-116`）；没含就重编 `mtk_stp_wmt_soc`，别在旧 .ko 上调时序。
- **判定日志**: 修复前 dmesg 出现 `WMT HIF already exist. overwrite! old(2), new(3/6)`；修复后该行不应出现。

### W3 [高] launcher / SET_STP_MODE 必须先于任何 func-on（三条 host 侧门禁）
- **根因**: 内核三条门禁只有 launcher 的 `WMT_IOCTL_SET_STP_MODE`（→ `wmt_lib_set_hif()` + 入队 HIF_CONF op）能全部满足:
  1. `wmtInfoBit & WMT_OP_HIF_BIT` — 缺则 `wmt_core_stp_init` L647 `assert + "no hif info!"`
  2. `hifType==WMT_HIF_BTIF` → f_mode=BTIF_MAND（缺则 f_mode=0，见 W2）
  3. `g_stp_if_type` 有指向 — **本 fork 已改默认 `STP_BTIF_IF_TX`**（stp_exp.c:74，"CONNSYS stability fix C"），消除了调度门禁，但 hifType/f_mode 两条仍必须 launcher 提供。
  - 另外 launcher 主线程必须**持续 poll /dev/stpwmt** 应答内核 sw_init 发的 `srh_patch`，否则 `patch_info_prepare` 阻塞 2s 超时。
- **修复方向**: launcher 用 `-m 3`（BTIF 显式确认，SOC 芯片 0x6572 默认强制 BTIF_FULL）且先于任何 func-on 启动；`-p <目录>` 指真实固件目录。兜底：`wmt_core_stp_init` 对 SOC 芯片默认按 BTIF 处理 + 模块加载时默认注册 BTIF if_tx。

### W4 [中] hw_check fail (-2) 的其他可能（在 W2 修好之后）
- **根因**: hw_check 读 0x80000008（GEN_HCR）必须由 **CONN MCU ROM 的 STP 引擎 + WMT 命令处理器**应答（host 不能直读 CONNSYS 内部寄存器）；若 CONNSYS 电源门没真正打开（`wmt_plat_pwr_ctrl` 桩/no-op），0x80000008 读超时。
- **修复方向**: 先修 W2/W3；上板确认 dmesg 出现 `get hwcode (chip id) (0x6572)`；若电源桩问题，重写 `wmt_plat_alps.c`/consys 电源序列（MT6323 PMIC + CONNSYS clk + reset）。注意 sw_init 的 BTIF 三连（`QUERY_STP-DEFAULT` → `SET_STP` → 10ms → `QUERY_STP`）本身也是 ROM STP 引擎存活探测，第一条命令挂即 -2。

### W5 [中] 固件文件路径缺位 → 静默失败/偶发失败（部署级）
- **根因**（内核 `filp_open`，非 request_firmware 读板级配置）:
  - `WMT_SOC.cfg`: `wmt_conf.h:33-34` 硬编码 `/system/etc/firmware/WMT_SOC.cfg` — Alpine 没此目录即静默失败（insmod 失败，上板实测过）。
  - `ROMv1_patch*`: launcher `-p` 目录 glob（0x6572→`ROMv1` 前缀），路径经 `WMT_IOCTL_SET_PATCH_INFO` 回灌内核再 filp_open — 目录必须是设备真实路径且内核可读。
  - `WIFI_RAM_CODE_MT6582`: wlan_gen2 `request_firmware` → `/lib/firmware/WIFI_RAM_CODE_MT6582`（gl_kal.c 先试 `_MT6582` 后缀）。
  - `patch total num = [0]` = launcher 没扫到 ROMv1_patch*.bin 或 ROM 主版本不匹配（版本字节在文件偏移 22），不是下载失败。
- **修复方向**: 三处文件补齐（见 §4 清单）；缺任何一个 = 偶发失败。

### W6 [低-中, 离线无法排除] EMI 别名窗口
- **根因**: AP 侧共享 EMI 已迁 0xbe080000（dts `connsys@be000000` carveout），CONN 固件侧 `CONSYS_EMI_FW_PHY_BASE=0xf0080000` 未动。若 0xf0000000 别名窗口固定映射旧 0x80080000（现在普通 RAM），固件写共享区 = 踩内存 → 偶发握手失败，与"偶发成功"高度吻合。
- **修复方向**: 上板整片 power-on 后 dump 共享 EMI（`tools/connsys-regdump.sh`）看 0xbe080000 有无固件写回的握手数据流；没有则评估迁 `CONSYS_EMI_FW_PHY_BASE`。

### W7 [低] PSM / host wakeup
- **根因**: `mtk_wcn_wmt_func_ctrl` 排队 op 前 `wmt_lib_host_awake_get()` + `DISABLE_PSM_MONITOR()`；chip 已睡则 STP 发送失败。
- **修复方向**: WiFi func-on 前 `echo '1 0' > /proc/driver/wmt_dbg`（quick-sleep off）+ PSM off。BT bridge 的 PSM governor 对 WiFi 失明，**WiFi 会话别跑 bridge 的 PSM governor**（WMT SLEEP 会杀固件）。

---

## 2. 蓝牙当前问题清单

### 底层状态: ✅ 全部就绪（无需驱动源码改动）
- 传输层 `mtk_btif_drv.ko`（dts `btif@1100e000`）、控制面 `mtk_stp_wmt_soc.ko`（dts `connectivity@18070000`，稳定性修复 A+C+D 已落树）、BT 通道 `mtk_stp_bt_soc.ko`（/dev/stpbt major 192）、`mtk_wmt_wifi_soc.ko`（/dev/wmtWifi 155）、`wlan_gen2.ko` + `cfg80211.ko` — 全 z1 vermagic 编好。
- launcher 静态 ARM 二进制已编（522212 B）；固件 blob 齐全；ramdisk 已含 6 .ko + launcher + 脚本 + 固件 + iw。
- **BTIF 是 BT 和 WiFi 的公共底座**；"func-on WiFi 偶发成功"已间接证明 BTIF/WMT/chip 上电/patch 下载这条 BT 底链在板上活过。

### BT enable 路径（两条可用 + 一条不可用）
1. **内核 HCI 直注**（= BPI-R2 mainline 成功做法）: 把 `stp_chrdev_bt.c` 编成 `-DMTK_BT_HCI=1`（内含 `hci_register_dev`，bus=HCI_SDIO，`mtk_bt_hci_init` L457-493 整个 `#ifdef MTK_BT_HCI`）→ BlueZ 直接见 hci0 → `hciconfig hci0 up`。BPI-R2 实测次序: `wifi.sh → modprobe stp_bt_chrdev → hciconfig hci0 up`（hci0 Bus: SDIO）。
2. **用户态 vhci 桥（Z1 当前方案，本硅片唯一验证过的接法）**: `stpbt-vhci-bridge` 把 `/dev/stpbt`(H4) ↔ `/dev/vhci`(hci_vhci) 双向泵；bridge 兼做 H4 重定帧 + MTK quirk shim（丢 hw-error evt、伪造 ReadLocalExtFeatures 空页）+ PSM 治理。**当前 build 未编 `MTK_BT_HCI`**（out-of-tree 走 Kbuild，`-DMTK_BT_HCI=1` 在子 Makefile 没被读）→ 走字符设备路径 = 与已验证参考一致。
3. **hciattach 不可用**: `/dev/stpbt` 是字符设备不是 tty；hciattach 需要 tty + N_HCI line discipline。除非包一层 pty，否则放弃。
- **关键依赖**: `BT_open` 只触发 `func_on(WMTDRV_TYPE_BT)`，**不负责上电芯片**；STP 必须 already ready（`stp_is_ready()` 不过直接 -ENODEV）→ BT 必须等 launcher 整片 FUNC_ON 完成之后。BT 不需要独立固件（CONSYS ROM 自带 BT 栈；patch 是 BT/WiFi/GPS/FM 合一 WMT patch，BT-only 冒烟 patch 文件可有可无，但 launcher 必须在线应答 srh_patch）。

### 缺口（BT 实测前的全部拦路项）
1. **内核 BT/HID 模块没编、config 没开（最大缺口）**: `fork_linux/.config` 有 `CONFIG_BT=m`，但 **`CONFIG_BT_HCIVHCI`（/dev/vhci 必需）、`CONFIG_BT_HIDP`、`CONFIG_UHID` 没开**；`CONFIG_CRYPTO_LIB_AES=m`。树内无任何 BT .ko。需: `make ARCH=arm CROSS_COMPILE=... M=net/bluetooth modules`（bluetooth.ko/ecc.ko/ecdh_generic.ko）+ `M=drivers/bluetooth`（hci_vhci.ko）+ `M=net/bluetooth/hidp`（hidp.ko）+ `M=drivers/misc/uhid`（uhid.ko），再打进 ramdisk `/root/connsys/bt/`。
2. **BT 用户态工具没编没入 ramdisk**（源码在 `/tmp/connsys_build/tools/`）: `stpbt-vhci-bridge`（必需）、`stpbt-hci-test`、`hci-localver`、`btup-scan`、`btif-lpbk-test`。构建: `arm-linux-gnueabihf-gcc -static -O2`。
3. **BlueZ 用户态没入 ramdisk**: bluetoothd / bluetoothctl / dbus-daemon + lib（参考实现装在 /opt/bt；Alpine 需 apk 装 bluez/dbus 或静态版）。
4. **BT 未上板实测**（缺 1-3 自然没测）。
- 注意项: BT MAC 来自 efuse 或 BT_Addr NVRAM（stock 在 /data/nvram/APCFG/APRDEB/BT_Addr），不配也能配对仅 MAC 可能随机；`/dev/stpbt` 单开 + 阻塞 read 会挂死控制台 → 只用非阻塞工具；HCI 冒烟失败 = **必须 reboot，绝不 retry / 绝不 rmmod `mtk_stp_wmt_soc`**（rmmod 不可中断挂死；自恢复后的 radio 半活，outbound paging 静默死）。

---

## 3. 正确 func-on 时序（三段严格先后，源码级）

```
【阶段 1】insmod 造节点（顺序固定）
  insmod mtk_btif_drv.ko  →  mtk_stp_wmt_soc.ko  →  mtk_wmt_wifi_soc.ko  →  wlan_gen2.ko (+cfg80211.ko)
  → 出现 /dev/stpwmt(190) /dev/stpbt(192) /dev/wmtWifi(155)

【阶段 2】起 launcher，完成整片 power-on（等它做完，勿抢）
  stp_uart_launcher -m 3 -p <固件目录, 如 /etc/firmware> &
  ① open /dev/stpwmt（失败 usleep 300ms 重试）
  ② query_chip_id → ioctl WMT_IOCTL_WMT_QUERY_CHIPID → 0x6572（Z1 fork 硬编码 PLATFORM_SOC_CHIP）
  ③ SOC 流: gStpMode=STP_BTIF_FULL（0x6572∈SOC 集合强制）
  ④ ioctl SET_PATCH_NAME
  ⑤ ★ioctl SET_STP_MODE(0x23 = FM_COMM<<4|BTIF_FULL) → 内核 wmt_lib_set_hif(): hifType=BTIF(2)
     + g_stp_if_type=BTIF + au4StrapConf[0]=FM_COMM + 入队 HIF_CONF op（必须先于任何 func-on）
  ⑥ ioctl SET_LAUNCHER_KILL 0
  ⑦ pthread pwr_on_chip: ioctl LPBK_POWER_CTRL 1（失败 20×1s 重试）→ 内核 FUNC_ON(LPBK):
       opfunc_pwr_on:
         opfunc_hif_conf（若 op 带 HIF_BIT；正确 build 的 FUNC op 不带）→ 保留 launcher 的 BTIF
         WMT_CTRL_HW_PWR_ON → wmt_plat_pwr_ctrl(FUNC_ON)      ← MT6323 PMIC + CONNSYS clk + reset
         wmt_core_stp_init:
           STP_OPEN（BTIF 打开, stp_btif_if_tx 注册, rx parser 挂上）
           STP_CONF EN=0 / RDY=0
           hifType==BTIF → MODE=MTKSTP_BTIF_MAND_MODE       ← f_mode=bit3（P4）
           STP_CONF EN=1                                    ← P5
           hw_check: SET_REG 读 0x80000008 ──► CONN MCU ROM STP 引擎应答 0x6572
                     + ver_check 读 0x80000000/0x80000004（ROM ver, 填 gDevWmt.chip_id/fw_ver）
           sw_init (mtk_wcn_soc_sw_init):
             init_table_1_2: QUERY_STP-DEFAULT → ROM 答（P7）
             init_table_4:   SET_STP(BTIF full-set) → ROM 切
             host MODE=BTIF_FULL + EN=1; osal_sleep_ms(10)
             init_table_5:   QUERY_STP → ROM 报 DF 0E 68 01
             patch_info_prepare: 发 "srh_patch"（经 /dev/stpwmt）→ launcher 应答:
               扫 /etc/firmware ROMv1_patch*.bin（版本字节偏移22 匹配）→ ioctl SET_PATCH_NUM/INFO → 回 "ok"
             GET_PATCH_NUM → patch_num（0=launcher 没扫到）
             逐片 GET_PATCH_INFO/GET_PATCH（内核 filp_open 读文件）→ PATCH_ADDRESS/P_PADDRESS → 分片 PATCH_CMD → init_table_3 WMT_RESET
             （后续）EFUSE/RF 校准/PALDO/coex/FM strap/PS enable
           STP_RDY → WMT DRV_STS_FUNC_ON ← ★等这个标志
  ⑧ 主线程持续 poll /dev/stpwmt（应答 srh_patch/命令；必须在 pwr_on 前就绪）

【阶段 3】等阶段 2 完成后才操作子系统
  等: dmesg "WMT-CORE: WMT [FUNC_ON]"（或 sleep ≥3~5s）
  echo 1 > /dev/wmtWifi        → WIFI func-on（WMT 已 FUNC_ON → 跳过整片 pwr_on → wlan probe → request_firmware("WIFI_RAM_CODE_MT6582") → 出 wlan0）
  ip link set wlan0 up ; iw dev wlan0 scan
  蓝牙（BT 也等整片 FUNC_ON 之后）:
    modprobe stp_bt_chrdev → hciconfig hci0 up        （内核 HCI 直注路线）
    或 stpbt-vhci-bridge & → sleep 2 → hciconfig hci0 up（vhci 桥路线）
```

**前置条件清单 P1-P10**（FW_DOWNLOAD_TIMING 提取）: P1 CONNSYS 上电（func-on 自己完成）| P2 launcher 已发 SET_STP_MODE 且 HIF_CONF op 已处理（hifType=BTIF, HIF bit, g_stp_if_type=BTIF）| P3 STP_OPEN 完成 | P4 f_mode=BTIF_MAND | P5 STP_EN=1 | P6 hw_check 读到 0x6572 | P7 sw_init BTIF 三连 ROM 应答 | P8 launcher 存活且 poll /dev/stpwmt | P9 /etc/firmware 有 ROMv1_patch* 且版本匹配 | P10 patch 下完 → STP_RDY → WMT FUNC_ON。

**坑提示**: fork 只修了 P2 第 3 项（g_stp_if_type 默认 BTIF）；P2 前两项（hifType + wmtInfoBit）仍必须 launcher SET_STP_MODE 提供——`echo 1` 早于 SET_STP_MODE ioctl 仍会失败。失败重试前先整片复位（/proc/driver/wmt_dbg chip rst 或 rmmod/insmod mtk_stp_wmt_soc），别在同一脏状态盲重试。

---

## 4. 下次上板验证清单（按序）

### 4.0 前置准备（编译/打包，板上之前）
1. **确认/重编 .ko 含 W2 的 HIF_BIT fix**（`wmt_exp.c:104-116` 注释）；没有则重编 `mtk_stp_wmt_soc`。验证法: `echo 1` 后 dmesg 有 `WMT [FUNC_ON]` 且无 `WMT HIF already exist. overwrite!`。
2. **补齐固件三处**:
   - `/system/etc/firmware/WMT_SOC.cfg`（或改 `wmt_conf.h` 宏为 `/etc/firmware/` 后重编）
   - `/etc/firmware/ROMv1_patch*`（launcher `-p` 同目录，`ROMv1` 前缀，偏移 22 版本字节匹配）
   - `/lib/firmware/WIFI_RAM_CODE_MT6582`
3. **BT 缺口补齐**（§2 缺口 1-3）: 编 bluetooth.ko/ecc.ko/ecdh_generic.ko/hci_vhci.ko/hidp.ko/uhid.ko + 编 stpbt-vhci-bridge/stpbt-hci-test/hci-localver/btup-scan/btif-lpbk-test + BlueZ 入 ramdisk。

### 4.1 WiFi scan（板上）
1. `sh /root/connsys/z1_modprobe_connsys.sh` — 等 dmesg: `chip id 0x6572` / `patch total num = [n]` / `STP ready`。
2. PSM 前置: `echo '1 0' > /proc/driver/wmt_dbg`（quick-sleep off）。
3. launcher 完成后（等 `WMT [FUNC_ON]` 或 sleep 3~5s）: `echo 1 > /dev/wmtWifi`。
4. 期望 dmesg 链: `get hwcode (chip id) (0x6572)` → `patch total num = [n]` → `WMT turn on WIFI success` → `wlan0` + MAC（efuse <MAC>）。
5. `ip link set wlan0 up` → `iw dev wlan0 scan`；若只有被动信道/NO-IR → `iw reg set CN`（world domain 正常现象）。**预期: 主动扫描能发现 AP。**
6. 若失败: 看 dmesg 是否出现 `WMT HIF already exist. overwrite! old(2), new(3/6)`（= 旧 .ko 未含 fix）；整片复位后再试，绝不盲重试。
7. EMI 验证: 整片 on 后 `tools/connsys-regdump.sh` dump 共享 EMI，确认握手数据流在 0xbe080000。

### 4.2 BT（板上，WiFi 成功后或独立跑）
1. 同一底链脚本（`z1_modprobe_connsys.sh`）→ 期望 `chip id 0x6572` / `patch total num = 2` / `STP ready`。
2. BT 冒烟（不依赖 BlueZ）: `/root/connsys/stpbt-hci-test` → 期望 `HCI RESET OK - radio answers, BT link is LIVE`。**失败 → reboot 重试，绝不 retry/rmmod。** 可加 `/root/connsys/hci-localver`。
3. 内核 BT/HID 模块（补缺口后）: `insmod bluetooth.ko`（先 `insmod libaes.ko`）→ ecc.ko → ecdh_generic.ko → hci_vhci.ko → hidp.ko → uhid.ko。
4. PSM 前置: `echo '1 0' > /proc/driver/wmt_dbg` + `echo '0 0' > /proc/driver/wmt_dbg`；`devmem 0x10001800 & 0x10000` 非 0 = AP2CONN_OSC_EN ok。
5. hci0 上线: `/root/connsys/stpbt-vhci-bridge > /tmp/bridge.log 2>&1 &` → `sleep 2` → `hciconfig hci0 up`（或 `bluetoothctl power on`）。
6. 扫描: `/root/connsys/btup-scan`（裸 HCI inquiry）；或全 BlueZ: `dbus-daemon --system --fork` → `bluetoothd -n &` → `bluetoothctl scan on` → `pair <BDADDR>`。
7. 注意: bridge 的 PSM governor 只认 BT 流量，WiFi 会话不要跑；`/dev/stpbt` 只用非阻塞工具。

---

## 5. 联网参考 URL 汇总

### 权威/参考源码
- stp_uart_launcher.c（launcher 源码, Z1 移植蓝本; SOC 流判据/STP_BTIF_FULL/ROMv1 前缀映射）: https://github.com/LIFECorp/mediatek/blob/master/external/combo_tool/src/stp_uart_launcher.c
- MT6582 kernel 3.4.67 WMT 源码（与 Z1 同家族同版本; mtk_wcn_consys_hw.c + wmt_plat_alps.c）: https://github.com/ferhung-mtk/android_kernel_huawei_h30t00/tree/master/kernel-3.4/mediatek/platform/mt6582/kernel/drivers/wmt
- android.googlesource wmt_stp_exp.c（WMT STP 导出层）: https://android.googlesource.com/kernel/mediatek/+/android-mtk-3.18/drivers/misc/mediatek/connectivity/common/common_detect/wmt_stp_exp.c

### BPI-R2 / MT7623 CONNSYS（同栈最接近的完整 bring-up 实证）
- MT6625L on mainline 完整流程（launcher 日志/ROMv2_lm_patch 下载/WLAN-GEN2 init/BT stp_bt_chrdev 次序; p20: BT 模块须在 wmt-tools 之后加载）: https://forum.banana-pi.org/t/bpi-r2-internal-wifi-bt-mt6625l-kernel/4307
- BPI R2 LEDE（wmt_loader + launcher + echo 实测时间线; 12s 后 echo; wmt_loader "failed to init module" 正常）: https://forum.banana-pi.org/t/topic/3878?page=4 （page=3: https://forum.banana-pi.org/t/topic/3878?page=3）
- OpenWrt LEDE wifi connect（wmt_loader; launcher -p /etc/firmware &; sleep 2; echo 1）: https://forum.openwrt.org/t/openwrt-lede-how-to-connect-to-wifi/5738

### MT6572/MT6582 mainline 状态确认
- mt6572-mainline 组织（6 repos 无 connectivity 驱动; kernel-compat 仅 U-Boot FIT）: https://github.com/mt6572-mainline / https://github.com/mt6572-mainline/kernel-compat
- postmarketOS MT6572 wiki（WiFi/BT = Untested）: https://wiki.postmarketos.org/wiki/MediaTek_MT6572
- Swer15 mt6582-mainline（纯 mainline + DT, 无 connectivity）: https://github.com/Swer15/mt6582-mainline
- TeamYogaBlade2 MT6589 mainline: https://github.com/TeamYogaBlade2
- arzam16/empty-sixty-five（MT65xx mainline 现状: 只有 CPU/GIC/timer/UART/WDT; 无 clock driver 是最大瓶颈）: https://github.com/arzam16/empty-sixty-five

### 固件 / regulatory / BT
- armbian firmware 的 WMT_SOC.cfg（独立 combo 版，与 Z1 SOC 版不同）: https://github.com/armbian/firmware/blob/master/mediatek/WMT_SOC.cfg
- MediaTek 固件命名惯例（WIFI_RAM_CODE_MTxxxx）: https://github.com/morrownr/USB-WiFi/blob/main/home/How_to_Install_Firmware_for_Mediatek_based_USB_WiFi_adapters.md
- MTK Wi-Fi 软件指南（CountryCode 配置）: https://www.scribd.com/document/830114635/MTK-Wi-Fi-SoftAP-Software-Programming-Guide-v4-12-1
- cfg80211 regulatory 处理规则（world domain 信道/NO-IR）: https://wireless.docs.kernel.org/en/latest/en/developers/regulatory/processing_rules.html
- hciattach 需 tty 的官方说明: https://github.com/bluez/bluez/wiki/hciattach

> 注: BT_ENABLE_PATH 报告运行时 WebSearch/WebFetch 不可用（provider 400），其 BT 联网结论来自 `CONNSYS_MAINLINE_REFERENCE_20260814.md`（此前 tavily+firecrawl 产出），核心结论与树内参考实现一致。

---

## 6. 相关文件索引（绝对值）
- 汇总各报告目录: `/home/<user>/claude/6572/mainline_recon/`（WIFI_BT_WEB_RESEARCH / FW_DOWNLOAD_TIMING / BT_ENABLE_PATH / HW_CHECK_FAIL_ROOTCAUSE / BTIF_EMI_FIX_REVIEW / CONNSYS_MAINLINE_REFERENCE / CONNSYS_FW_READINESS / CONNSYS_STABLE_FIX / CONNSYS_MISSING_BUILT，均 _20260814.md）
- 构建/参考树: `/tmp/connsys_build/`（README 含硅片级验证状态；tools/ 含 launcher/bridge/hci-test 等）
- Z1 dts: `/home/<user>/claude/6572/fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts`
- Z1 内核 config: `/home/<user>/claude/6572/fork_linux/.config`
- ramdisk 打包: `/home/<user>/claude/6572/tools/repack_ramdisk_connsys.sh`；当前 ramdisk `/home/<user>/claude/6572/out/ramdisk_mainline_connsys.gz`
- 上板脚本: `/home/<user>/claude/6572/scripts/z1_modprobe_connsys.sh`、`z1_connsys_onboard_test.sh`
- 固件源: `/home/<user>/claude/6572/out/firmware_backup/firmware/`
