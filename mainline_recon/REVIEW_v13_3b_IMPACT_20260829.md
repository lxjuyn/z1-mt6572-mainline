# REVIEW v13.3b 影响面/回归风险评审 (2026-08-29)

> 评审人维度: 影响面 (代号 Daniel)。只读审查，除本报告外未修改任何项目文件
> （评审中 extract_boot.py 曾在 `out/` 落 5 个中间文件 kernel_region.bin /
> kernel.xz / kernel_xz_*.xz / ramdisk.gz，已当场删除还原；`out/ramdisk_v13_4_acm.gz`
> 为他人并行产物，未动）。
> 对象: `out/boot_mainline_v13_3.img` (v13.3b 重打包, sha256 `814ef070…`, 4,884,480 B)。
> 基线: `out/boot_mainline_v13_2a1.img` (sha256 `d438dd8d…`, 上一可用已上板验证)。

## 总裁决: 影响面可控，可直接刷（附 3 条缓解动作）

v13.3b 相对已上板验证的 v13.2a1 的全部实质差异 = **一块 ramdisk（/init 六处纯自报日志
+ tty0 守卫）+ dtb 一个 4 字节属性（initrd-end）**。zImage 逐字节相同，cmdline 逐字相同，
无任何驱动/config 行为面变化，回滚链两件均完好。缓解动作见文末检查单（核心：下载线
开机后再插、验证工具只走 by-id 口、勿用脏工作树重打旧版镜像）。

---

## 1. 改动面精确清单

**方法**：`bridge_backups/products/products_v13_3_20260827_015723/` 已被 08-29 01:59 的
v13.3b 产物覆盖刷新（4 个文件 sha256 与 `out/` 完全一致，init 脚本 diff exit 0），
不能当"旧版"用。真正的旧版 v13.3 ramdisk 保有改前备份
`bridge_backups/ramdisk_v13_3_pre_review_fix_20260829_015349.gz`（1,069,896 B =
512B 头 + 1,069,384 body）。剥头、gunzip、cpio 解包后 diff 其 `/init`；boot 头用
`tools/extract_boot.py` + 手工 dd 比对。

v13.3b 相对 **v13.2a1** 的每一处变化：

| # | 项 | v13.2a1 | v13.3b | 性质 |
|---|---|---|---|---|
| 1 | ramdisk body | 1,069,384 B (0x105148) | 1,069,518 B (0x1051ce, +134) | 内容变化，见 #2 |
| 2 | `/init` | 2,664 B | 3,159 B | 六处 diff，全部是 `[acm]` 成功/失败自报 echo（gadget dir / VID-PID / configs/c.1 / `ln -s acm.usb0` 结果——此前 symlink 失败静默）+ tty0 getty 加 `[ -e /dev/tty0 ]` 守卫。**无执行流/命令集变化，纯可观测性补丁** |
| 3 | dtb `linux,initrd-end` | 0x84204f41 | **0x842051ce** | 全部差异仅此 3 字节（0x842051ce vs 0x84204f41 尾部 2 字节不同 + 长度注释）；= 0x84100000 + 0x1051ce，与新 ramdisk 严格配对 |
| 4 | boot 头 cmdline | `console=tty0 console=ttyS0,921600n8 root=/dev/ram panic=10 loglevel=7 clk_ignore_unused regulator_ignore_unused oops=panic sysrq_always_enabled rdinit=/init earlycon=mtk8250,0x11005000` | **逐字相同** | 无变化 |
| 5 | zImage | — | **逐字节相同**（两镜像 kernel 段 3 字节差异全在 dtb initrd-end）| eMMC 预热修复不丢 |
| 6 | MTK KERNEL 头 size 字段 / kernel_size | 0x3a1f24 / 3,809,572 | 0x3a1f20 / 3,809,568 | v13_2a1 的 kernel 段 dtb 之后多 4 字节打包冗余（v13.2a1 已上板启动成功，证明无害）；v13.3b 反而更干净 |
| 7 | task 前提勘误 | — | — | "cmdline ttyMT0→ttyS0" **不是 v13.3b 的改动**：两镜像 cmdline 已同为 ttyS0；该切换发生在 v13.2→v13.2a1（wip commit 423b07c5b 记录）。PACKAGE_NOTE_v13_3.md 第 25 行"评审'应改 ttyMT0'为误报，未改"与字节证据一致 |

镜像自检（本次实测）：`ANDROID!` magic ✓；4,884,480 B ≤ 6MB 分区上限 ✓；镜像内嵌
ramdisk（0x3a3000 起，512B MTK ROOTFS 头 `88 16 88 58` + 长度字段 `ce 51 10 00` =
1,069,518）与 `out/ramdisk_v13_3_acm.gz` **sha256 逐字节一致** ✓；镜像内嵌 dtb 反编译
`linux,initrd-end = <0x842051ce>` ✓。initrd↔ramdisk 配对链完整。

**结论**：改动面收敛在 initramfs 可观测性与 initrd 长度配对，回归风险=低。
dtb 内嵌 `chosen/bootargs` 兜底串仍是 `ttyMT0,921600n8`（陈旧残留）：LK 经 ATAG
传 cmdline 时被覆盖无影响；仅当 ATAG cmdline 缺失时兜底 console 名才落空。只记录不改。

## 2. ttyACM0 抢口风险核实

逐脚本证据：

| 脚本 | 选口方式 | gadget 上线后风险 |
|---|---|---|
| `z1_auto_switch.sh` | **硬编码 `/dev/ttyACM0`**（第 4 行），且向该口发 `[[BR:115200]]/[[BR:921600]]` 桥命令 | 会抢错口：若 gadget 先枚举为 ttyACM0，桥命令被打进 gadget 的 root shell（getty `-l /bin/sh`，无认证），桥速率不被切换、日志乱码 |
| `grab_acm.sh` | **硬编码 `/dev/ttyACM0`**（第 6/9 行） | 抓到的可能是桥的 UART 透传而非 gadget |
| `tools/acm_capture.sh` | **硬编码 `/dev/ttyACM0`**（第 15/19 行），还会 `sudo chmod 666` | 同上 |
| `z1_capture_mainline.sh` | by-id 四模式 `Espressif_*/GigaDevice_*/1a86_*/QinHeng_*`（第 27-32 行） | **安全**：gadget 枚举名是 `Z1/Z1 ACM/Z1MT65720001`，不匹配任何模式；且带 flock 防并发 |
| `z1cmd.sh` | 同上 by-id（第 10-15 行） | **安全**（头注释"发命令到 ttyMT0"已过时，实际落到 ttyS0 console 的 fallback getty，功能不受影响） |

**首次验证安全操作顺序**：
1. C3 桥 UART 线全程不动；**下载线保持拔出**，上电开机。
2. 串口（`./z1_capture_mainline.sh 600`）确认 `[acm] configfs mounted` →
   `[acm] acm linked into c.1` → `[acm] UDC bound` → `[acm] /dev/ttyGS0 ready` 全绿。
3. **此时才插下载线**（主板后置 USB，不经 hub），`lsusb` 应现 `0e8d:2006`；
   `ls -l /dev/serial/by-id/` 区分桥（Espressif/GigaDevice/1a86/QinHeng）与
   gadget（`usb-Z1_Z1_ACM_*`）。
4. 抓日志/交互只用 `z1_capture_mainline.sh` / `z1cmd.sh`；本轮禁跑
   `z1_auto_switch.sh` / `grab_acm.sh` / `tools/acm_capture.sh`。
5. 附带风险记录：gadget 对外是**无认证 root shell**（物理插入即 root），bringup 期接受。

## 3. p6 分区超界警示

- 日志证据（grep 需 `-a`，log 含 NUL 字节，普通 grep 会当 binary 漏检）：
  `mmcblk0: p6 size 4292549119 extends beyond EOD, truncated`——22 份
  `z1_boot_mainline_*.log` 含此行；0815 场景 log `z1_boot_mainline_20260815_204801.log`
  两次 boot 各一条。v13 时代（08-26/27）log **无**此行——CMD1 硬挂下 eMMC 根本没枚举到
  分区扫描，属预期。
- **勘误（任务前提不准）**：该警示**未**记录在 `V13_PACKED_20260825.md`（全文无
  p6/EOD/beyond）；定案在 `P6_EOD_REBOOT_SCENE_20260815.md` +
  `P6_EOD_VERDICT_20260815.md`：p6=usrdata，MTK EBR 对末分区惯用 2TiB 哨兵值
  （0xFFFFFFFF−start_lba），内核截到 EOD 封顶，无 I/O 错误，**历史上每次 boot（含存活
  678s 的健康 boot）都出现，与 30.5s 复位无关（根因是 WDT 饿死）**。
- **结论**：v13.3b 刷入后一旦 eMMC 枚举恢复，此行会重现——**预期噪声，非回归**，不作为
  刷写失败的判据。建议后续在 V13 文档补一行索引指向 P6_EOD 两份定案。

## 4. 回滚链完整性

- `1/boot.img`：存在，6,291,456 B（恰好 6MB 原厂全分区镜像），sha256 `2dd41702…`。
- `out/boot_mainline_v13_2a1.img`：存在，4,884,480 B，sha256 `d438dd8d…`；另有归档
  `bridge_backups/products/products_v13_2a1_20260827_012312/boot_mainline_v13_2a1.img`。
- `tools/flash_with_retry.sh` 回滚逻辑真实存在且完整：擦除失败/写失败/"写成功但读回
  校验失败"三条路径均先 `rollback_and_verify()`（mtk.py w + 完整读回 cmp）再重试；
  "干净没连上"与"半写"区分判定避免误回滚毁掉成功写入；回滚再失败 exit 3 停手禁止
  自动继续；bootimg 非 bootloader 类分区不强制回滚镜像，但 PACKAGE_NOTE 给出的命令
  显式带了 `1/boot.img` 兜底 ✓。
- **结论**：两级回滚链完好。注意脚本单次只接受一个 rollback 镜像（本轮=`1/boot.img`），
  退到 v13.2a1 需再手动刷一次。

## 5. cmdline 副作用：regulator_ignore_unused

时间线（读档证据）：
- **2026-08-14 进 cmdline**：`CMDLINE_PANIC_AUDIT_20260814.md`（§127"加——直接停掉
  39.5s 的 regulator 晚清理关机，与 DT always-on 双保险"）+ `BOOT_STABILITY_FIX_20260814.md`
  （~39.5s 重启循环修复：dts always-on `37a481a21` 为主 + 本参数免重编内核双保险）。
- **2026-08-15 WMT 文档"不建议加"**：`WMT_INSMOD_SLOW_VERDICT_20260815.md` §2 明说
  "**对 WMT 时序：regulator_ignore_unused 零影响**"（devm_regulator_get 纯查表不碰硬件）；
  其 §2.6"不建议加"的真实语境是——**当时 connsys 镜像的 cmdline 本来就没有它**，且若
  操作流程拿 39.5s `vusb/vmc/vmch: disabling` 日志当门控标记，加它会消掉该标记。
  这是针对旧 connsys insmod 人肉流程的告警，**不是"该参数有害"的定论**，与 08-14 已进
  v13 主链 cmdline 的事实不构成矛盾（两份文档描述的是不同镜像链）。
- `P6_EOD_REBOOT_SCENE_20260815.md` §0.2：带此参数时 30s delayed work 只 pr_warn 一行
  即 return，不碰任何 regulator；30.55s 复位系 WDT，与本参数无关。
- **现状**：v13.2a（上板验证：eMMC 枚举 + initramfs 解包 + /init 全成功）起每轮镜像
  （含 v13.2a1、v13.3b）都带它，属**已上板验证配置**。

**明确结论：保留。** 理由：与 dts always-on 冗余但无害（一代价：30s 时一行 pr_warn），
且 cmdline 属"已验证不动的部分"——本轮任何 cmdline 变化都是无谓的新回归面。下轮
（产品化收敛时）可与 `clk_ignore_unused`/`panic_print` 一起评估移除，届时 dts always-on
已是主修复，参数失去存在必要；但**不是本轮**。

## 6. Alpine p4 路线影响

- v13.3b `/init` 全文**无** switch_root / mmcblk0p4 / newroot 引用（old/new 两版 diff
  均无）；它只起 ACM gadget + console shell，**不挂任何 eMMC 分区**。
- Alpine 路线属 `boot_mainline_z1_stable.img`（8/16，6,021,120 B）家族：initramfs 挂
  p4 + `exec switch_root /sbin/init`（`ALPINE_BOOT_PATH_20260815.md` 方案 A）。
- **影响判定**：刷 v13.3b 仅覆盖 bootimg 分区 → Alpine 启动路径**暂不可用**（需刷回
  stable 家族镜像即恢复）；p4 上的 Alpine rootfs 数据**分毫不动**——且 v13.3b 的 init
  根本不碰块设备，从 Z1 侧无任何写 p4 的路径，天然隔离。**结论：无数据风险，仅路径
  切换成本，记录即可。**

## 7. USB 插拔时序

- 坑本身：AGENTS.md 坑3（=CLAUDE.md §5 第 2 条）"MTK preloader 看 USB LineState 判
  下载模式，插着下载线正常开会进重启循环。刷写才插下载线，正常开机拔掉；C3 桥 UART
  线全程不动"。
- 矛盾点：v13.3b 的核心验证（`lsusb` 见 0e8d:2006）**又必须**插下载线。
- **发现的缺口**：`out/PACKAGE_NOTE_v13_3.md` 第 68-69 行只写了"主机插线后 lsusb 应现
  0e8d:2006"，**未写"先开机、过 preloader 后再插线"的顺序，也未复述 LineState 坑**。
  若操作者按字面"刷完→插线→按开机键验证"，会正中坑3 的重启循环，且症状与"新镜像挂了"
  难以区分——这是本轮评审发现的最大操作面隐患。
- **结论：需缓解（口头上即可，不用改镜像）**。安全顺序即 §2 的五步：**先上电、串口见
  `[init]`/`[acm]` 自报（preloader 阶段已过）→ 再插下载线 → lsusb**。preloader 的
  LineState 检查只在早期启动发生，运行中插线只触发 gadget 枚举，正是我们要的。

## 8. eMMC 预热修复防护

实测核实：
- `git -C fork_linux log --oneline -5 main`：main 顶 = **b1380bf32**（2026-08-29 02:49，
  "msdc z1: CMD1 poll pre-warm … reset delay 10ms->50ms + msdc_irq loop bound"），
  其下 f9652087c（v13.3b dts/initrd-end）、984710f3f（v13.3）。
- `git branch --contains 8cec21a62` 仅列 `wip/v13.2a-msdc-fix`——**这不是缺口**：
  cherry-pick 产生新 SHA，内容等价性由以下证据背书：b1380bf32 与 8cec21a62 commit
  message 相同、diffstat 完全相同（drivers/mmc/host/mtk-sd.c，88+/5-）；main 工作树
  `mtk-sd.c` 含 `msdc_z1_prewarm_cmd1()`（:3397）+ probe 预热调用（:3906）+ msleep(50)。
- `git diff main wip/v13.2a-msdc-fix --stat`：两分支内核 **C 源零差异**，仅差 dts 与
  打包文档两笔（423b07c5b、8cec21a62 本体）。`main...origin/main` 无 ahead/behind = 已推送。
- **结论**：AGENTS.md §3.1 与 PACKAGE_NOTE_v13_3.md"⚠️勿从 main 重编 zImage"的警告
  **已过时**。建议 AGENTS.md §3.1 替换文案：

  > ✅ **更新 (2026-08-29)**：8cec21a62 已 cherry-pick 进 main（**b1380bf32**，mtk-sd.c
  > 88+/5- 与 wip 版逐字等效，main 与 wip 的内核 C 源已无差异，仅 dts/打包文档两笔
  > wip 独有 commit 未合并）。**从 main 重编 zImage 不再丢 eMMC CMD1 预热修复**。
  > 仍须遵守：(a) 工作树 `mt6572-z1.dts` 有未提交改动（如 v13.4 的 initrd-end
  > 0x8423664a）时，勿拿它重打 v13.3b 等旧配对镜像，重打旧版先 `git stash` 或按
  > HEAD=f9652087c 检出；(b) 重编前确认 .config 仍为 slim+XZ 配置；(c) 合并
  > 423b07c5b 后本条可整体撤销。

 PACKAGE_NOTE_v13_3.md 的"⚠️ 警示(修复 2)"段建议同步加一行指针，不必删（对尚不知
 cherry-pick 已发生的人仍是保险）。

**附带发现（不属 8 项清单但影响面相关，必须记录）**：
1. **fork_linux 工作树是脏的**：`mt6572-z1.dts` 有未提交 diff（initrd-end → 0x8423664a，
   为并行推进的 v13.4：`out/ramdisk_v13_4_acm.gz` 1,271,882 B 已于 08-29 03:16 出现在
   out/）。**若有人现在拿工作树重打"v13.3"，会静默配上 v13.4 的 initrd-end → initrd
   截断 → 复活 v13.0 时代 gzip read error**。缓解：v13.3b 只刷归档镜像（sha256
   `814ef070…`），v13.4 dts 改动先 commit 再动编译。
2. **备份卫生**：`products_v13_3_20260827_015723/` 被同名 v13.3b 产物覆盖刷新，8/27
   旧版 v13.3 boot.img（sha 599a3298 时代）已无处可寻（仅 ramdisk 有 pre_review_fix
   备份）。不影响本轮（v13.3 未刷过、已被 v13.3b 取代、不在回滚链上），但违反"改前
   备份"纪律的字面做法，建议后续归档用新目录不复用旧目录。

---

## 刷写前检查单

1. [ ] 镜像指纹：`sha256sum out/boot_mainline_v13_3.img` = `814ef070eb823d63acec76fc402e5d94c23da1f74861487589a6c25a5875f84c`（4,884,480 B）。
2. [ ] 回滚件在位：`1/boot.img`（2dd41702…）与 `out/boot_mainline_v13_2a1.img`（d438dd8d…）。
3. [ ] 下载线**拔出**状态上电；C3 桥 UART 线全程不动；起 `./z1_capture_mainline.sh 600`。
4. [ ] 刷写：`tools/flash_with_retry.sh bootimg out/boot_mainline_v13_3.img /home/<user>/claude/6572/1/boot.img 8`（先起 mtkclient 后按开机键；直插主板后置 USB，不经 hub）。
5. [ ] 串口验收（按序出现）：`[init] welcome … (v13.3 acm)` → `[acm] configfs mounted` → `[acm] gadget dir created` → `[acm] acm function created` → **`[acm] acm linked into c.1`（本轮新增的关键判据）** → `[acm] UDC bound` → `[acm] /dev/ttyGS0 ready`。
6. [ ] CMD1 判据：若 eMMC 修复生效，应见 mmc0 枚举 + `mmcblk0: p6 … extends beyond EOD, truncated`（预期噪声，见 §3）+ `[init] uname`；若第 3 次 CMD1 launch-post 后 600s 静默 = 修复未生效，走回滚评估。
7. [ ] **开机后**（见 [init] 自报）才插下载线 → `lsusb` 应现 `0e8d:2006`；`ls -l /dev/serial/by-id/` 确认桥口（Espressif/GigaDevice/1a86/QinHeng）与 gadget 口（usb-Z1_Z1_ACM_*）。
8. [ ] 交互/抓取只用 `z1_capture_mainline.sh` / `z1cmd.sh`（by-id）；本轮禁用 `z1_auto_switch.sh` / `grab_acm.sh` / `tools/acm_capture.sh`（硬编码 ttyACM0）。
9. [ ] 失败处置：刷写失败自动回滚 1/boot.img；exit 3 = 回滚也失败，停手人工。若新镜像功能异常，手动刷 `out/boot_mainline_v13_2a1.img` 即回上一可用点。
10. [ ] 验证通过后立即 `git add/commit/push`（含 v13.4 脏 dts 的处置决定），并按 §8 建议更新 AGENTS.md §3.1 警示文案。

—— 评审完毕。本报告为只读评审产物，未改动任何源码/镜像/脚本。
