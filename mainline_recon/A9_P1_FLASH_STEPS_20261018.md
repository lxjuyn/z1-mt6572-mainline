# A9 P1 出片与刷写判读（2026-10-18 凌晨）

## 0. 本轮做了什么（一句话）
把 A9 适配从 stock 3.4.67 线切到 **mainline 7.0-rc7 线**，修复了配置管线两处结构性缺陷，
补齐 A9 内核符号门 + ashmem/quota2 移植，并产出两个可刷 boot.img：**A（安全基线）** 与
**B（A + eMMC 4-bit 单点变化）**。分支 `wip/a9-mainline`，基线 `main`=`ffdff0653`。

## 1. 交付物与指纹（详见 out/BUILD_MANIFEST 与 bridge_backups/products/a9p1_*）
| 件 | 大小 | sha256 |
|---|---|---|
| `out/boot_mainline_a9p1_A.img` | 6,074,368 | `c0b03a5330bd659c26cab2a8f9a41623e70416e5e929c10f49b1cf3c900ecb74` |
| `out/boot_mainline_a9p1_B_emmc4b.img` | 6,074,368 | `56b6c9b48848403047f958daf349a80526811848adee1c643d9310ec725a2da1` |
| `out/ramdisk_a9p1.gz` | 1,276,535（body 1,276,023） | `2d066f75d7255722...` |
| zImage | 4,772,352 | `20444647d4936578...` |
| 探针 `z1_a9_probe` | 10,644 | `7d28a0718f1e0ea1...` |

boot 分区上限 6,291,456 → **余量 217,088 B**。`kernelrelease=7.0.0-rc7-z1+`（vermagic 未破，
`.ko` 同 vermagic：`xt_quota2.ko`/`xt_IDLETIMER.ko` 在 a9_wt/net/netfilter/）。
`linux,initrd-end = 0x84237877`（= 0x84100000 + 1,276,023，与 ramdisk 512B 头 0x04 字段互镜像，dtc 反解已核）。

## 2. 刷写顺序（你来执行）
```bash
cd <工作区根>   # 本机为 /home/<用户>/claude/6572
tools/flash_with_retry.sh bootimg out/boot_mainline_a9p1_A.img <工作区根>/1/boot.img 8
Z1_CAPTURE_BAUD=921600 ./z1_capture_mainline.sh 600      # 或 ./z1_capture_mainline.sh 180（C3 桥）
```
A 正常后再刷 B；任何异常先刷回 `1/boot.img`。**刷写才插下载线，正常开机拔掉。**
**刷机链已就绪**：`mtkclient_venv` 已重建并实测可用（`usb1/serial/Crypto/hexdump/colorama` 全部可导入，`mtk.py --help` 退出码 0；注意 `usb1` 模块由 PyPI 包 `libusb1` 提供，requirements 里的 PySide6/keystone 等 GUI/其它依赖对 CLI 刷写非必需，故按定向安装）。备选 `SP_Flash_Tool_Linux/` 亦在位（Download Only，只勾 bootimg，勿动 userdata）。

## 3. 串口判读（探针输出 `RESULT_STEP_*`）
| 项 | 预期 | FAIL 的含义 |
|---|---|---|
| `binder`（含 protocol_version）/`hwbinder`/`vndbinder` | PASS | binder 域未注册 → 查 `ANDROID_BINDER_DEVICES`/`ANDROID_BINDER_IPC` |
| `ashmem_open` + `set_prot_mask=0` | PASS | 回移未生效或 mdev 未建节点 |
| `timerfd_create_boottime`/`settime_boottime` | PASS | 内核时钟异常（对照 stock 3.4.67 的 EINVAL） |
| `cgroup_freezer/cpuset/cpuacct/memory/devices` | PASS | `libprocessgroup` 无地基 |
| `selinux_securityfs_mount` + `selinux_policy_loaded` | PASS | SELinux 未编入 |
| `xattr_security_selinux` | PASS | TMPFS_XATTR/EXT4_FS_SECURITY 缺失 → vold/installd 打标签失败 |
| `seccomp_filter` | PASS | minijail 不可用 |
| `dma_heap_system_open`/`dma_heap_alloc` | PASS | ION 缺位下无缓冲区后端 |
| `ion_dev`、`sw_sync_dev` | **FAIL 属预期** | 主线已删 ION / 旧 staging sync |
| `binderfs_registered` | ABSENT | PRESENT 说明配置漂移（我们刻意 `ANDROID_BINDERFS=n`） |
| `[a9cfg]` 段（`/proc/config.gz`） | 打印 40+ 条 =y/=m | 未打印说明 IKCONFIG_PROC 失效 |
探针文本还会被 `[klog]` 段 spool 到 USB ACM（`17ef:7439`? 不，本机 gadget 是 `0e8d:2006`），S6 那种无串口触点也能拿。

## 4. A/B 差分（已证明）
镜像同尺寸；差异仅：Android 头 21B（kernel_size 与 header SHA1）、MTK KERNEL 包装头 1B（长度）、
DTB 区 9,794B；**zImage 体内 0 处差异、ramdisk 及之后 0 处差异**，且两镜像内嵌 zImage 逐字节
等于树内 zImage。即 B 相对 A 的唯一实质变化 = `&mmc0` 的 `bus-width=<4>` + DAT1-3 pinmux
（PIN 47/48/49，据 `include/dt-bindings/pinctrl/mt6572-pinfunc.h`）。
**未证实项**：原厂 LK/硬件是否真的把 DAT1-3 走到 eMMC；B 若枚举失败即回 A。

## 5. 本轮修正的两个自我错误（记录以免重演）
1. `scripts/config --set-str` 我多包一层引号 → `CONFIG_ANDROID_BINDER_DEVICES="\\"` → 该行在
   `auto.conf` 以反斜杠结尾，**吞掉下一行** `CONFIG_GENERIC_IRQ_CHIP=y`，表现为 `rfkill`/`irq_gc_*`
   链接断裂。据此我曾误判"本仓 clean 构建必然断链"并错误关闭 `RFKILL_INPUT`；已纠正并恢复
   （见 `070e6cb61` 与 `A9_CONFIG_GATE.md` 第 3 节）。
2. 打包脚本两处 bug（`do_probe` 取错参数、awk 前缀匹配吞掉 `INITRD_END_VERIFIED` 导致 dts 一度
   未修改）——第二个尤其危险：它会让"我以为改了 initrd-end"而实际没改，正是 v13.1 截断事故模式。

## 6. 交给 AOSP 机器的接口
见 `mainline_recon/A9_DMA_HEAP_CONTRACT.md`：vermagic、符号清单、`.ko`、uapi 头、
`androidboot.*` 必须进 DTS chosen（LK 不转发 boot.img cmdline）、`linux,initrd-end` 规则、
ION→dma-heap 两条方案；并明确 stock 线那套 `ro.z1.stock_kernel` 门控/禁用 hwservicemanager
的补丁在本线**应当撤除**。
