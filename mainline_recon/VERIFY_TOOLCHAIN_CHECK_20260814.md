# VERIFY_TOOLCHAIN_CHECK_20260814 — 两个新脚本只读校验报告

日期: 2026-08-14
校验方式: 只读分析 + 交叉核对 CLAUDE.md / 当前 dts / repack / modprobe 脚本 + `bash -n`。未刷写、未碰硬件。

结论摘要:

| 脚本 | 结果 | 处理 |
|---|---|---|
| `tools/z1_make_bootimg.sh` | OK（1 处健壮性问题已修） | 备份 + 修复 |
| `scripts/z1_connsys_onboard_test.sh` | OK（无问题） | 不改 |

---

## 1. tools/z1_make_bootimg.sh — OK, 修复 1 处

### 语法
- `bash -n`: 通过（修复前后均通过）。
- `set -euo pipefail` 与本脚本用法兼容（校验/警告均在 `if` 条件或 echo 参数内，无 set -e 误杀风险）。

### 路径默认值（合理, 全部实存）
| 项 | 值 | 实测 |
|---|---|---|
| MKBOOTIMG | `$ROOT/build/mtkbootimg/mkbootimg` | 存在且可执行 ✅ |
| zImage | `fork_linux/arch/arm/boot/zImage` | 存在 (3764736 B) ✅ |
| dtb | `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb` | 存在 (19532 B) ✅ |
| ramdisk | `out/ramdisk_mainline_connsys.gz` | 存在 (2480968 B)，正是 `tools/repack_ramdisk_connsys.sh` 的产物（OUT 默认一致）✅ |
| output | `out/boot_mainline_connsys.img` | 不存在（预期, 待生成）✅ |

### mkbootimg 参数 vs CLAUDE.md 打包命令
- `--base 0x10000000 --kernel_offset 0x8000 --pagesize 2048 --mtk 1`: 与 CLAUDE.md §4/§4a 完全一致 ✅
- 不带 `--dt`、dtb 用 `cat` append 进 kernel 段（`/tmp/zImage_appended_z1ver`）: 与 §4 "拼到 zImage 末尾" 的思路一致 ✅（mainline CONFIG_ARM_APPENDED_DTB 在 `_edata` 找 `d00dfeed`）。

### cmdline 说明（有意偏差, 非 bug）
脚本 cmdline = `console=tty0 console=ttyS0,115200n8 root=/dev/ram panic=0 ignore_loglevel clk_ignore_unused rdinit=/init`。
- 与 CLAUDE.md §4 历史命令的 `console=ttyMT0,921600n8 earlycon=mtk8250,0x11005000` 不同。
- 但**与当前 dts `mt6572-z1.dts` 第 31 行 `chosen.bootargs` 逐字一致**，且 memory 记录 console 换 115200/ttyS0 是已验证的根治（"console挂死=115200根治"）。故与当前项目状态一致，判定为**有意迁移**，不改。
- 建议（非必须）: 若需早期调试输出，可在 CMDLINE 追加 `earlycon=mtk8250,0x11005000`。

### 校验逻辑正确性
- 6MB 校验: `MAX_BOOT=6291456`（=6MiB）正确；`[ "$SIZE" -le "$MAX_BOOT" ]` 正确。
- magic 校验: `head -c 8 "$OUT"` 与 `"ANDROID!"`（8 字节）比较正确。
- ROOTFS 头警告: 比较前 4 字节 `88168858`，实测 ramdisk 头为 `88 16 88 58 48 d9 25 00 ... ROOTFS`，匹配 ✅（此处用 od/tr 仅为 PC 端警告，非 busybox 限制场景）。
- 大小余量提示: 当前 zImage(3.76MB)+dtb(19KB)+ramdisk(2.37MB) 按 pagesize=2048 补齐约 5.98MiB，距 6MB 仅 ~22KB 余量。脚本会在超限时 FAIL（本次修复后还会 exit 1），不会静默产坏包。

### 本次修复（唯一问题点）
原版两个 FAIL 分支只打印 `[FAIL]` 后继续跑，最后仍打印 "=== 打包完成, 未刷写 ===" 且 exit 0，误导链式/CI 调用方。已加 `exit 1`:
- 6MB 校验 FAIL → `exit 1`
- magic 校验 FAIL → `exit 1`
- 备份: `bridge_backups/z1_make_bootimg.sh_20260814_025401.bak`
- 修复后 `bash -n` 通过。

---

## 2. scripts/z1_connsys_onboard_test.sh — OK, 无需修改

### 语法
- `bash -n`: 通过；`sh -n`: 通过（#!/bin/sh 合法）。

### busybox 命令白名单（只用 busybox 有的命令）
实际使用的命令: `echo / cat / grep / ls / sleep / command -v`（command 是 ash 内建）＋ shell 关键字/内建（if/for/case/`[`/test）。
- **无 `tee` / `lsmod` / `od` / `hexdump`** 等 busybox 缺失命令 ✅
- 头部注释列的允许集（printf/df/mount）只是声明"可用"，脚本体实际未用，无碍。
- 提示: 被调用的 `z1_modprobe_connsys.sh` 用了 `pgrep -f`（busybox 可能无），但其在 `if ... 2>&1` 内，缺失时优雅降级为"视为未运行→启动 launcher"，不构成硬失败。属脚本外内容，仅提示。

### 模块名（交叉核对 `scripts/z1_modprobe_connsys.sh` + `tools/repack_ramdisk_connsys.sh`）
EXPECT = `cfg80211 mtk_btif_drv mtk_stp_wmt_soc mtk_stp_bt_soc mtk_wmt_wifi_soc wlan_gen2`
- 与 modprobe 脚本 insmod 的 6 个 `.ko` 名逐一对应 ✅
- `/proc/modules` 行形如 `mtk_stp_wmt_soc  12345  0 ...`，`grep -q "^$m "`（模块名后跟空格）匹配正确 ✅
- ≥5 判定含 cfg80211，与依赖顺序合理 ✅

### 设备节点 major（交叉核对 modprobe 脚本 mknod）
- stpwmt=190 ✅（`mknod /dev/stpwmt c 190 0`）
- stpbt=192 ✅（`mknod /dev/stpbt c 192 0`）
- wmtWifi=155 ✅（`mknod /dev/wmtWifi c 155 0`）
- 脚本第 ④ 步只检查节点存在，提示文案里 major 也对 ✅

### 部署路径
`/root/connsys/z1_modprobe_connsys.sh` 与 `tools/repack_ramdisk_connsys.sh`（`cp "$SCRIPT" .../root/connsys/z1_modprobe_connsys.sh`）一致 ✅

### 上板流程逻辑
①~⑦ 步骤顺序合理；⑤ `echo 1 > /dev/wmtWifi` 用 `if` 检测重定向失败（节点不存在时 shell 重定向失败→进入 fail 分支）✅；⑥ wlan* 接口检测正确 ✅。

---

## 3. 最终清单
- 两个脚本 `bash -n` 均通过。
- `z1_make_bootimg.sh` 修复 1 处（FAIL 时 exit 1），原版已备份 `bridge_backups/z1_make_bootimg.sh_20260814_025401.bak`。
- `z1_connsys_onboard_test.sh` 无需改动。
- 未刷写、未碰硬件、未调用 flash 工具。
