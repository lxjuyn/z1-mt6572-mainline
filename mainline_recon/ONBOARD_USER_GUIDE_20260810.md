# Z1 上板验证操作手册（2026-08-10 晚）

> 目标：验证 eMMC 块读修复（CMD13 方案）。约 30 分钟，只看本文档即可操作。
> 主刷镜像：`out/boot_mainline_z1_emmc_cmd13.img`；备选：`_50mhz`（提速）/`_audio_batt`（音频+电池）。
> 全部路径基于 `cd /home/<user>/claude/6572`。

## 三条铁律
1. **刷写才插 download 线，正常开机必拔**（USB 插着干扰 boot，进重启循环）。
2. **C3 桥 UART 线全程不动**。
3. **时序**：先起脚本等 BROM 窗口，再插线+按开机键；顺序反了连不上。

## 一、刷写前（2 分钟）
1. 确认 C3 桥常亮、`/dev/ttyACM0` 在线：`ls -l /dev/ttyACM0`。
2. 起刷写窗口（等 BROM）：
   ```
   Z1_VERIFY=0 tools/flash_with_retry.sh bootimg out/boot_mainline_z1_emmc_cmd13.img /home/<user>/claude/6572/1/boot.img 8
   ```
3. 看到「第 1/8 次尝试写」进入等待后：**插 download 线 + 按开机键**。
   - 预期：`BROM` 握手 → `Write done` / `Done` → 脚本退出 0。
   - 出问题：连不上 → 拔线等 3 秒重插再按开机键，脚本自动重试 8 次。`Z1_VERIFY=0` 已跳严格读回（内核常驻后连不上 preloader 是正常，别当失败）。

## 二、刷后开机 + 抓 log
1. **拔 download 线**，电池开机（不带 USB）。
2. 抓 log（600 秒）：
   ```
   ./z1_capture_mainline.sh 600
   ```
   - 预期：preloader 串 → `earlycon` → `Linux version` → `mmc0: new high speed SDXC card` → 最终 `~ #` shell 提示符。
   - 出问题：0 输出重启循环 → 回滚原厂（第四节）。

## 三、板上跑验证
到 `~ #` 后，**推荐手动跑 3 条关键命令**（输出同时看 console）：
```
# 1) 512B 数据通路 smoke —— 核心：能读出 = 根因已解决
head -c 512 /dev/mmcblk0p2 | hexdump -C | head -4
# 2) 挂载 p4 金标准 —— 原厂 system 可访问
mkdir -p /mnt && mount -t ext4 -o ro,noload /dev/mmcblk0p4 /mnt && ls /mnt && cat /mnt/build.prop | head
# 3) 写验证 —— 用 p6 中段（避开 FDE footer），别碰 p2/boot0/boot1
dd if=/dev/mmcblk0p6 of=/dev/null bs=512 skip=20971520 count=1
echo -n 'Z1WTEST' | dd of=/dev/mmcblk0p6 bs=512 seek=20971520 count=1 conv=notrunc
sync; sleep 1
dd if=/dev/mmcblk0p6 bs=512 skip=20971520 count=1 | hexdump -C | head -3
```
若想跑整脚本：把 `tools/onboard_verify.sh` 内容经 C3 桥粘到板子 shell，`sh onboard_verify.sh`，结果在 `/tmp/verify.log`。
- 预期：命令 1 有 hexdump 输出（不是 `stuck busy`/-110）；命令 2 mount 成功见 `build.prop`；命令 3 读回含 `Z1WTEST`。脚本则看 `SUMMARY: PASS=` 与 `OVERALL: PASS`。
- 注意：**脚本内 STEP5 写的是 p2@20400（p2 可能存 IMEI/MAC，写坏险）**，跑脚本时到 STEP4 即可，写验证用手动第 3 条。p6 若报 `bad magic` = FDE 加密预期，非损坏。
- 完事 `umount /mnt` 再关机。

## 四、回滚（镜像起不来 / 结果不对）
```
Z1_VERIFY=0 tools/flash_with_retry.sh bootimg /home/<user>/claude/6572/1/boot.img /home/<user>/claude/6572/1/boot.img 8
```
流程同第一节，确认脚本等窗口后再插线+按开机键。

## 五、提速 / 增强（块读 PASS 后再刷）
先记录当前验证结果，再同流程换镜像重抓 log 对比：
- 50MHz 提速：`out/boot_mainline_z1_emmc_50mhz.img`
- 音频+电池：`out/boot_mainline_z1_emmc_audio_batt.img`

---
技术细节见 `mainline_recon/ONBOARD_VERIFY_PLAN_20260810.md`；镜像归档 `bridge_backups/products/products_emmc_cmd13_20260810_001443/`。
