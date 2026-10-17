# Z1 Android 9 线交接梳理（2026-10-17）

性质：只读梳理 + 异地备份，未改动任何内核源码、镜像、脚本或刷机链。
取证方式：本机 `fork_linux` 停在 `main`（工作树未动），全部 a9 内容通过 `git show/diff/ls-tree` 从远端 ref 读取；结论性技术断言（第 4 节配置管线）由我另行独立核验，标注了证据行号。

---

## 1. 两条路线，别再混

| | 路线一 mainline+A9 | 路线二 stock+A9（**当前主线**） |
|---|---|---|
| 内核 | 本地 Linux 7.0-rc7（`fork_linux`，改配置+真源码） | **原厂 3.4.67 二进制，一字未动** |
| userspace | LineageOS 16 (Pie) | AOSP/LOS16 stock 产品 + 25 份补丁 |
| 分支 | `origin/wip/android9`（86 提交，main 的直接后代） | `origin/wip/android9-stock-s1`（10 提交，孤儿树） |
| 是否需重编内核 | 需要（8 个提交全动构建输入） | 不需要（boot.img 自 S1F 起逐字节同一） |
| 上板最远到达 | 从未验证 A9 启动 | SystemServer 中段，未达 `sys.boot_completed` |

## 2. 分支拓扑（已核实）

- `origin/main` = `ffdff0653`（2026-09-03，tag `v13.5`）— 当前唯一已验证基线。
- `origin/wip/android9` = `c6501138e`，merge-base 即 `main`，`main..ref` = 8 提交，`git diff --stat main` = **14 文件 +1762/−59**，无 `android9/` 无文档。
- `origin/wip/android9-m7-userspace` = `b425b9e41`（root `29fc5a419`，3 提交，**孤儿**，51 文件）。
- `origin/wip/android9-stock-s1` = `868b4e728`（10 提交，同 root，**是 m7-userspace 的后代**；`android9/` 75 文件 + `tools/` 20 + `mainline_recon/` 14）。
- 三条 a9 ref 上**均无 tag**。远端最后一条时间 2026-10-04 04:11，此后 13 天无新提交。

## 3. 路线二：S1 → S1I 实机故障链（引自该分支 `mainline_recon/*.md`）

| 修订 | 观察到的 fatal（原报告措辞） | 根因 | 修复层 | 验证状态 |
|---|---|---|---|---|
| S1D `STOCK_S1D_FASTBOOT_DIAG` | ACM 身份出现又同秒消失 → preloader → fastboot `0bb4:0c01`；"No device log payload was captured" | **未知** | init 阶段打印 + PID1 fatal 15s 窗口 | 全运行时未证；"does not identify the fatal root cause yet" |
| S1E | 刷完立刻回 fastboot，无 ACM payload | `LOG(FATAL)` 走 `InitAborter` 而非 SIGABRT，窗口在 DoReboot 前不生效 | `system__core__init__log.cpp.patch` | "actual Android startup failure remains unknown" |
| S1F | `Cannot open /proc/sys/vm/mmap_rnd_bits` | 3.4.67 ARM32 **无该 sysctl 接口** | `init__security.cpp.patch` 按 ENOENT 放行 | **首个真 fatal**；S1E 实机日志已证 policy26 加载、二阶段 init 起 |
| S1G | SurfaceFlinger 在 `RenderEngine::overrideUseContextPriorityFromConfig()` 空指针段错 | stock 产品只打包 configstore **1.1**，HIDL 查找要 **1.0** impl 前缀 | 新增 `@1.0-impl.z1stock` + RenderEngine 守卫 + wificond/netd 不请求 hwbinder | system build PASS、e2fsck PASS、闭包 59 svc/58 roots、补丁反查 24/24；"not yet been run on the device" |
| S1H | 电池线程 fatal 后**约每 16 秒重复**，`sys.boot_completed` 未置位 | `BatteryService.logOutlierLocked()` 在 system provider 就绪前读 `Settings.Global` | `BatteryService.java.patch`（延到 `PHASE_SYSTEM_SERVICES_READY`）+ 镜像级注释 audioserver 两个远程 `onrestart` | 独立 Java 检查复现旧异常、新代码 12 项 PASS；"尚未证明进入桌面" |
| **S1I** | `AlarmManagerService` 干掉 SystemServer：3.4.67 `timerfd` 对 `CLOCK_BOOTTIME` **返 EINVAL** | 老 timerfd 不认 BOOTTIME，但认 `CLOCK_BOOTTIME_ALARM` | JNI 侧仅对该 EINVAL 回落（`com_android_server_AlarmManagerService.cpp.patch`） | `libandroid_servers.so` ARM 编译 PASS、镜像字节与产物一致、host C++ fixture 6 类分支 PASS；**"Runtime on this exact S1I revision has not been tested. The next serial-log boot is needed."** |

S1I 自带的方法论警告：回落会改变非唤醒闹钟的功耗语义；正解是内核 `fs/timerfd.c` 一行接受 `CLOCK_BOOTTIME`，但**该工作区没有 Z1 3.4.67 内核源码**。

结构性限制（贯穿全程）：SwiftShader 软件渲染、SELinux permissive、无 hwbinder（大量硬件服务本地 passthrough 或停掉）、audio 为静音软件 HAL、keymaster 软件后端无 TEE、电池无 NTC 校准、gralloc 是 goldfish fbdev 拷贝且自称未编译未上板。

### 上板判据（省一轮试错）
- S1F/G/H/I 共用同一 boot.img `e9b4e412…`（内含原厂 3.4.67，kernel body sha256 `f9afaa2a…`，3,336,776 B）。**只需刷 `system`，SP Flash Tool 用 Download Only 且保留 userdata**；scatter 用"之前同一份已成功使用的"——**该分支文档未记录 scatter 文件名**（缺口，见第 7 节）。
- 采集：`tools/z1_capture_android9_acm.py --stock`，认 USB 身份 `17ef:7439` / serial `Z1STOCK20261003`；设备侧 helper `/z1_bootdiag`，标记文件 `/z1_stock_kernel`，块别名 `/dev/block/z1-stock-{system,cache,data}`。
- 分区：boot ≤ 6,291,456；system(p4) = 943,718,400（分区 `0x38400000`）；cache(p5) 236,978,176；**userdata(p6) 几何未实测，禁止格式化**。

### 产物哈希台账（分支文档所载）
S1 最终 boot `20e0667b5483…`(5,320,704 B) / system `b180922ee235…`(943,718,400 B)；sepolicy `c3ffbe20a780…`(394,417 B，**v26**)；init `4d7a740da2cf…`；A7 基线 boot `47566a84ea47…`；打包审计测试像 `e16318c3…`（非发布件）。目录约定 `out/A9_STOCK_S1*_<日期>/`、`bridge_backups/products/products_stock_s1*_<ts>/`。

## 4. 路线一：mainline 侧 8 提交与**配置管线断裂**（我独立核验过）

改动面：ashmem 内置（`drivers/android/ashmem.c` 985 行 + uapi）、`xt_quota2.c`(308) / `xt_IDLETIMER.c`(+244) 新源码、`generic-adc-battery.c` 温度通道修正、dts bootargs 追加 `androidboot.hardware=z1 androidboot.selinux=permissive`、`z1_defconfig` 累计 +55 行。提交自述措辞："(build verified)"、"Kernel and modules cross-build successfully"、"Device boot is not yet verified"、"next hardware boot pending"。

**雷（证据行号在该 ref 上）**：
- 分支 `.config`：`958:# CONFIG_NETFILTER is not set`、`952:# CONFIG_IPV6 is not set`、`927:# CONFIG_XFRM_USER is not set`、`5770:# CONFIG_FUSE_FS is not set`、`5746:# CONFIG_EXT4_FS_SECURITY is not set`、`175/176/178` CGROUP_FREEZER/CPUSETS/CGROUP_CPUACCT 均 not set；而 `ANDROID_ASHMEM=y`、`ANDROID_BINDER_IPC=y`、`SECURITY_SELINUX=y` 在（`.config` 最后一次被改是第 1 个 a9 提交 `616164412`，共 11 条提交动过它）。
- 分支 `z1_defconfig`：有 `NETFILTER=y`、`IPV6=m`、`XFRM_USER=m`、`NETFILTER_XT_MATCH_QUOTA2=m`、`NETFILTER_XT_TARGET_IDLETIMER=m`、`FUSE_FS`、`EXT4_FS_SECURITY`、`MEMCG`、`CGROUP_FREEZER`；但 `ANDROID_ASHMEM / ANDROID_BINDER_IPC / SECURITY_SELINUX / ZRAM` **出现 0 次**。
- 直接后果①：用 `.config` 增量编译，因 `NETFILTER_XT_MATCH_QUOTA2 depends on NETFILTER_ADVANCED && PROC_FS`（该 ref `net/netfilter/Kconfig:1524-1526`），新写的 quota2/idletimer **压根不参与编译**，"编译通过"是假象。
- 直接后果②：任何人执行 `make z1_defconfig`，Android 三大支柱（ashmem/binder/SELinux）+ zram **被整体抹掉**。
- 另：该 ref 的 `linux,initrd-end` 仍是 `0x84235d32`（v13.5 ramdisk 值），A9 ramdisk 必须走 `tools/z1_prepare_android9_ramdisk.py` 的 `fdtput` 副本，不能想当然。

## 5. 本机（`/home/<user>/claude/6572`）能否续作——分活看

**能做**：内核侧改动与重编（需先解第 4 节的雷）、a9 补丁审阅、纯主机静态测试、ramdisk/boot.img 打包、文档与交接。
**不能做**：
- **A9 全量 systemimage 编译**：脚本要求 `android9/los16/` 整棵 LineageOS-16 树（`build_z1.sh` 用 `TARGET_NO_KERNEL:=true`，`lunch lineage_z1-userdebug`，还需 `host-tools/python2`、jdk9 预编译）。本机无该树、无 `python2`/`repo`/`mkuserimg_mke2fs`/`simg2img`，且根盘（就是这块 120G 移动硬盘 `sda`）清理前仅 17G——LOS16 需要 200G+ 净空，**物理不可能**。
- **上板刷写**：`tools/flash_with_retry.sh:27` 硬依赖 `mtkclient_venv/bin/python`，本机无该 venv；无 `boot_a9_pie.img`/`system_a9_fixed.img`；当前无 MTK 下载口（只有 CH340 `/dev/ttyUSB0`，无 C3 桥）。
- 路径坑：a9 脚本按"仓库根 = 6572/"假设默认 `--dtb/--zimage`，本机仓库根自身就叫 `fork_linux`，会解析成 `fork_linux/fork_linux/…`，**必须显式传参**。

## 6. 本次会话新增的状态变更（便于对账）

- 备份（脱敏后，推送前均跑 9 项密钥 + 5 项隐私门禁扫描，全 0）：
  - `lxjuyn/z1-mt6572-mainline` 新 orphan 分支 **`docs-z1-recon` = `40f2208b40`**：234 篇 md（`mainline_recon/*.md` 232 + `out/*.md` 6 + 根级 4，减 9 份重复模板 README）。此前这些 Z1 调研结论**从未被 git 跟踪**，只有本地一份副本。排除 `CLAUDE.md`（按记录含 GitHub PAT）与 `AGENTS.md`（内含本机 sudo 口令）。
  - `lxjuyn/r11s-mainline-kernel` 新 orphan 分支 **`workspace-public` = `a10ad3c7ab`**：144 文件脱敏快照。**剔除 229 个原始取证件**（`logcat.txt` 2.3M、`dmesg (1).txt`、`recon/p0_adb_*`、`recon/p2_adb_*`、v14a/v14b 完整 dmesg、camera 取证目录）+ 5 个智能体本地配置文件，清单与 sha256 前缀写在该分支 `EXCLUDED_PRIVACY.md`。
  - 曾把 r11 全历史原样推到公开仓（分支 `workspace-backup`）后发现其中 `logcat.txt` 含 43 处 MAC、36 处 SSID/BSSID、17 处邮箱及 IMEI/ICCID 字段，**已删除该分支**；注意 GitHub 在 GC 前仍可能按 SHA 访问，介意则需走支持渠道。
  - 另曾误建私有仓 `lxjuyn/r11s-mainline-workspace`，经确认**已删除**。
- 本机 git 变更：`fork_linux` 仅多 1 个本地分支 `docs-z1-recon` 与远端 ref 元数据（工作树仍在 `main`、干净）；`r11` 多 `unsaved_20261017/`（u-boot 未提交改动 patch + R11s dts + 说明，共约 7KB）、1 个提交 `440babe`、以及首次配置的 `origin` 远端（此前 22 个提交无任何远端）。
- 凭据：`~/.git-credentials` 由 `https://<token>@github.com` 修正为 `https://lxjuyn:<token>@github.com`（push 才可用），原件备份 `~/.git-credentials.bak-20261017`。**该 token 在本会话被打印过一次，建议轮换**。
- 磁盘：清理脚本 `phase1`（T1+T2）运行中；r11 侧 T3 尚未开始。

## 7. 后续作业清单（按优先级）

1. **决定在哪个设备上继续 A9**：S1I 只差"刷 system + 抓一次串口"，但本机做不了编译也刷不动。需要那台有 `los16` 树与 a9 镜像的机器，或在本机补 `mtkclient_venv`（刷写只需 venv + 镜像，不需 LOS16）。
2. 上板刷 S1I，验证 `timerfd_settime` 与更晚阶段；同时把 scatter 文件名、S1I 镜像 sha256 记进 PACKAGE_NOTE（当前分支文档**没记 scatter 名**，是复现断点）。
3. 解第 4 节的配置管线雷：把 Android 必需符号补进 `z1_defconfig`，并让 `.config` 与 defconfig 单一来源化；修完必须做一次 `make z1_defconfig && 符号集 diff` 的确定性校验，再谈重编 zImage。
4. 若要根治 timerfd：取 Z1 3.4.67 内核源（原厂包）改 `fs/timerfd.c` 接受 `CLOCK_BOOTTIME`，替掉 S1I 的功耗侧回落。
5. 本机待补：`mtkclient_venv`、（如要上板）C3 桥或 CH340 @921600 链路确认；`bridge_backups/` 目录在本机缺失，备份纪律目前无法执行。
6. 悬置问题：S1A/S1B/S1C 无独立文档（命名从 S1 直接跳 S1D）；a9 镜像与 LOS16 树在哪台机器；路线一是否还打算继续投入。
