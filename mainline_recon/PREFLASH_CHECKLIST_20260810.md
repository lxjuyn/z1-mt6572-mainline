# Z1 eMMC CMD13 刷写确认单（2026-08-10 晚 · 上板用）

> 目标：验证 eMMC 块读修复（CMD13 方案，git `522c2abbe`）。全程约 30 分钟，只看本单。
> 主镜像 `out/boot_mainline_z1_emmc_cmd13.img`（4870144B，final5）；回滚 `1/boot.img`（原厂 6MB）。
> 基础路径：`cd /home/<user>/claude/6572`。

## 0. 就绪前置（刷前确认，全 ✅ 才动手）
- [ ] 镜像在：`out/boot_mainline_z1_emmc_cmd13.img`（4870144B，归档 `bridge_backups/products/products_emmc_cmd13_20260810_001443/`）
- [ ] 回滚在：`1/boot.img`（6291456B，原厂零修改）
- [ ] 脚本在：`tools/flash_with_retry.sh`、`z1_capture_mainline.sh`、`tools/onboard_verify.sh`
- [ ] 桥在线：C3 常亮 + `ls -l /dev/ttyACM0` 有节点（当前离线，刷前需接上）
- [ ] fork 树含修复：`git log` 有 `522c2abbe`（CMD13）+ `a4c17f314`（CONNSYS 时钟门）
- [ ] 铁律：刷写才插 download 线，正常开机必拔；C3 UART 线全程不动；先起脚本等 BROM 再插线+按开机键

## 1. 刷写
```sh
Z1_VERIFY=0 tools/flash_with_retry.sh bootimg out/boot_mainline_z1_emmc_cmd13.img /home/<user>/claude/6572/1/boot.img 8
```
- 看到「第 1/8 次尝试写」进入等待后：**插 download 线 + 按开机键**
- 预期 `BROM` 握手 → `Write done`/`Done` → 退出 0。连不上就拔线 3s 重插再按，脚本自动重试 8 次
- `Z1_VERIFY=0` 跳严格读回（内核常驻后连不上 preloader 属正常，勿判失败）

## 2. 开机 + 抓 log
```sh
# 先拔 download 线，电池开机（不带 USB）
./z1_capture_mainline.sh 600
```
- 预期链：preloader 串 → `earlycon` → `Linux version` → `mmc0: new high speed SDXC card` → `~ #` shell
- 0 输出重启循环 → 直接回滚（§5）

## 3. 板上验证（到 `~ #`，手动 3 条关键命令，输出看 console）
```sh
# ① 512B 数据通路 smoke —— 核心判据：能读出 = CMD13 根因已解决
head -c 512 /dev/mmcblk0p2 | hexdump -C | head -4
# ② 挂载 p4 金标准 —— 原厂 system 可访问
mkdir -p /mnt && mount -t ext4 -o ro,noload /dev/mmcblk0p4 /mnt && ls /mnt && cat /mnt/build.prop | head
# ③ 写验证 —— 用 p6 中段（避开 FDE footer），勿碰 p2/boot0/boot1
dd if=/dev/mmcblk0p6 of=/dev/null bs=512 skip=20971520 count=1
echo -n 'Z1WTEST' | dd of=/dev/mmcblk0p6 bs=512 seek=20971520 count=1 conv=notrunc
sync; sleep 1
dd if=/dev/mmcblk0p6 bs=512 skip=20971520 count=1 | hexdump -C | head -3
```
- 可选全脚本：`tools/onboard_verify.sh` 经 C3 桥粘入板 shell 跑（结果 `/tmp/verify.log`）。**只跑到 STEP4**——脚本 STEP5 写的是 p2@20400，p2 可能存 IMEI/MAC，写验证用手动第 ③ 条
- 完事 `umount /mnt` 再关机

## 4. 判据（PASS = 全过）
| 检查 | 判据 |
|---|---|
| ① smoke | hexdump 有 512B 输出（**不是** `stuck busy`/-110/`Controller busy`） |
| ② mount | p4 mount 成功 + 见 `build.prop`（= 原厂 system 在 mainline 下可访问） |
| ③ 写读回 | p6 读回含 `Z1WTEST`；p6 若报 bad magic = FDE 加密预期，非损坏 |
| ④ 健康只读(可选) | `cat /sys/block/mmcblk0/device/{pre_eol_info,life_time,rev}` |
| ⑤ 独立项(同轮) | `ls /dev/ttyGS0 && echo hi > /dev/ttyGS0`；`cat /proc/bus/input/devices`（goodix disabled，仅 2 键）；`echo test > /dev/tty0` |

- 分区映射提醒：bootimg/recovery/uboot **无 pN 节点**（裸偏移 41MiB 区），只找 `/dev/mmcblk0p1..p6/boot0/boot1`；p2/p3 各 10MiB（smoke）、p4 900MiB system（mount 金标准）、p6 2450MiB userdata（写验证）

## 5. 回滚（镜像起不来 / 结果不对）
```sh
Z1_VERIFY=0 tools/flash_with_retry.sh bootimg /home/<user>/claude/6572/1/boot.img /home/<user>/claude/6572/1/boot.img 8
```
- 流程同 §1：先等脚本窗口，再插线+按开机键

## 6. 提速 / 增强备选（块读 PASS 后再刷，先记录本轮结果）
- 50MHz 提速：`out/boot_mainline_z1_emmc_50mhz.img`（4870144B，git `84e1b0fb9`）
- 音频+电池：`out/boot_mainline_z1_emmc_audio_batt.img`（4976640B）
- 同流程换镜像重抓 log 对比

## 7. 若块读仍卡（根因排查备选，勿临时改）
1. PB1 外科清：`msdc_init_hw` 里 `pb1_val &= ~BIT(14)`（清 DDR_CMD_FIX_SEL，可选再清 bit7），勿整写 0xffff0009
2. block.c 早退：`mmc_blk_part_switch` 开头 ACC 已等目标分区就 return（跳过 PART_CONFIG CMD6）
3. R1B 按 R1 编码：控制器侧 RSPTYP=0x1 不监测 DAT0

---
技术细节见 `mainline_recon/ONBOARD_VERIFY_PLAN_20260810.md`；操作流程见 `mainline_recon/ONBOARD_USER_GUIDE_20260810.md`。
