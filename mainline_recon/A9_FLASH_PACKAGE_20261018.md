# A9 可刷写刷机包装配线（2026-10-18）

目标产物不是一个 `boot.img`，而是**能安全落到板子上的一套东西**：scatter + 镜像 + 指纹清单 + 步骤。
工具：`tools/z1/z1_a9_flashpkg.sh`（装配 + 预检）与 `tools/z1/z1_a9_scatter.py`（scatter 改写）。

## 1. 分区几何（读自原厂 `1/MT6572_Android_scatter.txt`，19 个分区）

| 分区 | linear_start | 容量 | 说明 |
|---|---|---|---|
| preloader | 0x0 | 0xC00000 | **原厂 scatter 里 `is_download: true` 且 `file_name:` 是空值** |
| mbr / ebr1 | 0xC00000 / 0xC80000 | 0x80000 | 分区表本体 |
| pro_info / nvram / protect_f / protect_s / seccfg / misc / expdb / bmtpool | — | — | 本就 false，校准数据，不许动 |
| **uboot** | 0x2920000 | 0x60000 | 里面是 **LK**，`is_download: true`（丢了它这台机器只能靠 JTAG） |
| **bootimg** | 0x2980000 | **0x600000 = 6 MiB** | 本项目所有 mainline 镜像的硬上限 |
| recovery / sec_ro / logo | 0x2F80000… | 0x600000/0x40000/0x300000 | `is_download: true` |
| **android** | 0x4340000 | **0x38400000 = 900 MiB** | system 分区（p4） |
| cache | 0x3C740000 | 0xE200000 = 226 MiB | p5 |
| **usrdata** | 0x4A940000 | **0x9DDC0000 = 2.47 GiB** | p6 —— 台账里"userdata 几何未实测"这条**由 scatter 补上了**；注意这是分区容量，里面现有文件系统状态仍未实测，仍禁止格式化 |

## 2. 这套工具做的事

- 生成 `MT6572_Z1_A9_scatter.txt`：**只放行 `bootimg`（提供 system 时再加 `android`）**，
  其余 10 个原本 `true` 的分区（preloader/mbr/ebr1/uboot/recovery/sec_ro/logo/cache/usrdata 等）
  一律改成 `is_download: false` + `file_name: NONE` 并逐条打印出来给人核对。
- 预检（任一不过就 `rc=3`，不产出可刷包）：boot 尺寸 vs 6 MiB、system 尺寸 vs 900 MiB、
  `ANDROID!` magic、system 形态（sparse `0x3AFF26AF` 或 ext4 superblock `0xEF53`）、
  回滚件 `<工作区根>/1/boot.img` 是否在位、scatter 里放行的文件名是否真在同目录。
- 出 `HASHES.txt`（分支/HEAD/镜像/ scatter 的 sha256）。**里面不写含用户名的绝对路径**——
  这类清单经常被人直接发出去。
- 打印两条刷法：SP Flash Tool（强调 **Download Only**，绝不是 Format All + Download）
  与 `tools/flash_with_retry.sh` 逐分区（带原厂回滚）。

实测：boot-only 与 boot+system 两种模式 `rc=0`；超容量 system 被拒（`rc=3`，报出超出 130,023,424 B）；
坏 magic 的 boot 被拒。正式包已装配在 `out/A9_flashpkg_p4/`（P4 D1 boot.img
`88fc207c…`，scatter `379de109…`，rollback 原厂 `2dd41702…`），当前是 boot-only ——
**缺的就是 A9 的 system.img**。

## 3. 写这个工具时踩到的两个坑（自检把它们抓出来的）

1. **不能用跨行正则改 scatter**。`re.sub(r'file_name:\s*\S+', …)` 在 `preloader` 那种
   `file_name:`（空值）+ 换行 + `is_download: true` 的块上，会把**下一行的 is_download 键整个吞掉**，
   生成出 `file_name: NONE false` 这种结构已坏的行。改为逐行处理。
2. **不能"只重新输出分区块"**。第一版按块重组，结果丢掉了文件头的
   `general: MTK_PLATFORM_CFG`（platform/project/storage/block_size）——那是 SP Flash Tool
   认这个文件的必需段，341 行被写成 323 行，产物直接不可用。
   现在改成整文件逐行原地改，并断言**行数、CRLF 数、每块键序列三者与原件完全一致**
   （`SCATTER-STRUCT-FAIL` 就是它抓到第 2 条的）。原厂 scatter 是 CRLF，读写都必须 `newline=''`。

## 4. 与 A9 出片链的衔接

```sh
# 1) A9 generic ramdisk 到位后，先出 boot.img（预算/initrd-end/载荷完整性自动核验）
A9_RAMDISK=out/ramdisk_a9.gz tools/z1/z1_a9_p2.sh pack 1b out/boot_mainline_a9p5.img
# 2) 再装配可刷包（boot+system 一起放行，其余分区全部关掉）
tools/z1/z1_a9_flashpkg.sh --boot out/boot_mainline_a9p5.img --system <system.img> \
      --dir out/A9_flashpkg_final
```
