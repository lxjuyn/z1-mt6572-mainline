# MMC 分区 / EOD 处理 — 源码级分析 (2026-08-15)

> 只读源码分析。对象: `fork_linux` mainline 7.0-rc7 的 block/mmc 源码 + Z1 出厂分区表 dump（`/home/<user>/claude/6572/1/{MBR,EBR1,MT6572_Android_scatter.txt}`）。
> 聚焦内核 log `mmcblk0: p6 size 4292549119 extends beyond EOD, truncated` 的**代码路径**、p6 声明 vs 实际容量、以及该警告与"shell 后 ~30s 重启"的关系。
> 与 `P6_EOD_REBOOT_SCENE_20260815.md`（log/场景级）互补，本文件是源码级。

---

## 0. TL;DR

1. **`extends beyond EOD` 在 `block/partitions/core.c`，不在 mmc 驱动里**。mmc 驱动只负责把整盘容量 (`set_capacity` = EXT_CSD SEC_CNT = 7634944 扇区) 交给 block 层，分区表解析与 EOD 截断全部是通用 block 层逻辑。
2. **超 EOD 时内核做的是"静默截断"，不是丢弃、不是 I/O 错误**：`core.c:549-563` 把分区 size 截为 `capacity − from`，分区照常创建。p6 从声明的 4292549119 扇区被截为 **5215744 扇区 ≈ 2.487 GiB**。
3. **p6 声明 4292549119 的单位是 512B 扇区 ≈ 2.0 TiB，不是 4.29 GB**（任务背景里的"4.29GB"是单位误读）。这是 MTK EBR 生成器对最后一个逻辑分区（usrdata）填的"到 32-bit LBA 上限 0xFFFFFFFF"哨兵值 `0xFFFFFFFF − rel_start = 0xFFDB19FF`。eMMC 实际只有 3.64 GiB，超 1.76 TiB。
4. **分区表是 MBR+EBR（msdos 风格），不是 GPT**；`CONFIG_EFI_PARTITION=y` 但因 MBR 无 0xEE protective 项而立即退出，不走 GPT 路径。
5. **分区超 EOD 不是重启诱因**：分区扫描是 ~0.7s 的一次性只读事件（只读 LBA 0 + LBA 1024），截断后 `/dev/mmcblk0p6` 是合法封顶分区，后续任何访问都由 block 层 `bio_check_eod` 兜底（超界请求返回 -EIO，不会下发到 MMC 控制器）；30.55s 复位时板子空闲在 shell（无 eMMC 流量），无 eMMC 异常→WDT 的因果通道。历史多个存活超 30s 的 boot 每次都有同一行警告。

---

## 1. "extends beyond EOD" 的代码位置

字符串在 **`block/partitions/core.c`**（通用分区层，非 mmc 专用）：

| 行 | 代码 | 含义 |
|---|---|---|
| `core.c:540-547` | `if (from >= get_capacity(disk))` → `"p%d start %llu is beyond EOD"` | 分区起点就在盘尾之外 |
| `core.c:549-563` | `if (from + size > get_capacity(disk))` → `"p%d size %llu extends beyond EOD"` | **分区跨越盘尾 → 本警告** |
| `core.c:562` | `size = get_capacity(disk) - from;` | **截断动作**（唯一一行） |
| `core.c:517-528` | `disk_unlock_native_capacity()` | 截断前的"解锁原生容量"尝试，mmc 无此 fops → 打 `truncated` 并返回 false |

调用链：

```
mmc_blk_alloc_req() → add_disk_fwnode()            [drivers/mmc/core/block.c:2535-2645]
  → device_add_disk() → bdev_disk_changed()        [block/partitions/core.c:643]
    → blk_add_partitions()                          [core.c:582]
      → check_partition() → msdos_partition()       [block/partitions/msdos.c:581]
        → parse_extended()                          [msdos.c:130]
      → blk_add_partition() ← EOD 检查+截断在此      [core.c:530-580]
```

mmc 驱动侧只做两件事（`drivers/mmc/core/block.c`）：
- `mmc_blk_alloc()` (block.c:2657-2677)：`size = card->ext_csd.sectors`（EXT_CSD SEC_CNT，整盘容量）→ `set_capacity(md->disk, size)` (block.c:2629)。**这是 EOD 的取值来源**。
- `mmc_bdops` (block.c:871-881)：**没有 `unlock_native_capacity` fops**（只有 open/release/getgeo/ioctl/alternative_gpt_sector）。这是决定走"truncated"而不是"enabling native capacity"的关键（见 §2.2）。

`mmc_part_add()` (drivers/mmc/core/mmc.c:318-328) 只是把 boot/gp 硬件分区登记进 `card->part[]`，与 OS 分区表（MBR/EBR → p1..p6）**完全无关**；mmcblk0p1-p6 是 block 层从 MBR/EBR 建的，不是 mmc_part_add 建的。

---

## 2. 分区 size 超 EOD 时内核做了什么

### 2.1 截断流程 (`core.c:530-580`)

```c
static bool blk_add_partition(struct gendisk *disk, struct parsed_partitions *state, int p)
{
	sector_t size = state->parts[p].size;
	sector_t from = state->parts[p].from;
	...
	if (from >= get_capacity(disk)) {                    // 起点就超 → 丢弃该分区
		printk(KERN_WARNING "%s: p%d start %llu is beyond EOD, ", ...);
		if (disk_unlock_native_capacity(disk)) return false;
		return true;                                    // 不建分区
	}
	if (from + size > get_capacity(disk)) {              // 跨越盘尾 → 截断
		printk(KERN_WARNING "%s: p%d size %llu extends beyond EOD, ", ...);
		if (disk_unlock_native_capacity(disk)) return false;
		size = get_capacity(disk) - from;               // ← 截断到盘尾
	}
	part = add_partition(disk, p, from, size, ...);       // 照常创建（封顶后的 size）
	...
}
```

注意 `add_partition` (core.c:294-332) 内部 `bdev_set_nr_sectors(bdev, len)` 用**截断后的 len** 设置分区块设备容量。

### 2.2 为什么 mmc 打 "truncated" 而不是 "enabling native capacity"

`disk_unlock_native_capacity` (core.c:517-528)：

```c
static bool disk_unlock_native_capacity(struct gendisk *disk)
{
	if (!disk->fops->unlock_native_capacity || ...) {
		printk(KERN_CONT "truncated\n");
		return false;
	}
	printk(KERN_CONT "enabling native capacity\n");
	disk->fops->unlock_native_capacity(disk);
	return true;
}
```

该机制是为 ATA 设备"容量被 HPA 隐藏"设计的。`mmc_bdops` (block.c:871-881) 没有这个回调 → 短路第一个分支 → 打 `truncated` 并返回 false → 落到 `size = get_capacity(disk) - from` 截断。所以 log 里同一行以 `truncated` 结尾。log 有时该行以 `, ` 结尾（`truncated` 被后续 KERN_CONT 合并/丢失），行为相同。

### 2.3 截断后的 mmcblk0p6 大小（数字验证）

- **EOD** = `get_capacity(mmcblk0)` = `card->ext_csd.sectors` = EXT_CSD SEC_CNT = **7634944 扇区**（3.909 GiB 字节 = 3728 MiB = 3.64 GiB）。旁证：preloader log `[SD0] Size: 3728 MB ... nblks(7634944)`、内核 `mmcblk0: mmc0:0001 H4G1d 3.64 GiB` 全部一致。
- **p6 绝对起点** = 1024（扩展分区基址，MBR p1） + 2418176（EBR1 表项2 rel_start） = **2419200 扇区** = 字节 `0x49D40000`。
- **截断后 p6** = 7634944 − 2419200 = **5215744 扇区** = 2,670,461,440 B ≈ **2.487 GiB**。

（`P6_EOD_REBOOT_SCENE_20260815.md` 表里 p6 起点写 0x49E00000，与二进制 dump 的 0x49D40000 不符；后者经自洽校验成立：p5 起点 0x3BB40000 + p5 大小 0xE200000 = 0x49D40000，即 p5 与 p6 首尾相接。）

### 2.4 截断是否影响后续 mmcblk0p6 访问 — 否（正常访问不受影响）

- 分区**照常创建**，只是 size 封顶到盘尾。`/dev/mmcblk0p6` 存在且 `bd_nr_sectors = 5215744`。
- 对 p6 的任何读写都由 block 层 `bio_check_eod` (block/blk-core.c:556-573) 兜底：请求超过分区容量 → 返回 `-EIO`，**不会下发到 MMC 控制器**，不产生硬件层越界。
- 内核在封顶范围内读写 p6 完全正常；唯一副作用是"声明的尾部 1.76 TiB 被静默丢弃"。
- 若用户态/文件系统企图写超出封顶大小的地址 → 得到 `-EIO`，不是复位。

---

## 3. p6 为什么声明 4292549119（≈2.0 TiB）而设备只有 3.64 GiB

### 3.1 设备分区表是 MBR+EBR（msdos），不是 GPT

`CONFIG_MSDOS_PARTITION=y`、`CONFIG_EFI_PARTITION=y`。GPT parser (`block/partitions/efi.c:595-607`) 先检查 MBR 是否有 0xEE protective 项（`is_pmbr_valid`）——本 MBR 是 0x05/0x83，无 0xEE → `goto fail`，GPT 路径**根本不读盘**，分区表由 `msdos.c` 独占解析。内核 log 的 `p1 < p5 p6 > p2 p3 p4`（`< >` 是 msdos.c 扩展分区标记，P6 文件 §3.1 已记录）也证明走 msdos。

### 3.2 EBR1 二进制 dump（`1/EBR1`，零修改备份）实际值

| Linux 分区 | 来源 | type | 绝对起点 | 声明 size | 判定 |
|---|---|---|---|---|---|
| p1 | MBR 表项1 | 0x05 扩展 | LBA 1024 (0x80000) | **0xFFFFFFFF 扇区** | 内核只建 2 扇区占位（`msdos.c:676-679`） |
| p2 | MBR 表项2 | 0x83 | 0x900000 | 0xA00000 = 10 MiB | 正常 |
| p3 | MBR 表项3 | 0x83 | 0x1300000 | 0xA00000 = 10 MiB | 正常 |
| p4 | MBR 表项4 | 0x83 | 0x3740000 | 0x38400000 = 900 MiB = system.img | 正常 |
| p5 | EBR1 表项1 | 0x83 | 0x3BB40000 | 0xE200000 = 226 MiB = cache.img | 正常 |
| **p6** | EBR1 表项2 | 0x83 | 0x49D40000 | **4292549119 扇区 (0xFFDB19FF)** | ❌ 超 EOD → 内核截断到 5215744 |

p6 原始字段：`rel_start=2418176 (0x24E600)`，`nsec=4292549119 (0xFFDB19FF)`。

### 3.3 0xFFDB19FF 哨兵的来历

```
0xFFFFFFFF − 2418176 = 4294967295 − 2418176 = 4292549119 = 0xFFDB19FF
```

**p6 的 size 字段 = 0xFFFFFFFF − p6 自己的 rel_start**，即 MTK EBR 生成器把最后一个逻辑分区（usrdata）填到"32-bit LBA 可寻址上限 0xFFFFFFFF"（≈2 TiB）。MBR 扩展分区表项也填 0xFFFFFFFF（`msdos.c:41-49` 按 32-bit le32 读取）。也就是说：**p6 声明的是"一直到 2TiB 盘尾"，不是"实际 4GB 盘尾"**。对 4GB eMMC 而言这是出厂表固有行为（哨兵值），不是坏分区、不是新引入的修改。

（严格说 size 按 `0xFFFFFFFF − rel_start` 计算、而绝对起点还含 +1024 扩展基址，故 p6 声明末端 = 4294968319 扇区 = 0x1000003FF，比 0xFFFFFFFF 多 1024 扇区，进一步说明这是"拍脑袋哨兵"而非精确计算。）

### 3.4 scatter 文件 vs 实际 EOD（另一处不一致）

`1/MT6572_Android_scatter.txt` 布局末端（bmtpool 结束）：

```
0xE8700000 + 0x1500000 = 0xE9C00000 = 7659520 扇区 = 3740 MiB
实际 EOD = 7634944 扇区 = 3728 MiB
差 = 24576 扇区 = 12 MiB
```

**scatter 线性布局本身比实际 Hynix H4G1d 用户区大 12 MiB**（SPFT 按标称 4GB eMMC 生成）。即"分区表声明超过实际容量"在 MBR/EBR 和 scatter 两处都存在，只是形式不同：EBR 用 2TiB 哨兵，scatter 用 3740MiB 尾值，都 > 实际 3728MiB。

另注意：scatter 各分区线性地址与设备上 MBR/EBR 的绝对地址普遍差 0xC00000（如 cache scatter@0x3C740000 vs EBR@0x3BB40000）——scatter 是 SPFT 下载映射，on-disk MBR/EBR 是内核读的真相，两者生成器不同；内核以 on-disk 为准。

### 3.5 单位澄清

`core.c:533-534` 的 `size/from` 是 `sector_t`（512B 扇区）。4292549119 扇区 × 512 = 2,197,785,148,928 B ≈ **2.0 TiB**。任务背景"p6 声明 4.29GB"是把扇区数当字节数的误读。

---

## 4. 分区超 EOD 是否可能是 preloader 重启诱因 — 否

### 4.1 分区扫描不产生超 EOD 的读

`msdos.c:parse_extended` 只读 LBA 1024（EBR1）；EBR1 内 3/4 表项为空，扩展链在 EBR1 即终止（`msdos.c:208-217` 找不到下一个扩展指针 → done），**不会沿着 p6 的 2TiB 声明去读任何扇区**。`read_part_sector` (core.c:713-732) 只对 `n >= get_capacity` 的读打 `access_beyond_eod`（本场景不触发），分区表整体可读 → `blk_add_partitions` 正常走完。**扫描阶段 0 次越界读、0 个 I/O 错误。**

### 4.2 截断无副作用

截断只改分区 size，然后 `add_partition` 建块设备。后续任何越界请求被 `bio_check_eod` (blk-core.c:556-573) 以 -EIO 拦下，物理层（MSDC/CMD 层）**永远不会看到超出物理盘尾的 LBA** → 不存在"eMMC 访问异常 → 硬件复位"通道。

### 4.3 时序/因果链断裂

- p6 EOD 截断发生在 `mmc probe` 阶段（204801 log `[0.69s]`，171058 log `[8.4s]`，取决于枚举时序），是一次性事件，之后内核对该分区零操作。
- 30.55s 复位时板子空闲在 shell（无 mount/dd/fsck/分区访问，P6 场景文件 §4 已证）。**0.7s 的事件与 30.55s 的复位之间没有任何持续 eMMC 活动**，不存在"延迟 30s 触发的 eMMC 异常"。
- 复位形态 = 回 preloader 的无标识硬件复位（非内核 panic/打印），与 eMMC 错误路径（打印 + I/O error + 无复位）不符。

### 4.4 历史证据（同一警告下存活远超 30s）

- `z1_boot_mainline_20260815_171058.log`：12 次 boot，每次都有该行，其中一次存活到 **678s**。
- `z1_boot_mainline_20260815_200224.log` boot3：p6 EOD 后跑完 CONNSYS 5 模块 insmod + WiFi func-on，全程正常。
- `z1_emmc_test_233012.log`（旧 1.82GiB 状态）：`/proc/partitions` 显示 p6 = 2607872 块，同样是封顶后的合法值，读 `/dev/mmcblk0p4` 无错误。

> 结论（源码+场景双线一致）：**p6 EOD 截断与 30.5s 重启无关**。30.5s 固定时长复位的最可能根因是 WDT 饿死（详见 `P6_EOD_REBOOT_SCENE_20260815.md` §5：mtk_wdt probe 见 LK 遗留 WDT_MODE_EN → 硬件倒数，init 喂狗一次性 `echo V` 不常驻 → 到点咬死），与 p6 无因果。

---

## 5. 结论（本文件范围）

1. 源码层面确认 `extends beyond EOD` 是**通用 block 层**行为（`block/partitions/core.c:549-563`），mmc 侧无特殊处理，唯一与 mmc 相关的变量是"EOD 值 = mmc 报告的整盘容量"。
2. 超 EOD 的分区被**截断并照常创建**（p6 → 5215744 扇区 ≈ 2.487 GiB），不丢弃、不报 I/O 错、不影响封顶范围内访问；越界访问由 `bio_check_eod` 兜底为 -EIO。
3. p6 声明 4292549119 **扇区**（≈2.0 TiB）是 MTK EBR 对最后 usrdata 分区的"到 32-bit 上限"哨兵；设备实际 7634944 扇区（3.64 GiB）。scatter 布局也超过实际容量 12 MiB。**出厂表固有，非新坏分区。**
4. 分区超 EOD **不是** preloader 重启诱因（无超界读、无持续 eMMC 活动、历史存活 boot 佐证、硬件复位形态不符）。30.5s 重启另查（WDT 喂狗，见 P6 场景文件）。

---

## 6. 关键代码行索引（`fork_linux/`）

| 文件 | 行 | 内容 |
|---|---|---|
| block/partitions/core.c | 533-563 | `blk_add_partition` EOD 检查 + 截断（`size = get_capacity(disk) - from` 在 562） |
| block/partitions/core.c | 517-528 | `disk_unlock_native_capacity`（mmc 无 fops → 打 truncated） |
| block/partitions/core.c | 582-641 | `blk_add_partitions`（含 `access_beyond_eod` 分支 622-628） |
| block/partitions/core.c | 713-732 | `read_part_sector`（越界读打 `access_beyond_eod`） |
| block/partitions/core.c | 294-332 | `add_partition`（`bdev_set_nr_sectors` 用截断后 len） |
| block/partitions/msdos.c | 130-221 | `parse_extended`（逻辑分区；i<2 项不查边界 → p6 原始 size 直接上报） |
| block/partitions/msdos.c | 581-717 | `msdos_partition`（主分区 + 扩展分区 `< >`） |
| block/partitions/efi.c | 595-607 | `is_pmbr_valid`（非 protective MBR → GPT 退出，不走盘尾读） |
| block/blk-core.c | 556-573 | `bio_check_eod`（越界请求 -EIO，不下发控制器） |
| drivers/mmc/core/block.c | 871-881 | `mmc_bdops`（**无 unlock_native_capacity**） |
| drivers/mmc/core/block.c | 2629 | `set_capacity(md->disk, size)` — EOD 来源 |
| drivers/mmc/core/block.c | 2657-2677 | `mmc_blk_alloc`（size = `ext_csd.sectors`） |
| drivers/mmc/core/block.c | 3003-3037 | `mmc_blk_alloc_parts`（boot/gp 硬件分区，与 p1-p6 无关） |
| drivers/mmc/core/mmc.c | 318-328 | `mmc_part_add`（硬件分区登记） |
| drivers/mmc/core/mmc.c | 413-427 | SEC_CNT → `ext_csd.sectors` 解码 |
| drivers/mmc/core/mmc.c | 679-715 | `mmc_read_ext_csd` |

## 7. 数据来源

- 分区表 dump: `/home/<user>/claude/6572/1/MBR`、`/home/<user>/claude/6572/1/EBR1`（二进制解析见本文件 §3.2，p6 字段 `rel=2418176 nsec=4292549119(0xFFDB19FF)`）
- scatter: `/home/<user>/claude/6572/1/MT6572_Android_scatter.txt`（末端 0xE9C00000 = 7659520 扇区）
- EOD 旁证: preloader log `[SD0] Size: 3728 MB ... nblks(7634944)`（`z1_boot_mainline_20260714_170618.log:113` 等多处）、内核 `mmcblk0: mmc0:0001 H4G1d 3.64 GiB`
- 主 log: `mainline_recon/z1_boot_mainline_20260815_204801.log`（两次 boot 均 `[0.69s] p6 size 4292549119 extends beyond EOD, truncated`）
- 存活佐证: `mainline_recon/z1_boot_mainline_20260815_171058.log`（存活 678s）、`z1_boot_mainline_20260815_200224.log`（boot3 跑完 CONNSYS）
- 场景级重启分析: `mainline_recon/P6_EOD_REBOOT_SCENE_20260815.md`（WDT 饿死根因、30.55s 复位点）
