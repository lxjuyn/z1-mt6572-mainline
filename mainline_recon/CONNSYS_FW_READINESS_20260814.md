# CONNSYS WiFi/BT 固件部署就绪审计 (2026-08-14)

> 只读审计。板子关机，未碰硬件、未改任何文件。
> 审计对象: Z1 (MT6572) mainline CONNSYS 栈 — 5 外部模块 + ramdisk + 固件 blob。
> 内核: `7.0.0-rc7-g2cd361bd40c7-dirty SMP mod_unload ARMv7 p2v8` (fork_linux, 2026-08-14 重建)

---

## 0. 结论一句话

**固件 blob 全部在本地且齐全，deploy_connsys_fw.sh 的部署路径与 wlan_gen2 / mtk_stp_wmt_soc 期望路径一致；但"上板能跑"还差 4 个非固件件：① mtk_stp_launcher 用户态守护进程未编译/未入 ramdisk（WMT patch 无人喂，WiFi RAM code 会在 func-on 入口死，BT 不受影响）；② cfg80211.ko 缺失（CONFIG_CFG80211=m 且没编进内核，也没有与当前 vermagic 匹配的 .ko，wlan_gen2 insmod 会 unknown symbol）；③ Wi-Fi NVRAM (`/etc/firmware/nvram/WIFI`) 本地没有任何提取产物；④ 当前 boot img 内嵌的 ramdisk 只有 .ko，**不含任何固件**——固件必须靠 deploy 脚本在设备上现装。**

---

## 1. deploy_connsys_fw.sh 部署清单 vs 模块期望路径

脚本: `fork_linux/scripts/deploy_connsys_fw.sh`（`FW_SRC` 默认 `out/firmware_backup/firmware`，`ROOT` 默认 `/`）

| 源文件 (FW_SRC) | 部署目标路径 | 由谁读取 | 是否匹配模块期望 |
|---|---|---|---|
| `WIFI_RAM_CODE_SOC` | `<ROOT>/lib/firmware/WIFI_RAM_CODE_MT6582` | wlan_gen2 `request_firmware` | ✅ 一致 |
| `WIFI_RAM_CODE_SOC` | `<ROOT>/lib/firmware/WIFI_RAM_CODE_SOC` | wlan_gen2 `request_firmware` (chipinfo fallback) | ✅ 一致 |
| `mt6572_82_patch_e1_0_hdr.bin` | `<ROOT>/system/etc/firmware/mt6572_82_patch_e1_0_hdr.bin` | mtk_stp_launcher (用户态) | ✅ 一致 |
| `mt6572_82_patch_e1_1_hdr.bin` | `<ROOT>/system/etc/firmware/mt6572_82_patch_e1_1_hdr.bin` | mtk_stp_launcher (用户态) | ✅ 一致 |
| `mt6572_82_patch_e1_0_hdr.bin` | `<ROOT>/system/etc/firmware/ROMv1_patch_0_hdr.bin` | mtk_stp_launcher (无 Android property 时 fallback 名) | ✅ 一致 |
| `mt6572_82_patch_e1_1_hdr.bin` | `<ROOT>/system/etc/firmware/ROMv1_patch_1_hdr.bin` | mtk_stp_launcher (同上) | ✅ 一致 |
| `WMT_SOC.cfg` | `<ROOT>/system/etc/firmware/WMT_SOC.cfg` | mtk_stp_wmt_soc 内核 `wmt_dev_read_file` (filp_open 绝对路径) | ✅ 一致 |

**不在此脚本覆盖范围的部署（脚本不装，但模块需要）:**
- `mtk_stp_launcher` 二进制 — 见 §5.1（**关键缺口**）
- `/etc/firmware/nvram/WIFI` (Wi-Fi NVRAM) — 见 §5.4
- `cfg80211.ko` — 见 §5.2
- `stpbt-hci-test` / `stpbt-vhci-bridge`（BT 冒烟/PSM 治理，BT 功能非固件件）

---

## 2. 本地固件 blob 库存确认

`out/firmware_backup/firmware/` (2022-08-23 原厂提取) — 全部存在:

| 文件 | 大小 | 判定 | 部署需要? |
|---|---|---|---|
| `WIFI_RAM_CODE_SOC` | 160,464 B | 前4字节 `MTKW`, platform=0x0030 **SOC 集成模式** → 正是 MT6572 CONNSYS 用的 | ✅ 是 |
| `WIFI_RAM_CODE` | 158,784 B | 无 MTKW magic (`LE#M`) | 备选(非首选) |
| `WIFI_RAM_CODE_E6` | 159,264 B | 非 MTKW | 否 |
| `WIFI_RAM_CODE_MT6628` | 157,344 B | MTKW platform=0x37b0, **MT6628 非 Z1** | 否 |
| `WIFI_RAM_CODE_MT6630` | 235,760 B | MTKW platform=0x0813, **MT6630 非 Z1** | 否 |
| `mt6572_82_patch_e1_0_hdr.bin` | 37,360 B | 头 ASCII `20140507093455a...`, offset22=0x8a | ✅ 是 |
| `mt6572_82_patch_e1_1_hdr.bin` | 20,488 B | 同上, offset22=0x8a | ✅ 是 |
| `WMT_SOC.cfg` | 80 B | ASCII `coex_wmt_ant_mode=1 ...` | ✅ 是 |

`out/firmware_backup/etc/`, `etc/firmware/`, `etc/firmware/mt6627/`（FM patch 等）均为空目录。
`out/firmware_backup/bin/` 有 6620_launcher/wmt_loader/wlan_loader 等，**但全是 `/system/bin/linker`(bionic) 动态链接**，在 mainline/Alpine rootfs 上跑不了（`deploy-modules-sd.sh` 明确禁止用它们）。

**mt6627 子目录** (`firmware/mt6627/`): 只有 FM patch/coeff，与本轮 BT/WiFi 无关。

**NVRAM blob 搜索**: `find / -iname "*APRDEB*"`、`nvram-WIFI`、`/etc/firmware/nvram` 全 0 命中。**本地无 Wi-Fi NVRAM。**

---

## 3. wlan_gen2 请求的固件名（确切字符串）

文件: `/tmp/connsys_build/wlan/os/linux/gl_kal.c` `kalFirmwareOpen()` (L801-833) + `include/config.h` (L923-926):

```c
#define CFG_FW_FILENAME  "WIFI_RAM_CODE"           /* config.h L926 */
/* gl_kal.c L816-820 请求顺序: */
1. request_firmware(&prWlanFw, CFG_FW_FILENAME "_MT6582", prDev)  → "WIFI_RAM_CODE_MT6582"
2. request_firmware(&prWlanFw, aucFwName, prDev)                  → "WIFI_RAM_CODE_<chipinfo>"
3. request_firmware(&prWlanFw, CFG_FW_FILENAME, prDev)           → "WIFI_RAM_CODE"
```

`glGetChipInfo()` (`wlan/os/linux/hif/ahb/ahb.c` L537-557): MT6572 **不在** 显式 `%04x` 列表(6571/8127/6752/8163/6735/6580/6755/7623)中 → 走 `default: kalMemCopy(pucChipBuf,"SOC")` → **chipinfo = "SOC"** → fallback 名 = **`WIFI_RAM_CODE_SOC`**。

**结论**: wlan_gen2 首选 `WIFI_RAM_CODE_MT6582`，其次 `WIFI_RAM_CODE_SOC`，最后 `WIFI_RAM_CODE`。deploy 脚本把 `WIFI_RAM_CODE_SOC` 同时装成 MT6582 名和 SOC 名 → **前两个都命中，覆盖完全。**

加载时机: `kalFirmwareImageMapping` → 在 wlan 适配器 start 时（`gl_init.c` L2980 与 `nic_pwr_mgt.c` L452 两处调用）即 func-on 下载固件时。

---

## 4. mtk_stp_wmt_soc 的 WMT 固件下载路径

### 4.1 WMT 配置
`conn_soc/core/include/wmt_conf.h` L33-34:
```c
#define CUST_CFG_WMT        "WMT_SOC.cfg"
#define CUST_CFG_WMT_PREFIX "/system/etc/firmware/"
```
`wmt_conf_read_file()` (`wmt_conf.c` L480-520) → `wmt_dev_patch_get("/system/etc/firmware/WMT_SOC.cfg")` → `wmt_dev_read_file()` (`linux/pri/wmt_dev.c` L1445+) **filp_open 绝对路径读**（不是 request_firmware）。所以 WMT_SOC.cfg 必须在该绝对路径存在，否则 `osal_assert(0)`。

### 4.2 WMT patch 文件
`wmt_ctrl.c`:
- `wmt_ctrl_patch_search()` (L489) 向芯片发 `srh_patch` 命令；
- `wmt_ctrl_get_patch_info()` (L512) 从 `pDev->pWmtPatchInfo[i]` 拷出 patchName + address；
- `wmt_ctrl_get_patch()` (L683) 用 patchName 调 `wmt_dev_patch_get()` → filp_open 读文件。

**关键**: patch 文件名**不由内核自算**，由 **用户态 launcher** 通过 ioctl 喂给内核:
- `WMT_IOCTL_SET_PATCH_NAME` (`wmt_dev.c` L1856 `wmt_lib_set_patch_name`)
- `WMT_IOCTL_SET_PATCH_NUM` / `WMT_IOCTL_SET_PATCH_INFO` (`wmt_dev.c` L2115-2143)

`wmt_core.h` L44 默认 `defaultPatchName "mt66xx_patch_hdr.bin"` — 是旧 MT66xx 名字，**不是** MT6572 实际用的。

### 4.3 launcher 的命名逻辑 (`tools/launcher/stp_uart_launcher.c`)
`cmd_hdr_sch_patch()` (L498+): 对 MT6572 chipId (`0x6572`) → `chipName = "ROMv1"` + `"_patch"` → **搜索前缀 `ROMv1_patch`**（在 `-p` 指定的目录里 opendir/readdir），文件名匹配 + offset22 版本匹配 → `WMT_IOCTL_SET_PATCH_NAME` 传完整路径（含 `/system/etc/firmware/` 前缀）给内核。

**结论**: WMT patch 期望名 = `/system/etc/firmware/ROMv1_patch_{0,1}_hdr.bin`（deploy 脚本已用 `mt6572_82_patch_e1_*` 建好别名）✅。内核侧只认 launcher 喂进来的名字，路径前缀与目录须一致。

---

## 5. 缺口列表（按严重度排序）

### 5.1 🔴 mtk_stp_launcher 用户态守护进程 — 未编译、未入 ramdisk、deploy 脚本也不装
- 源码在 `/tmp/connsys_build/tools/launcher/stp_uart_launcher.c`（56,959 B），**全盘无编译产物**（`find / -name mtk_stp_launcher` 只中源码/目录）。
- 构建命令（README L90-92 + deploy-modules-sd.sh 注释）:
  ```
  arm-linux-gnueabihf-gcc -static -O2 -I tools/launcher \
      -o tools/launcher/mtk_stp_launcher tools/launcher/stp_uart_launcher.c
  ```
- `connsys-up.sh` 以 `./mtk_stp_launcher -m 3 -p /system/etc/firmware/` 启动。
- **为什么致命**: launcher 是 `srh_patch` 的应答者——芯片发 srh_patch 后，内核靠 launcher 的 `WMT_IOCTL_SET_PATCH_NUM/INFO` 才知道 patch 文件名。没 launcher → patchNum=0 → 芯片跑裸 ROM → **BT 仍工作（不需 patch），但 WiFi RAM code 在入口死**（README L104-106 明示）。当前 z1_modprobe_connsys.sh 只 insmod，**不启动 launcher**。

### 5.2 🔴 cfg80211.ko 缺失 — wlan_gen2 insmod 会失败
- `fork_linux/.config`: `CONFIG_CFG80211=m`，且 vmlinux/System.map **无任何 cfg80211 符号**（0 个 `cfg80211_init`/`wiphy_register`）→ **没编进内核**。
- wlan_gen2.ko `modinfo depends` = `cfg80211,mtk_stp_wmt_soc,mtk_wmt_wifi_soc`，`nm -u` 26 个未定义 cfg80211 符号（`cfg80211_connect_done` 等）。
- ramdisk **不含 cfg80211.ko**；本地能找到的旧 cfg80211.ko vermagic = `7.0.0-rc7 SMP mod_unload ARMv7 p2v8`（无 `-g2cd361bd40c7-dirty`），**与当前内核不匹配**，insmod 会拒载。
- 需: 在 fork_linux 里 `make M=net/wireless cfg80211.ko`（与当前内核同 vermagic）后放入 ramdisk `/root/connsys/cfg80211.ko`。z1_modprobe_connsys.sh L27-30 有尝试 insmod cfg80211.ko 的逻辑，只是文件缺失。

### 5.3 🟠 当前 boot img 内嵌 ramdisk 不含任何固件
- `out/ramdisk_mainline_connsys.gz`（已核对 md5 = boot img 内嵌 ramdisk）: **只有 5 个 .ko + z1_modprobe_connsys.sh，无 `/lib/firmware`、无 `/system/etc/firmware`、无 `/etc/firmware`**。
- 另有一个旧变体 `out/ramdisk_mainline_fw.gz`（2026-08-10，含 `lib/firmware/WIFI_RAM_CODE` + `system/etc/firmware/mt6572_82_patch_e1_*` + `WMT_SOC.cfg`），**但该变体不含任何 .ko，且没有被当前 boot img 使用**（md5 不匹配）。
- 含义: 上板 boot 后必须运行 `scripts/deploy_connsys_fw.sh`（ROOT=/）把固件现装进运行中的 rootfs，或把 fw ramdisk 与模块 ramdisk 合并成一个再打包。

### 5.4 🟠 Wi-Fi NVRAM 完全缺失
- wlan 驱动读 `WIFI_NVRAM_FILE_NAME "/etc/firmware/nvram/WIFI"` (`wlan/os/linux/platform.c` L124，`kalCfgDataRead16` L460)。
- 本地**无任何 NVRAM 提取产物**（stock 存在 `/data/nvram/APCFG/APRDEB/WIFI`）。
- 无 NVRAM 后果: 固件每次随机 MAC，netdev MAC 与 per-BSS 收包过滤不一致 → **所有单播数据帧被丢（EAPOL 也丢）**，管理帧正常（README/`wifi-fw-extract.sh` 注释）。WiFi 能 scan/assoc 但 DHCP/数据不通。
- 需: 从 stock `/data` 分区提取 514B blob（`wifi-fw-extract.sh` 的 NVSRC 路径），或后续单独立项。

### 5.5 🟡 设备节点 major 号不一致（z1_modprobe_connsys.sh）
- 脚本建: `/dev/stpbt c 191 0`，但驱动 `stp_chrdev_bt.c` L47 `#define BT_DEV_MAJOR 192` → **/dev/stpbt 应建 c 192 0**。BT HCI 会 open 失败（或错节点）。
- `/dev/stpwmt c 190 0` ✅ (WMT_DEV_MAJOR 190)、`/dev/wmtWifi c 195 0` 但驱动 `wmt_chrdev_wifi.c` L39 `WIFI_DEV_MAJOR 155` → **/dev/wmtWifi 应建 c 155 0**。`echo 1 > /dev/wmtWifi` 会 open 到错误 major。
- connsys-up.sh 用的是 `mknod /dev/stpbt c 192 0`（正确），z1 专用脚本抄错了。非固件问题但会挡 func-on。

### 5.6 🟡 WMT_SOC.cfg 内容核对
`WMT_SOC.cfg` 只含 `coex_wmt_ant_mode=1` / `wmt_gps_lna_*` / `co_clock_flag=1`，wmt_conf 解析字段表（`wmt_conf.c` L117-159）能覆盖 ✅。

---

## 6. func-on 前后各需要什么

### 6.1 func-on 之前（加载 + 启动 launcher）
1. insmod `mtk_btif_drv.ko` → `mtk_stp_wmt_soc.ko`（此步做 WMT init，**读 `/system/etc/firmware/WMT_SOC.cfg`**，缺则 assert）→ `mtk_stp_bt_soc.ko`（BT 通道）。
2. **启动 `mtk_stp_launcher -m 3 -p /system/etc/firmware/`**（缺则 WMT patch 无人喂）。
3. insmod `mtk_wmt_wifi_soc.ko`（建 /dev/wmtWifi, major 155）→ 建节点。
4. insmod `cfg80211.ko` → `wlan_gen2.ko`（缺 cfg80211 则 wlan_gen2 失败）。
5. 确认 `/lib/firmware/WIFI_RAM_CODE_MT6582` 存在（deploy 脚本已装）。
6. 设备节点 major 修正: stpbt=192, wmtWifi=155, stpwmt=190。

### 6.2 func-on (`echo 1 > /dev/wmtWifi`)
- mtk_wmt_wifi_soc → `mtk_wcn_wmt_func_on(WMTDRV_TYPE_WIFI)` → WMT 下载 patch（launcher 提供名字 + 内核 filp_open 读 `/system/etc/firmware/ROMv1_patch_*`）→ wlan probe → `kalFirmwareImageMapping` request_firmware `WIFI_RAM_CODE_MT6582` → AHB HIF @0x180F0000 下发 → `wlan0` 出现。
- 若 launcher 没起: patch 数为 0，WiFi RAM code 入口死（BT 不受影响）。
- func-on 后: `ip link set wlan0 up` 需 NVRAM（缺 → 随机 MAC + 单播 RX 丢帧）。

### 6.3 BT func-on
- BT 通道 `/dev/stpbt`(192) → BlueZ/HCI。**BT 不需要 patch 也能工作**，但正常仍走同一 WMT patch 下载。

---

## 7. 建议下一步（按依赖顺序）

1. 编译 `mtk_stp_launcher`（静态，见 §5.1 命令）并加入 ramdisk `/root/connsys/`；改 z1_modprobe_connsys.sh 启动它（`-p /system/etc/firmware/`）。
2. 在 fork_linux 编 `cfg80211.ko`（同 vermagic）并加入 ramdisk `/root/connsys/`。
3. 修正 z1_modprobe_connsys.sh 设备节点 major（stpbt=192, wmtWifi=155）。
4. 把固件（`deploy_connsys_fw.sh` 部署的 4 类文件）合并进 ramdisk，或上板后先跑 deploy 脚本再 func-on。
5. 单独立项: 从 stock `/data` 提取 Wi-Fi NVRAM (`nvram/APCFG/APRDEB/WIFI`)。

---

## 附: 关键文件路径索引

- deploy 脚本: `/home/<user>/claude/6572/fork_linux/scripts/deploy_connsys_fw.sh`
- 固件 blob 源: `/home/<user>/claude/6572/out/firmware_backup/firmware/`
- 模块源码: `/tmp/connsys_build/{btif,conn_soc,wlan}/`
- 固件请求代码: `/tmp/connsys_build/wlan/os/linux/gl_kal.c` (L801-833), `/tmp/connsys_build/wlan/include/config.h` (L923-926), `/tmp/connsys_build/wlan/os/linux/hif/ahb/ahb.c` (L537-557)
- WMT 固件路径: `/tmp/connsys_build/conn_soc/core/include/wmt_conf.h` (L33-34), `/tmp/connsys_build/conn_soc/core/wmt_conf.c` (L480-520), `/tmp/connsys_build/conn_soc/core/wmt_ctrl.c` (L489-730), `/tmp/connsys_build/conn_soc/linux/pri/wmt_dev.c` (L1445-1560)
- launcher 源码: `/tmp/connsys_build/tools/launcher/stp_uart_launcher.c` (L498-830)
- bring-up 脚本: `/tmp/connsys_build/tools/{connsys-up,wifi-up,deploy-modules-sd}.sh`
- ramdisk: `/home/<user>/claude/6572/out/ramdisk_mainline_connsys.gz` (当前 boot img 内嵌, md5 `eabc977f...`) / `ramdisk_mainline_fw.gz` (旧固件变体, 未使用)
- boot img: `/home/<user>/claude/6572/bridge_backups/products/products_connsys_modules_rebuilt_20260814_004806/boot_mainline_z1_connsys_mod.img`
- 历史固件审计: `/home/<user>/claude/6572/bridge_backups/wifi_bt_resource_audit_pre_doc_20260718_202342/WIFI_FW_BLOB.md`
