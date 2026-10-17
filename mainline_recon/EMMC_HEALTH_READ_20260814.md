# Z1 eMMC 旧卡 H4G1d 健康读取方案 (2026-08-14)

> 纯只读源码分析，未改文件 / 未编译 / 未碰硬件。fork_linux HEAD = `29517eb7a`。
> 涉及文件：`drivers/mmc/core/mmc.c` / `drivers/mmc/core/block.c` / `drivers/mmc/core/debugfs.c` / `drivers/mmc/core/bus.c` / `drivers/mmc/host/mtk-sd.c` / `include/linux/mmc/mmc.h` / `arch/arm/boot/dts/mediatek/mt6572-z1.dts`。
> 板上证据：`mainline_recon/z1_boot_mainline_20260814_152956.log:1228-1246` eMMC 枚举正常。

---

## 1. sysfs 导出确认：pre_eol_info / life_time 存在，但值只在 EXT_CSD_REV >= 7 时填充

### 导出路径（两者都无条件注册）
- `/sys/block/mmcblk0/device/pre_eol_info`（1 字节）
- `/sys/block/mmcblk0/device/life_time`（2 字节 `"0xAA 0xBB"` = TYP_A TYP_B）
- 定义：`mmc.c:805-808` `MMC_DEV_ATTR(pre_eol_info, "0x%02x\n", ...)` / `MMC_DEV_ATTR(life_time, ...)`；挂进 `mmc_std_attrs[]`（`mmc.c:867-868`），**任何 eMMC 卡都注册这两个属性，属性本身永远存在**。

### 值的来源与填充门槛（关键）
- `mmc_read_ext_csd`（`mmc.c:679`）boot 期发 CMD8（`MMC_SEND_EXT_CSD`）读 512B EXT_CSD；`mmc_decode_ext_csd` 解码字段。
- **`pre_eol_info` / `device_life_time_est_typ_a/b` 只在 `card->ext_csd.rev >= 7`（eMMC v5.0 及以后）时被读入**（`mmc.c:640-653`，注释 "eMMC v5 or later"）：
  ```c
  if (card->ext_csd.rev >= 7) {
      ...
      card->ext_csd.pre_eol_info = ext_csd[EXT_CSD_PRE_EOL_INFO];      // EXT_CSD[267]
      card->ext_csd.device_life_time_est_typ_a = ext_csd[EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_A]; // 268
      card->ext_csd.device_life_time_est_typ_b = ext_csd[EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_B]; // 269
  }
  ```
- `card->ext_csd` 是 `kzalloc` 出来的（`bus.c:282` `mmc_alloc_card`），**rev < 7 时这两个字段保持 0x00**。即 rev < 7 的卡 `cat pre_eol_info` 必得 `0x00`、`cat life_time` 必得 `0x00 0x00` —— 这是"未定义"值，**不是"健康/正常"**，切勿误读为 0x01 normal。
- 寄存器偏移（`include/linux/mmc/mmc.h:283,314-316`）：`EXT_CSD_REV=192`、`PRE_EOL_INFO=267`、`LIFE_TIME_TYP_A=268`、`LIFE_TIME_TYP_B=269`。
- `rev` 本身经 sysfs `rev` 导出：`mmc.c:804` `MMC_DEV_ATTR(rev, "0x%x\n", card->ext_csd.rev)` → `/sys/block/mmcblk0/device/rev`。**这是决定健康值是否可信的第一道闸门**。

### EXT_CSD_REV 版本映射（JEDEC JESD84）
| rev | 规范 | pre_eol/life_time 字段 |
|---|---|---|
| 0x5 | eMMC 4.41 | 不存在 |
| 0x6 | eMMC 4.5 | **规范上存在**（4.5 引入），但本内核不读 → sysfs 恒 0x00 |
| 0x7 | eMMC 5.0 | 内核读入，sysfs 有真值 |
| 0x8 | eMMC 5.1 | 内核读入，sysfs 有真值 |

> **结论：sysfs 路由只有 `rev >= 0x7` 才给真实健康数据。** 对 rev=6（4.5）的老卡，规范字段存在但本 mainline 内核不导出，sysfs 恒 0x00。

### 备选读法（绕过 rev 门槛，拿原始 EXT_CSD 字节）
本内核 **没有** `/sys/kernel/debug/mmc0/ext_csd`（`debugfs.c:353-399` 只有 host 的 `ios/caps/caps2/clock/err_state/err_stats` + card 的 `state/quirks`，**无 ext_csd 文件**）。`EMMC_REMAINING_OFFLINE_20260814.md:122` 提到的 "debugfs /sys/kernel/debug/mmc0/ext_csd" 在本树**不存在**，予以纠正。

真值只能靠直接发 CMD8 拿原始 EXT_CSD[267/268/269]，可选：
- `mmc-utils`：`mmc extcsd read /dev/mmcblk0 | grep -iE 'PRE_EOL|LIFE_TIME|EXT_CSD_REV'`（ioctl `MMC_IOC_MULTI_CMD` 发 CMD8，不受内核 rev 门槛限制）。但当前 rootfs 是 MTK ROOTFS 精简 ramdisk（上板实测无 `dd`，见 `z1_emmc_test_233012.log`），**mmc-utils 基本确定没有**。
- 自写微型 CMD8 ioctl 程序（本次禁止编译，留作上板后如需再考虑）。

---

## 2. mtk-sd.c 对读健康的支持：无特殊限制，CMD8 读 EXT_CSD 早已验证可用

- Z1 主机 = `mtk-msdc 11120000.mmc`。`host->z1_direct_cid = of_machine_is_compatible("jbo,z1")`（`mtk-sd.c:3630`）走 Z1 专属路径：BOOTRST 复位移入、CMD1 vendor rawcmd 编码、PB1 外科清 `BUSY_CHECK_SEL|DDR_CMD_FIX_SEL`（保留 CMDTA/WRDAT_CRC）、PAD_TUNE 直写——**全与"读 EXT_CSD"无关，不构成限制**。
- 内核 boot 期已经成功 `mmc_read_ext_csd`（CMD8 单块 512B 读），当前 boot log `z1_boot_mainline_20260814_152956.log:1230-1244` 卡全枚举：`new MMC card at address 0001` / `mmcblk0: mmc0:0001 H4G1d 3.64 GiB` / `mmcblk0boot0/boot1 ... 4.00 MiB` / `mmcblk0rpmb ... chardev`，无 I/O 错误。**host 读路径（含 CMD8）现成可用，无需任何改动**。
- 无 `MMC_CAP2_BOOTPART_NOACC`（`mtk-sd.c` 未设）→ boot0/boot1 正常注册。
- 相关限制只影响**大块数据读的耗时/超时**（1-bit 总线，见 §4），不影响 512B 的 EXT_CSD 读与健康读取。
- dts（`mt6572-z1.dts:342-348`）：`bus-width=<1>`、`max-frequency=<50000000>`、`no-sd`、`no-sdio`、`non-removable`。1-bit 决定大块读慢（§4 超时要点）。

---

## 3. 上板验证命令（全部只读 / 无硬件动作）

> 前提：Z1 拔 download 线、电池开机、C3 桥 921600 抓 console（现有流程）。rootfs 为精简 ramdisk，**无 `dd`**（`z1_emmc_test_233012.log` 实测 `dd: not found`），命令全部用 `cat/head/od` 可执行的写法，`dd` 只作备选。

```sh
# (0) 小 read-ahead，避免 400kHz/1bit 下大单请求撞 5s 软看门狗（见 §4）
echo 16 > /sys/block/mmcblk0/queue/read_ahead_kb

# (1) 第一道闸门：EXT_CSD revision
cat /sys/block/mmcblk0/device/rev
#   0x5=4.41 | 0x6=4.5（字段规范存在但本内核不读→下面恒 0x00）| 0x7=5.0 | 0x8=5.1
#   期望：<0x7（2013 时代 4GB 旧卡大概率 rev 5/6）；若意外 ≥0x7，则 §2 有真值

# (2) 健康字段（sysfs 纯内存读，boot 期已解码，不再发任何命令，零风险）
cat /sys/block/mmcblk0/device/pre_eol_info
#   0x01=Normal | 0x02=Warning(已消耗≥80%保留块) | 0x03=Critical(≥90%，紧急) | 0x04=EWR over | 0x00=未定义
cat /sys/block/mmcblk0/device/life_time
#   输出 "0xAA 0xBB"：TYP_A TYP_B，0x01=0-10% ... 0x0a=90-100%，0x00=未定义
#   MLC 卡重点看 TYP_B。rev<7 时必为 "0x00 0x00"（未定义，勿当健康）

# (3) 直接读原始 EXT_CSD（仅当 rootfs 有 mmc-utils；预期没有，属可选项）
which mmc && mmc extcsd read /dev/mmcblk0 | grep -iE 'PRE_EOL|LIFE_TIME|EXT_CSD_REV'

# (4) boot0/boot1 只读验证
cat /sys/block/mmcblk0boot0/force_ro          # 期望 1（boot 区 set_disk_ro=true，block.c:2610）
cat /sys/block/mmcblk0boot1/force_ro          # 期望 1
cat /dev/mmcblk0boot0 > /tmp/boot0.img 2>&1   # 只读 cat，期望成功
cat /dev/mmcblk0boot1 > /tmp/boot1.img 2>&1   # 期望成功
echo test > /dev/mmcblk0boot0 2>&1            # 期望写被拒：Read-only file system / Write error
#   boot 区写被 block 层拒（set_disk_ro + mmc_blk_open BLK_OPEN_WRITE 检查，block.c:380-383），
#   底层 CMD 根本不会发出，安全。

# (5) 卡健康相关旁证
cat /sys/block/mmcblk0/size                   # 期望 ~7634944（=3.64 GiB 扇区数）
cat /sys/kernel/debug/mmc0/ios                 # 实际 clock/bus width（需先 mount -t debugfs none /sys/kernel/debug）
# 备选 dd（若 rootfs 有）：dd if=/dev/mmcblk0boot0 of=/tmp/boot0.img bs=512 count=8192
```

**boot0/boot1 读的底层行为与安全性**：`cat /dev/mmcblk0boot0` 首次 open 会经 `mmc_blk_part_switch` 发一次 CMD6 `PART_CONFIG`（EXT_CSD[179] 寄存器配置写，R1B）切到 boot 区，再 CMD18 数据读。该 CMD6 由修复 A（摘 `.card_busy` → core 走 CMD13 轮询，`mtk-sd.c:3048-3064`）正确完成；只写 EXT_CSD 寄存器，**不碰用户区/数据**，对卡零磨损。boot0 4 MiB 读取耗时：HS 50MHz/1bit ≈ 0.7s；若时钟停在 400kHz/1bit ≈ 80s+（正常现象，别当卡死）。

---

## 4. H4G1d 旧卡寿命风险评估

### 卡的事实
- CID 产品名 `H4G1d`，4GB 级 MLC 旧卡，MT6572（2013）时代的原配 eMMC，距今 10+ 年。
- 当前 boot 全正常：`mmcblk0 3.64 GiB` + `p1<p5 p6>p2 p3 p4` + boot0/1 各 4.00 MiB + rpmb chardev，读/写/挂载已上板验证 OK（任务前提）。boot log 无 eMMC I/O 错误。
- 已知写入历史：开发期多次 mtkclient 全 eMMC 刷写（bootimg/system.img 等），每一次下载写都是实打实的 P/E 消耗。

### 寿命字段语义（JEDEC，仅 rev>=7 可读）
- `pre_eol_info`：0x01 正常（保留块消耗 <80%）→ 0x02 警告（≥80%）→ 0x03 紧急（≥90%，建议尽快备份）→ 0x04 超出保修。
- `life_time` TYP_A/TYP_B：控制器估算寿命已用百分比，0x01~0x0a = 0~100%，10% 一档。MLC 卡 TYP_B 是主指标。

### 风险判断
1. **读操作永远安全**：CMD8/CMD17/CMD18 是纯读，不产生 program/erase，不消耗 P/E 循环。读引起的 read-disturb 控制器内部搬运/刷新，对 10 年旧卡在"全盘备份级"（几 GB）读取量下可忽略。**降级只读归档 = 对这个卡做的最正确的事。**
2. **如果 pre_eol_info=0x03（Critical）或 life_time=0x09/0x0a（80-100%）**：
   - 读仍安全，但控制器判定已近 EOL：坏块增多、读可能变慢/偶发 read-retry，写可靠性大幅下降。
   - **立即优先做一次全盘只读 dd 归档**（`cat /dev/mmcblk0 > /tmp/z1_emmc_full.img`，几个 GB，1-bit 下可能需几十分钟，分批 bs 小的 `head -c` 循环可防单请求超时）。归档后再做任何写操作（重刷、改分区）都先评估必要性。
   - 系统镜像定稿后不再反复全刷；维持 ramdisk rootfs 启动（当前 `root=/dev/ram`，boot 不自动写卡，天然低磨损）。
3. **如果 rev<7（很可能）读不出真值（0x00/未定义）**：
   - 按"未知健康、按老化处理"的最保守姿态：**先全盘只读归档，再当只读卡用**，所有挂载用 `ro`，能不写就不写。10+ 年 MLC 原厂卡，把"能不能读出完整数据"当最高优先级，别赌它还能扛几次重刷。
4. **容量旁证**：早期 08-04 boot（`z1_emmc_test_233012.log`）`/proc/partitions` 显示 `mmcblk0=3817472` 块（1.82 GiB）而当前 08-14 boot 打印 `3.64 GiB`，系旧 boot 的旧状态；当前以 `cat /sys/block/mmcblk0/size`（期望 7634944 扇区）为准。另注意 boot log `mmcblk0: p6 size 4292549119 extends beyond EOD`——MTK 分区表惯例 userdata 填到盘尾，内核自动钳制，正常现象。

### 上板读健康优先级建议
1. `rev` → 决定能不能信 sysfs 健康值（最高优先，一秒钟出结果）。
2. `pre_eol_info` / `life_time` → 若 rev≥7 有真值，直接定级；若 0x00 按"未定义"处理。
3. **无论读到什么，先做全盘只读归档**，再讨论后续写操作。
4. boot0/boot1 只读 + `force_ro=1` 验证（§3 命令），确认引导区可完整救出（它们是救砖的关键 4+4 MiB）。

---

## 5. 结论一句话
健康读法现成且零风险：`cat /sys/block/mmcblk0/device/{rev,pre_eol_info,life_time}` + boot0/1 `cat` 只读验证；但 **rev<7 的老卡（H4G1d 大概率）sysfs 健康值恒 0x00（未定义），不能拿来当"健康"证据**——按老化卡处理，先全盘只读归档、只读挂载、尽量少写。

*基于 fork_linux HEAD `29517eb7a`，只读完成。行号以该提交为准。*
