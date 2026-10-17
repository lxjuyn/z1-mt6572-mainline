# Wi-Fi NVRAM 提取方案 — Z1 (MT6572) mainline (2026-08-14)

> 只读分析。板子关机，未碰硬件、未改任何文件。产出本方案。
> 结论先行：**本地就有 Z1 的 Wi-Fi NVRAM 源** —— `/home/<user>/claude/6572/1/NVRAM`（stock 5MB NVRAM 分区镜像）里 offset `0x20000` 处就是 `/data/nvram/APCFG/APRDEB/WIFI` 的内容（512B `WIFI_CFG_PARAM_STRUCT`，MT6628 版本 0x0104，真实 MAC `<MAC>`）。
> 这纠正了 `CONNSYS_FW_READINESS_20260814.md` §5.4 的结论——它用 `find / -iname "*APRDEB*"` 只搜文件名，没解析原始分区镜像，误判"本地无 Wi-Fi NVRAM"。

---

## 0. 结论一句话

Z1 的 Wi-Fi NVRAM 完整存在于本地，可直接提取部署，无需上板从 `/data/nvram` 提取（上板提取命令在 §6 作为兜底给出）。

---

## 1. 本地 NVRAM 相关备份搜索（任务点 1/2）

`find /home/<user>/claude/6572/` 下搜 `nvram` / `APCFG` / `APRDEB` / `WIFI` 结果：

| 位置 | 内容 | 判定 |
|---|---|---|
| `/home/<user>/claude/6572/1/NVRAM` | 5,242,880 B 原始分区镜像（file: `data`），内含 `/data/nvram/APCFG/APRDEB/WIFI` 路径记录 + WIFI 数据 | ✅ **本次关键发现，真实数据源** |
| `/home/<user>/claude/6572/1/PROTECT_F` | ext4（10MB），只有 `md/` modem 校准 | ❌ 无 Wi-Fi nvram |
| `/home/<user>/claude/6572/1/PROTECT_S` | ext4（10MB），同 PROTECT_F | ❌ 无 Wi-Fi nvram |
| `/home/<user>/claude/6572/1/userdata.img` | ext4（2.6GB，`/data` 分区） | ⚠️ `/data/nvram` 目录**为空**（stock 上它是 NVRAM 分区的挂载点）；`/data/BT_Addr`（64B）在，与 NVRAM 分区里的 BT 数据是两份不同快照 |
| `/home/<user>/claude/6572/1/system.img` | ext4（900MB） | 只有 `system/etc/firmware/*`（WIFI_RAM_CODE / patch / WMT_SOC.cfg），无 nvram |
| `/home/<user>/claude/6572/out/firmware_backup/` | system 提取的固件 blob | 无 nvram（`etc/wifi/`、`etc/firmware/` 均为空目录） |
| `wifi_recon/` / `bridge_backups/` / `mainline_recon/` | MTK 内核源码、审计报告 | 只有 nvram 的**定义/文档**（CFG_WIFI_Default.h、CONNSYS_FW_READINESS_20260814.md），无数据 |

**要点**：MT6572 上 `/data/nvram` 不是普通目录而是 NVRAM 分区（`/emmc@nvram`）的挂载点，所以 userdata 镜像里是空的；权威数据在 NVRAM 分区镜像本身。stock Android 的 nvram daemon 把每个 custom LID 的**原始 struct 字节**直接存进分区（无文件系统头），用路径字符串 `/data/nvram/APCFG/APRDEB/WIFI` 当 key。

---

## 2. NVRAM 分区布局（实测解析）

`/home/<user>/claude/6572/1/NVRAM`（5MB = 0x500000）：

```
0x000000-0x001614  LID 索引副本 1（40 条记录 × 140B，每条=路径+运行期元数据）
0x020000-0x0201ff  ★ WIFI 数据（512B WIFI_CFG_PARAM_STRUCT）← 唯一一份，签名搜索全盘仅 1 处
0x020200-...       BT_Addr 等其余 LID 数据 + custom LID 名称表（000WIFI、000WIFI_CUSTOM…）+ 加密校准块
0x440069-0x441681  LID 索引副本 2（头带 `dd cc bb aa` magic，备份）
0x4c0000-...       nvram daemon 事件日志区（`{{ZZ` 记录 + "Fri Jan 1 08:00:12 2010 / Enter clean boot, Restore From"）
```

索引记录尾部字段（两份副本一致，`07 00 00 00` 常量 + 两 u32）：
```
WIFI:         07 00 00 00 | 02 02 00 00 | 42 00 00 00   ← 0x202 = 514（见 §4 尺寸讨论）
BT_Addr:      07 00 00 00 | 44 02 00 00 | 06 00 00 00
WIFI_CUSTOM:  07 00 00 00 | 4a 02 00 00 | 4e 00 00 00
```
（这些字段是 nvram daemon 运行期结构，不能直接当数据偏移用；WIFI 数据以实测 offset `0x20000` 为准。）

---

## 3. WIFI 数据内容验证（512B，逐字段核对通过）

从 `0x20000` 取 512 字节（`/tmp/wifi_nvram_512.bin` 验证产物）：

| 偏移 | 字段 | 值 | 对照 |
|---|---|---|---|
| 0 | u2Part1OwnVersion | `0x0104` | = `CFG_WIFI_Default.h` MT6628 默认 |
| 2 | u2Part1PeerVersion | `0x0000` | = 默认 |
| 4 | aucMacAddress | `<MAC>` | **真实出厂 MAC（默认是全 0）** |
| 10 | aucCountryCode | `00 00` | = 默认 |
| 12-21 | rTxPwr (CCK/DSSS/OFDM/HT20/HT40) | `26 26 00 00` + `20`×12 + `1E`×6 | = 默认 |
| 196-339 | aucEFUSE[144] | 全 0 | = 默认（真实 eFuse 由芯片运行时读） |
| 196+ | ucTxPwrValid / support5G / bandedge | `01 00 01` | = 默认 |
| 199+ | cBandEdgeMaxPwrCCK/OFDM20/OFDM40 | `26 1E 1A` | = 默认 |
| 204 | ucRegChannelListMap / Index | `00 01` | 默认为 00/00 |
| 256-511 | Part2（Own/Peer ver + 保留） | 全 0 | 保留区未写 |

md5 = `a6182f634a2554bc881e87fdf111ecd1`，sha256 = `d28c5264265f5705d0c82e52d9f4d2e6841a71e23593e96d70cafbd7918e9239`
下一块数据起始于 `0x20200`（`aa a4...`），故该 blob 精确为 **512B**，无歧义边界。

---

## 4. 尺寸：512 vs 514（README 写 514B）

- **wlan_gen2 驱动**：`/tmp/connsys_build/wlan/include/CFG_Wifi_File.h` L230 `DATA_STRUCT_INSPECTING_ASSERT(sizeof(WIFI_CFG_PARAM_STRUCT) == 512)`；读法见 `gl_init.c` `glLoadNvram()`——按 `kalCfgDataRead16(offset)` 逐字段读 0~511，首个校验点读 offset 510（`sizeof - 2`）成功即 `fgNvramAvailable=TRUE`。
- **README**（`/tmp/connsys_build/README.md` L110）写 "514 B"；分区索引 WIFI 记录尾字段 `0x202` = 514。
- **结论**：stock `/data/nvram/APCFG/APRDEB/WIFI` 大概率 = 512B struct + 2B 尾缀（nvram daemon 校验/保留字节），但**驱动只读 0~511，多出的 2B 不被读**。部署 512B struct 完全正确；若日后某环节严格要 514，可在末尾补 2 个 0 字节（本方案不预做）。

---

## 5. 提取与部署方案（可执行）

### 5.1 数据源
`/home/<user>/claude/6572/1/NVRAM`（stock 全量 eMMC 备份的 NVRAM 分区，零修改原版）。

### 5.2 切出 512B blob
```bash
dd if=/home/<user>/claude/6572/1/NVRAM bs=1 skip=$((0x20000)) count=512 \
   of=/home/<user>/claude/6572/mainline_recon/wifi-nvram-WIFI.bin
# 校验（与 §3 一致）:
md5sum    wifi-nvram-WIFI.bin   # a6182f634a2554bc881e87fdf111ecd1
xxd -l 16 wifi-nvram-WIFI.bin   # 04 01 00 00 20 72 0d 39 0d 1f 00 00 26 26 00 00
```

### 5.3 放入 ramdisk 的 `/etc/firmware/nvram/WIFI`
当前 boot img 内嵌 ramdisk `out/ramdisk_mainline_connsys.gz`（MTK ROOTFS 头 512B + gzip cpio body）**没有 `/etc/` 目录**。用现有 `fork_linux/scripts/repack_ramdisk_connsys.sh` 同一流程在打包前加一步（**注意：本任务只读，未改脚本，以下为将来执行时的改法**）：

```bash
# 在 repack_ramdisk_connsys.sh 的 §3.5 固件段后追加:
mkdir -p "$TMP/rd/etc/firmware/nvram"
cp /home/<user>/claude/6572/mainline_recon/wifi-nvram-WIFI.bin "$TMP/rd/etc/firmware/nvram/WIFI"
echo "  固件 nvram WIFI (512B, MAC <MAC>)"
```
`$TMP/rd/etc/firmware/nvram/WIFI` 即驱动 `WIFI_NVRAM_FILE_NAME`（`wlan/os/linux/platform.c` L124）读取路径。重打包后 ramdisk body 变大，脚本自带的 initrd-end 同步逻辑会自动处理 dts。

等价备选（不改 ramdisk，上板后现装，需在 func-on 之前）：
```bash
mkdir -p /etc/firmware/nvram
cp /root/wifi-nvram-WIFI.bin /etc/firmware/nvram/WIFI   # 或任一路径拷入
```
以及 SD 分期部署口径：`deploy-modules-sd.sh` 期望 `tools/wifi-fw/nvram-WIFI`（该 staging 目录当前不存在），可直接把 §5.2 产物放到 `/tmp/connsys_build/tools/wifi-fw/nvram-WIFI`。

### 5.4 上板验证
```bash
# 正常 bring-up 后（z1_modprobe_connsys.sh → echo 1 > /dev/wmtWifi）:
ip link set wlan0 up
cat /sys/class/net/wlan0/address        # 应 = <MAC>（持久，不再随机）
# 再验证 nvram 生效 + 收包正常:
iw dev wlan0 scan | grep -E 'BSS|SSID'   # scan 正常
# dmesg 应无 nvram 相关错误; 单播 DHCP/数据帧不再丢
```
判定标准：**wlan0 MAC 固定为 <MAC>**，且与 `ip addr` 显示一致 = NVRAM 已被驱动读取（`glLoadNvram` → `fgNvramAvailable=TRUE`）。

---

## 6. 兜底：上板从 `/data/nvram` 提取（本地源丢失时）

本地源有效时不需要；若备份丢失，可上板用 `wifi-fw-extract.sh`（其 `NVSRC="nvram/APCFG/APRDEB/WIFI"`，§"--- stock /data ---" 段）：mainline rootfs 里 mount stock 各分区 → 找到含 `nvram/APCFG/APRDEB/WIFI` 的 ext4 → 拷到 `/etc/firmware/nvram/WIFI` + `/root/fw-extract/nvram-WIFI`。手写等价命令：
```bash
DEVPFX=/dev/mmcblk1p   # SD 启动时 eMMC 为 mmcblk1; 直启动则为 mmcblk0
for p in 4 5 6 7 2 3 8 9; do
  mount -t ext4 -o ro $DEVPFX$p /mnt 2>/dev/null || continue
  [ -f /mnt/nvram/APCFG/APRDEB/WIFI ] && { cp /mnt/nvram/APCFG/APRDEB/WIFI /etc/firmware/nvram/WIFI; umount /mnt; break; }
  umount /mnt
done
wc -c /etc/firmware/nvram/WIFI   # 应为 512（或 514）
```
> 注意 stock 上该文件在 NVRAM 分区挂载后出现；若 `/data/nvram` 仍为空，需先确认 NVRAM 分区已挂载（`mount | grep nvram`）。

---

## 7. 相关文件索引

- 数据源：`/home/<user>/claude/6572/1/NVRAM`（WIFI @ 0x20000，512B）
- 驱动读取路径：`/tmp/connsys_build/wlan/os/linux/platform.c` L124 `#define WIFI_NVRAM_FILE_NAME "/etc/firmware/nvram/WIFI"`
- 驱动 struct：`/tmp/connsys_build/wlan/include/CFG_Wifi_File.h`（L230 sizeof==512 assert）
- 驱动读 nvram：`/tmp/connsys_build/wlan/os/linux/gl_init.c` `glLoadNvram()` L1168+
- 提取脚本：`/tmp/connsys_build/tools/wifi-fw-extract.sh`（NVSRC 段）
- 部署脚本：`/tmp/connsys_build/tools/deploy-modules-sd.sh`（期望 `tools/wifi-fw/nvram-WIFI`）、`fork_linux/scripts/deploy_connsys_fw.sh`
- ramdisk 打包：`fork_linux/scripts/repack_ramdisk_connsys.sh`（当前产物 `out/ramdisk_mainline_connsys.gz` 无 `/etc/firmware/nvram`）
- 对照默认值：`wifi_recon/mt6572_kernel_drsami/mediatek/custom/cci72_we_jb3/cgen/cfgdefault/CFG_WIFI_Default.h`
- 此前误判记录：`/home/<user>/claude/6572/mainline_recon/CONNSYS_FW_READINESS_20260814.md` §5.4（"本地无 Wi-Fi NVRAM"，已被本次纠正）
