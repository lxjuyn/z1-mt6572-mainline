# 明天上板验证执行单（2026-08-10 晚）

> 来源：workflow `wyzb8t7ij`（9 agent 离线准备）+ eMMC 根因 workflow
> 待刷镜像：`out/boot_mainline_z1_emmc_cmd13.img`（final5，CMD13 方案，git `522c2abbe`）
> 同时已备：CONNSYS 时钟门修复已编进同 zImage（git `a4c17f314`）

## 分区映射（新确认）
| 分区 | 大小 | 内容 | 验证价值 |
|---|---|---|---|
| p1 | 1KiB | 保护性残留 | 无 |
| p2/p3 | 各 10MiB | PROTECT_F/S | 数据通路 smoke |
| p4 | 900MiB | **system ext4（rootfs 唯一候选）** | mount 金标准 |
| p5 | 226MiB | cache | 低 |
| p6 | 2450MiB | userdata（FDE footer 可能） | 写验证空区 |
| boot0/boot1 | 各 4MiB | preloader | dd boot1 前 1MB 对照原厂 |

**注意**：bootimg/recovery/uboot 等**无 pN 节点**（scatter 定义，裸偏移 41MiB 区），别找它们的 /dev/mmcblk0pN。

## 验证步骤（一次开机，按序）

### 1. 卡健康只读检查
```sh
cat /sys/block/mmcblk0/device/pre_eol_info 2>/dev/null
cat /sys/block/mmcblk0/device/life_time 2>/dev/null
cat /sys/block/mmcblk0/device/rev          # EXT_CSD_REV>=7 才有 life_time
```

### 2. 数据通路 smoke（关键！历史首扇区全 -110，新镜像未验）
```sh
head -c 512 /dev/mmcblk0p2 | hexdump -C | head -4   # 512B，10-80ms
head -c 512 /dev/mmcblk0p3 | hexdump -C | head -4
```
- ✅ 有输出 = **根因已解决**（CMD13 方案生效）
- ❌ `Card stuck being busy` / `Controller busy` = SDCBUSY 20ms 预检误拦 → 应用 PB1 外科清（`pb1_val &= ~BIT(14)`）

### 3. 内容验证 + 挂载（金标准）
```sh
echo 16 > /sys/block/mmcblk0/queue/read_ahead_kb
head -c 65536 /dev/mmcblk0p4 | strings | grep -a -m5 'ro.build\|build.prop\|Android'
mkdir -p /mnt && mount -t ext4 -o ro,noload /dev/mmcblk0p4 /mnt && ls /mnt
cat /mnt/build.prop 2>/dev/null | head
```
- ✅ mount 成功 + 看到 build.prop = **Z1 原厂系统镜像在 mainline 内核下可访问**
- p6 若报 bad magic = FDE footer 加密预期，非损坏

### 4. 超时边界负对照（可选，证明边界）
```sh
dd if=/dev/mmcblk0p2 bs=512K count=1 of=/dev/null   # 预期 ~5s abort
```

### 5. 写验证（已验证 ✅，位置已修正 p6@20971520→p6@2500000）
```sh
# ⚠️ 不用 p2(protect_f)：可能存 IMEI/MAC/校准出厂数据，写坏真险
# ⚠️ 不用 p6@20971520：实测超 p6 实际边界(1.24GB/2607872块)卡死 shell —— 执行单位置错误
# 用 p6@2500000 (1.28GB < 1.33GB 边界内, 2026-08-13 实测写读回成功)
dd if=/dev/mmcblk0p6 of=/dev/null bs=512 skip=2500000 count=1   # 基线读
printf 'Z1WTEST' | dd of=/dev/mmcblk0p6 bs=512 seek=2500000 count=1 conv=notrunc
dd if=/dev/mmcblk0p6 bs=512 skip=2500000 count=1 | cat           # 读回(应见 Z1WTEST)
```
- ✅ 2026-08-13 实测：CMD24 写 p6@2500000 成功，读回 Z1WTEST 持久化（外科清 PB1 修复生效）
- 勿碰 boot0/boot1/p1/p2/p3（防砖/防损出厂数据）

### 6. USB/按键/显示（独立，同轮测）
```sh
# USB: 内核起来后插线，看 /dev/ttyGS0（legacy g_serial 已就绪）
ls /dev/ttyGS0 && echo hi > /dev/ttyGS0
# 按键: 注意 goodix 已 disabled + keymap 仅 2 键，别按旧 13 键预期
cat /proc/bus/input/devices
# 显示: simplefb 已注册，echo 到 /dev/tty0
echo test > /dev/tty0
```

## 备选方案（若块读仍卡）
1. **PB1 外科清**：`msdc_init_hw` 里 `pb1_val &= ~BIT(14)`（清 DDR_CMD_FIX_SEL，可选再清 bit7）——勿整写 0xffff0009
2. **block.c 早退**：`mmc_blk_part_switch` 开头 ACC 已等于目标分区就 return（跳过 PART_CONFIG CMD6）
3. **R1B 按 R1 编码**：控制器侧 RSPTYP=0x1 不监测 DAT0

## 相关修复镜像
- final5：`out/boot_mainline_z1_emmc_cmd13.img`（4870144B）
- 归档：`bridge_backups/products/products_emmc_cmd13_20260810_001443/`
- git：`522c2abbe`（eMMC）+ `a4c17f314`（CONNSYS 时钟门）
