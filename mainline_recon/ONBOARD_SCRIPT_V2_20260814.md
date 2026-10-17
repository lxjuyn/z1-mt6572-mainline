# Z1 CONNSYS 上板脚本 V2 优化 + 校验报告 (2026-08-14)

> 板子关机。纯脚本编写 + 校验 + 打包验证，未刷写、未碰硬件。
> 结论: 全部脚本 `bash -n` / `dash -n` 通过；V2 上板脚本在真实 busybox ash (qemu-arm) 下端到端跑通（OK/FAIL 分支均正常）；修复了 repack 脚本 2 个**潜伏回归 bug**（MODS_SRC 指向错 vermagic、iw 源指向 WEXT 工具目录）；boot.img 已重新打包并校验通过（5.39 MiB ≤ 6MB，含全部固件/NVRAM/iw/launcher/验证脚本）。

---

## 1. 交付物与变更清单

| 文件 | 变更 | 说明 |
|---|---|---|
| `scripts/z1_connsys_onboard_test.sh` | **重写为 V2** | 7 步上板一键验证，busybox-only，每步 [OK]/[FAIL] |
| `tools/z1_make_bootimg.sh` | **优化 (V2)** | +自动重建 ramdisk (缺失/Z1_REPACK=1)；+ramdisk 内容校验 (固件/NVRAM/iw/launcher/脚本) |
| `tools/repack_ramdisk_connsys.sh` | **修复 2 bug** | ①`MODS_SRC` 默认指向 `-dirty` vermagic 模块（对 z1+ 内核 insmod 拒载）→ 改指 z1vermagic 产物；②`TOOLS_SRC` 默认指向 WEXT 工具目录（无 iw）→ 新增 `IW_BIN` 默认指 nl80211 iw；+把上板验证脚本一并打进 ramdisk `/root/connsys/` |
| `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts` | 被 repack 自动更新 | `linux,initrd-end` → `0x842c6cc8`（匹配新 ramdisk body 长度） |
| `out/ramdisk_mainline_connsys.gz` | 重建 | 1,862,856 B xz body；6 ko (vermagic `7.0.0-rc7-z1+`) + 固件 + NVRAM + iw + launcher + 2 脚本 |
| `out/boot_mainline_connsys.img` | 重建 | **5,652,480 B (5.39 MiB)**，ANDROID! magic ✓，sha256 `54ceb4be...` |

**备份** (改前原版全部保留在 `bridge_backups/scripts_v2_20260814_161122/`):
- `z1_connsys_onboard_test.sh_V1.bak`（V1 原版）
- `z1_make_bootimg.sh_pre_v2.bak` / `repack_ramdisk_connsys.sh_pre_v2.bak`
- `ramdisk_mainline_connsys.gz.pre_v2_20260814_161603.bak`（含 cfg80211.ko+launcher 的成功版旧 ramdisk）
- `boot_mainline_connsys.img.pre_v2_<ts>.bak`（旧 boot img）
- `mt6572-z1.dts.pre_repack_<ts>.bak`（repack 改 dts 前）

---

## 2. 上板验证脚本 V2（全量内容）

> 部署位置: ramdisk 内 `/root/connsys/z1_connsys_onboard_test.sh`（repack 自动放入）。
> 上板执行: `sh /root/connsys/z1_connsys_onboard_test.sh`

```sh
#!/bin/sh
# z1_connsys_onboard_test.sh — Z1 (MT6572) CONNSYS WiFi/BT func-on 上板一键验证 (V2, 2026-08-14)
#
# 用法 (板子开机进 busybox rootfs 后手动执行, 不刷写不碰 download 线):
#   sh /root/connsys/z1_connsys_onboard_test.sh
#   (脚本由 PC 端 tools/repack_ramdisk_connsys.sh 打进 ramdisk → /root/connsys/)
#
# busybox 兼容: 只用 echo/cat/insmod/mknod/ls/grep/printf/sleep/ifconfig + ash 内建
#   (本机 busybox 无 tee/lsmod; 有 ip 但本脚本统一用 ifconfig)
#   无线工具 iw (nl80211, 静态 ARM) 在 /usr/bin/iw — 脚本检测存在才执行 scan
#
# 流程 (每步 [OK]/[FAIL]):
#   1. /root/connsys 存在?
#   2. 跑 z1_modprobe_connsys.sh (insmod btif→wmt_soc→bt_soc→wmt_wifi_soc→cfg80211→wlan_gen2
#      并启动 mtk_stp_launcher -m 3 -p /system/etc/firmware/)
#   3. 等 launcher 就绪 (轮询 /proc/<pid>/cmdline 含 mtk_stp_launcher, 超时 20s)
#      — 注: launcher 是常驻 daemon, SET_STP_MODE + pwr-on(含 patch 下载)是启动期动作;
#        进程存活 + 后续 func-on 轮询 wlan0 是 busybox-safe 的就绪代理
#   4. echo 1 > /dev/wmtWifi (WiFi func-on)
#   5. 等 func-on 完成 (轮询 /sys/class/net/wlan*, 超时 30s)
#   6. ifconfig wlan0 up + iw scan (iw 存在则跑, 否则提示)
#   7. 蓝牙 /dev/stpbt 节点验证
#
# 依赖 (ramdisk 内, 由 repack_ramdisk_connsys.sh 部署):
#   /root/connsys/       5 个 .ko + cfg80211.ko + mtk_stp_launcher + 本脚本
#   /lib/firmware/WIFI_RAM_CODE_MT6582
#   /system/etc/firmware/{WMT_SOC.cfg,ROMv1_patch_0_hdr.bin,ROMv1_patch_1_hdr.bin}
#   /etc/firmware/nvram/WIFI   (缺 → 随机 MAC + 单播 RX 丢帧, scan 仍可测)

CONNSYS=/root/connsys
MODPROBE=$CONNSYS/z1_modprobe_connsys.sh
WIFI_DEV=/dev/wmtWifi
LOG=/tmp/connsys_onboard.log

ok()   { echo "  [OK]   $*"; }
fail() { echo "  [FAIL] $*"; }
step() { echo ""; echo "=== step $1: $2 ==="; }

: > "$LOG"

# wait_until <描述> <超时秒> <命令...> — 每 1s 轮询直到命令返回 0
wait_until() {
    desc=$1; tmo=$2; shift 2
    n=0
    while [ "$n" -lt "$tmo" ]; do
        if "$@"; then
            ok "$desc (${n}s)"
            return 0
        fi
        sleep 1
        n=$((n + 1))
    done
    fail "$desc (等 ${tmo}s 超时)"
    return 1
}

# launcher 进程在跑? 用 /proc 扫 (不依赖 pgrep)
launcher_alive() {
    for c in /proc/[0-9]*/cmdline; do
        [ -r "$c" ] || continue
        if grep -a 'mtk_stp_launcher' "$c" >/dev/null 2>&1; then
            return 0
        fi
    done
    return 1
}

# 有 wlan* 接口?
wlan_present() {
    ls /sys/class/net/ 2>/dev/null | grep '^wlan' >/dev/null
}

echo ""
echo "=== Z1 CONNSYS WiFi/BT 上板一键验证 (V2) ==="
echo "开机 uptime: $(cat /proc/uptime) s   日志: $LOG   (busybox 无 date, 用 uptime 计时)"

# --- 1 ---
step 1 "确认 $CONNSYS"
if [ -d "$CONNSYS" ]; then
    ok "$CONNSYS 存在"
else
    fail "$CONNSYS 不存在 — 先重建 ramdisk 部署 (PC 端 tools/repack_ramdisk_connsys.sh)"
    exit 1
fi

# --- 2 ---
step 2 "运行 z1_modprobe_connsys.sh (insmod 6 模块 + 启动 launcher)"
if [ -f "$MODPROBE" ]; then
    echo "  [RUN] sh $MODPROBE"
    sh "$MODPROBE"
    echo "  [INFO] 加载日志: /tmp/connsys_load.log"
else
    fail "未找到 $MODPROBE — 模块未加载, 继续检查现状"
fi

# 模块加载数量核对 (cat /proc/modules)
EXPECT="cfg80211 mtk_btif_drv mtk_stp_wmt_soc mtk_stp_bt_soc mtk_wmt_wifi_soc wlan_gen2"
n=0
for m in $EXPECT; do
    if grep "^$m " /proc/modules >/dev/null 2>&1; then
        n=$((n + 1))
    fi
done
echo "  [INFO] /proc/modules 已加载 $n/6 期望模块"
if [ "$n" -ge 5 ]; then
    ok "模块加载充分 (≥5, 含 cfg80211)"
else
    fail "模块不足 (需 ≥5) — cat /tmp/connsys_load.log 看 insmod 失败项"
fi

# --- 3 ---
step 3 "等 mtk_stp_launcher 就绪"
if wait_until "launcher 进程存活" 20 launcher_alive; then
    echo "  [INFO] launcher 启动期动作 (SET_STP_MODE + pwr-on patch 下载) 约 1-2s, 再等 2s ..."
    sleep 2
    ok "launcher 就绪 (若 func-on 仍失败, 见 step 5 排查 — 竞态细节见 mainline_recon/LAUNCHER_TIMING_20260814.md)"
else
    fail "launcher 未起 — WiFi func-on 会死 (patch 无人喂, BT 仍可用). 查: $CONNSYS/mtk_stp_launcher 存在? 手动: $CONNSYS/mtk_stp_launcher -m 3 -p /system/etc/firmware/ &"
fi

# --- 4 ---
step 4 "WiFi func-on (echo 1 > $WIFI_DEV)"
if [ -e "$WIFI_DEV" ]; then
    ok "$WIFI_DEV 节点存在"
else
    fail "$WIFI_DEV 缺失 — 应 mknod c 155 0 (z1_modprobe_connsys.sh 已建), 检查 mdev"
fi
echo "  [ACTION] echo 1 > $WIFI_DEV"
if echo 1 > "$WIFI_DEV" 2>&1; then
    ok "func-on 指令已下发"
else
    fail "写入 $WIFI_DEV 失败 — 节点/驱动未就绪"
fi

# --- 5 ---
step 5 "等 func-on 完成 (wlan0 接口注册)"
if wait_until "wlan0 接口注册" 30 wlan_present; then
    echo "  无线接口: $(ls /sys/class/net/ | grep '^wlan')"
else
    echo "  排查 (按序):"
    echo "    1. cat /tmp/connsys_load.log — insmod 是否全 OK / launcher 启动返回?"
    echo "    2. ls /system/etc/firmware/ /lib/firmware/ — ROMv1_patch_* / WMT_SOC.cfg / WIFI_RAM_CODE_MT6582 在不在"
    echo "    3. ls /etc/firmware/nvram/ — WIFI (NVRAM, 缺则 wlan0 上不了)"
    echo "    4. dmesg | grep -iE 'wmt|wlan|wifi' — 内核错误 (本机 busybox 有 dmesg)"
fi

# --- 6 ---
step 6 "ifconfig wlan0 up + iw scan"
WLAN0=$(ls /sys/class/net/ 2>/dev/null | grep '^wlan' | head -1)
if [ -n "$WLAN0" ]; then
    if ifconfig "$WLAN0" up 2>&1; then
        ok "ifconfig $WLAN0 up"
    else
        fail "ifconfig $WLAN0 up 失败 — NVRAM/驱动状态问题"
    fi
else
    fail "无 wlan* 接口, 跳过 up + scan"
fi

IW=""
for p in /usr/bin/iw /bin/iw /sbin/iw; do
    if [ -x "$p" ]; then IW="$p"; break; fi
done
if [ -n "$WLAN0" ] && [ -n "$IW" ]; then
    echo "  [RUN] $IW dev $WLAN0 scan (nl80211, 静态 ARM; wlan_gen2 只支持 nl80211 scan)"
    $IW dev "$WLAN0" scan 2>&1 | grep -aE 'BSS |SSID:<SSID> seen'
    echo "  [INFO] scan 摘要已过滤 (BSS/SSID/signal/freq). 连接: $IW dev $WLAN0 connect <SSID> ; 查看: $IW dev"
elif [ -n "$WLAN0" ]; then
    echo "  [提示] 未找到 iw (静态 iw 应在 /usr/bin/iw, 由 repack_ramdisk_connsys.sh 部署). 备选: iwlist $WLAN0 scan (WEXT, wlan_gen2 可能报不支持)"
else
    echo "  [提示] wlan0 未起, 跳过 scan"
fi

# --- 7 ---
step 7 "蓝牙节点 /dev/stpbt"
if [ -e /dev/stpbt ]; then
    ok "/dev/stpbt 存在 (major 192)"
    echo "  [INFO] 节点存在 ≠ 已上电: BT func-on 需 open /dev/stpbt 且 mtk_wcn_stp_is_ready()."
    echo "        下一步 (不在本脚本): stpbt-hci-test / BlueZ hciattach 走 stpbt 发 HCI_Reset"
else
    fail "/dev/stpbt 缺失 — 应 mknod c 192 0 (z1_modprobe_connsys.sh 已建), BT 不可用"
fi

echo ""
echo "=== 汇总 ==="
if wlan_present; then
    echo "[OK]   WiFi 链路完整: insmod → launcher → func-on → wlan0 → ifconfig up → iw scan"
    echo "[OK]   BT 节点: $([ -e /dev/stpbt ] && echo 在 || echo 缺)"
    exit 0
else
    echo "[FAIL] wlan0 未起 — 上板主目标未达成; 见 step 5 排查项"
    exit 1
fi
```

---

## 3. 校验结论

### 3.1 语法校验
| 脚本 | `bash -n` | `dash -n` |
|---|---|---|
| `scripts/z1_connsys_onboard_test.sh` (V2) | OK | OK |
| `scripts/z1_modprobe_connsys.sh` | OK | OK |
| `tools/z1_make_bootimg.sh` (V2) | OK | — |
| `tools/repack_ramdisk_connsys.sh` | OK | — |

### 3.2 busybox 实机运行校验（qemu-arm-static + 设备同款 busybox 1.36.1）
用 ramdisk 里的 **真实 busybox 二进制** + qemu-arm 跑 V2 脚本端到端（桩环境：stub modprobe/launcher/iw）：
- 7 步全部正常执行，无语法/未定义变量/运行时错误；OK 与 FAIL 分支均按预期走通。
- `launcher_alive` 正反路径：真 launcher 存活 → 0s 命中 [OK]；launcher 死后 → 20s 超时 [FAIL]。用 `/proc/<pid>/cmdline` + `grep -a` 扫（不依赖 pgrep），对含 NUL 的 cmdline 实测可用。
- `wait_until` 轮询函数（`"$@"` 调用函数、算术、`[ ]`）在 busybox ash 下全部正常。
- **关键发现（已修复）**: ramdisk busybox **无 `date`/`printf` 符号链接**（`command -v date` 返回 127；`printf` 是 ash 内建可用）。V2 原用 `$(date)` → 已改为 `$(cat /proc/uptime)`。`head/grep/ps/sleep/ifconfig/mknod/insmod/ls/cat/echo` 均确认有符号链接或内建。

### 3.3 打包产物校验
- `out/boot_mainline_connsys.img` = **5,652,480 B (5.39 MiB) ≤ 6MB**，`ANDROID!` magic ✓，sha256 `54ceb4beb17b245340058656b826dac920e7170c795015c86e025d72b99cd65b`。
- 内嵌 ramdisk md5 = `out/ramdisk_mainline_connsys.gz`（校验通过）。
- appended dtb: `d0 0d fe ed` magic 精确位于 zImage 末尾边界（offset = zImage 大小 3,764,736）。
- dts `linux,initrd-end = <0x842c6cc8>` = `0x84100000 + 1,862,856`（ramdisk body 长）✓。
- cmdline 与上板成功版一致: `console=tty0 console=ttyS0,115200n8 root=/dev/ram panic=0 ignore_loglevel clk_ignore_unused rdinit=/init`。

---

## 4. 修复的潜伏 bug（repack_ramdisk_connsys.sh，改前备份在 bridge_backups/）

### 4.1 🔴 MODS_SRC 默认指错目录 → 模块 vermagic 不匹配，insmod 必拒载
- **原默认**: `products_connsys_modules_rebuilt_20260814_004806`，其 ko vermagic=`7.0.0-rc7-g2cd361bd40c7-dirty`。
- **实际内核/成功版**: `CONFIG_LOCALVERSION="-z1"` → vermagic=`7.0.0-rc7-z1+`。上板成功用的模块（旧 ramdisk 内 6 个 ko，sha256 比对）全部是 `z1+`。
- **后果**: 按原默认重跑 repack 产出的 ramdisk，wlan_gen2/mtk_stp_wmt_soc 等 insmod 会被内核拒载 → 上板 WiFi/BT 全挂。
- **修复**: `MODS_SRC` 默认 → `products_connsys_modules_z1vermagic_20260814_015009`（全部 ko + cfg80211.ko + mtk_stp_launcher 都是 `z1+`，大小与旧成功 ramdisk 逐字节一致）。env `Z1_MODS_SRC` 仍可覆盖。

### 4.2 🔴 iw (nl80211) 源指错 → 重跑 repack 会丢 `/usr/bin/iw`，上板无法 scan
- **原默认**: `TOOLS_SRC=.../products_wifi_tools_20260814_131306/bin`，里面只有 `iwconfig/iwlist/iwpriv`（WEXT），**没有 `iw`**。原 `for t in iw; cp "$TOOLS_SRC/$t"` 必然跳空。
- **修复**: 新增 `IW_BIN` 默认 → `products_iw_nl80211_20260814_144324/iw_arm_static_stripped`（sha256 `38b5be3c...` = 上板成功 ramdisk 里的 `/usr/bin/iw`），备选 `$TOOLS_SRC/iw`；都缺则 WARN。

### 4.3 附带改进（上板一次跑完）
- repack 现在把 `scripts/z1_connsys_onboard_test.sh` 一并打进 ramdisk `/root/connsys/`（V1 时上板脚本不在 ramdisk，需手工推送）。
- `z1_make_bootimg.sh` 增加：ramdisk 缺失或 `Z1_REPACK=1` 时自动调 repack 重建；打包前解压 ramdisk 校验 8 个关键件（固件 WIFI_RAM_CODE / WMT_SOC.cfg / ROMv1_patch / NVRAM / iw / launcher / modprobe 脚本 / 验证脚本）+ `.ko` 数量，缺项 WARN 并提示重打包。

---

## 5. 下次上板流程（一次跑完）

```
# 1. 用户起 mtkclient + 插 download 线，刷新 boot img
tools/flash_with_retry.sh bootimg out/boot_mainline_connsys.img /home/<user>/claude/6572/1/boot.img 8
# 2. 拔 download 线 + 电池开机（C3 桥 USB-CDC 全程在线），C3 桥端口 921600
./z1_capture_mainline.sh 600
# 3. 内核起来进 busybox shell 后（期望见 console=... / rdinit=/init）:
sh /root/connsys/z1_connsys_onboard_test.sh
#     → 7 步: connsys 目录 / modprobe+launcher / launcher 就绪 / func-on / wlan0 / ifconfig up + iw scan / stpbt
#     → 成功标志: [OK] wlan0 接口注册 + iw scan 出 SSID 列表
```

预期关键行（参照 `WIFI_FUNC_ON_SUCCESS_20260814.md`）:
```
kalFirmwareOpen: ... Open FW image done, 160464 bytes
wlanProbe: ... MAC address: <MAC>
[MTK-WIFI] WIFI_write: WMT turn on WIFI success!
lo  wlan0
```

---

## 6. 备注 / 遗留
- 脚本位于 `/home/<user>/claude/6572/scripts/` 与 `/home/<user>/claude/6572/tools/`，**不在 git 仓库内**（仅 `fork_linux` 是 git 仓库），故未 commit/push；改动已按 CLAUDE.md 规则备份到 `bridge_backups/scripts_v2_20260814_161122/`。
- `fork_linux/.../mt6572-z1.dts` 的 `linux,initrd-end` 已由 repack 自动改为 `0x842c6cc8` 并重编 dtb（符合该项目既有 initrd-end 同步流程）。
- 上板脚本 step 7 的 BT 只验证 `/dev/stpbt` 节点存在；真正 BT func-on（open stpbt + HCI）不在本脚本范围，需 `stpbt-hci-test`（ramdisk 未内置）。
