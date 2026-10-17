# PACKAGE_NOTE v13.4 (Z1) + s6_v4 (S6) — 2026-08-29, host-side, no flash（v13.4r 评审修复重打版）

本轮 = 两项评审落盘 + W1 实质修复 + 共享新 ramdisk + 两台设备各一个可刷镜像。
评审报告: `mainline_recon/REVIEW_v13_3b_CORRECTNESS_20260829.md` (Ryan, GO-WITH-WARNINGS)
/ `mainline_recon/REVIEW_v13_3b_IMPACT_20260829.md` (Daniel, 影响面可控)。

## ⚠️ 本轮最重要修复 (W1): console 选择移入 dts bootargs
正确性评审板级日志铁证: **本启动链 LK 不转发 boot.img 头 --cmdline**
(8/26-27 内核 cmdline 与 dts bootargs 逐字相同, LK 参数全部缺席)。
因此 v13.3b 里 `console=ttyS0` 从未生效, /dev/console 实际落 LCD(tty0),
**串口在 Run /init 后必静默, "串口见 [acm] 自报"的验收方案本身不成立**。
修复: z1/s6 两 dts bootargs 改 `console=tty0 console=ttyS0,921600n8`
(mainline 8250 注册 uart0 为 ttyS0, 非 ttyMT0; 末位 console= 得 /dev/console
→ 串口拿内核日志尾部 + 末尾 exec getty; s6 同时删掉无效的 ttyACM0——那是
主机侧名, 设备侧 gadget 口是 ttyGS0)。boot 头 cmdline 保留同串镜像值
(本链无效, 仅镜像语义自洽)。

## 产物（v13.4r 实测，2026-08-29 14:43）
| 文件 | 大小 | sha256 |
|---|---|---|
| out/boot_mainline_v13_4.img | 5,083,136 B (≤6MB ✓, ANDROID! ✓) | 0f88cfcd0ad16a78329d1e7fb62f7b90b6fb89c1c091de935c8c254718bce2b2 |
| out/boot_mainline_s6_v4.img | 5,081,088 B (≤6MB ✓, ANDROID! ✓) | ff2280346405914374a4d042709a15877bd177cb323236fbcad59ea093eb9d27 |
| out/ramdisk_v13_4_acm.gz | 1,269,232 B (512B MTK 头 + body 1,268,720 = 0x135bf0) | a8c05ed6d95e254555f3d6737bc17eaddcefcf4a16bb0a738dc3ce1e23a581ed |
| tools/i2cprobe | 341,188 B (ARM 静态, stripped) | 34905dbd20e75506d57a5a9c2b5be959a9b41d27c8d43fae092ecff988e97bb2 |
（四件均已登记进工作区根 `SHA256SUMS` 尾部。）
- 共享 ramdisk = v13.3b 同一 /init 基础上加：
  **v13.4r 修 1（正确性评审唯一警告）**: /init 末段单次 `exec getty` 改为
  `while :; do getty; sleep 1; done` 重生循环——取证会话 exit 后 PID1 不再
  退出（原行为 → panic=10 整机重启）；顺带修正两处陈旧注释（/dev/console
  实际落 ttyS0 而非 tty0，W1 修复后事实）。
  **v13.4r 修 2**: i2cprobe 发 SMBus quick 前先 `ioctl(I2C_SLAVE, addr)`
  （原代码 quick 永远打向初始 client 地址 0，假阳性）；I2C_SLAVE 失败且
  errno==EBUSY 时打印“地址被内核驱动占用”与真 NACK 区分；默认只读行为不变。
  另：剔除上轮误入归档的打包中间产物 body_new.gz（2,594 B 垃圾）。
- 512B 头长度字段 (偏移 0x04 LE) = f0 5b 13 00 = 1,268,720 ✓ 与 body 互镜像。
- zImage 未变: sha e099f0cd... (评审四路同 hash; payload strings 命中
  `Z1 CMD1 pre-warm` ×3, **eMMC 预热修复在位**; 且 8cec21a62 已 cherry-pick
  进 main = b1380bf32, 源码级暗雷同步解除)。

## 配对表 (AGENTS.md §3.2 同步新增行)
| ramdisk | body 字节数 | linux,initrd-end |
|---|---|---|
| ramdisk_v11_extracted.gz | 1,068,865 | 0x84204f41 |
| ramdisk_v13_3_acm.gz (v13.3) | 1,069,384 | 0x84205148 |
| ramdisk_v13_3_acm.gz (v13.3b) | 1,069,518 | 0x842051ce |
| **ramdisk_v13_4_acm.gz (v13.4/s6_v4 原版, 已被 v13.4r 覆盖)** | 1,271,370 (0x13664a) | 0x8423664a |
| **ramdisk_v13_4_acm.gz (v13.4r 现行)** | **1,268,720 (0x135bf0)** | **0x84235bf0** |

## s6_v4 附带修复
- initrd-end 0x841e2810 (陈旧截断值, 与 Z1 v13.1 修掉的是同一暗雷) → 0x84235bf0 (v13.4r)。
- model "S6 smartphone (alps I3)" → "S6 hearing aid (alps I3)" (用户 08-26 确认是听力机)。

## 全链验证 (已做)
- 内嵌 kernel 段 (跳过 mkbootimg 自动加的 512B MTK KERNEL 头) = cat(zImage+dtb) 逐字节 ✓
- 内嵌 ramdisk 段 = out/ramdisk_v13_4_acm.gz 逐字节 ✓
- 内嵌 dtb 反编译: initrd-start/end 0x84100000/0x84235bf0 ✓, bootargs ttyS0 居末 ✓
  无 ttyMT0 ✓, mmc@11120000/usb@11100000/phy@11110800 全 okay (usb 无显式 status=默认启用);
  dts 源树与镜像内嵌双确认。

## 刷写与回滚 — Z1 (等用户唤醒设备; 刷写才插下载线, 直插主板后置 USB 勿经 hub)
```
tools/flash_with_retry.sh bootimg out/boot_mainline_v13_4.img /home/<user>/claude/6572/1/boot.img 8
```
- 回滚件: ~/claude/6572/1/boot.img (Z1 原厂); 上一可用 out/boot_mainline_v13_2a1.img。
- **顺序纪律 (影响面评审指出 v13.3b 未写清, 此处固化): 先正常开机 (拔下载线,
  过 preloader), 再插 USB 线验证 ACM**; preloader 看 USB LineState, 开机前插线会进重启循环。
- **v13.4r 新增行为**: 取证会话 `exit` 后不再整机重启, PID1 重生循环 1s 后
  重新拉起 getty; 验证时可反复 exit/重连确认。

## 刷写与回滚 — S6 (s6_v4 专节)
```
tools/flash_with_retry.sh bootimg out/boot_mainline_s6_v4.img /home/<user>/claude/6572/mt6572-2/extracted/boot.img 8
```
- 回滚件已实测在位: `mt6572-2/extracted/boot.img` (4,798,464 B, 头 8B = ANDROID! ✓,
  2023-12-15 提取的 S6 原厂件)。
- **s6_v4 即 S6 首刷; 唯一回退 = 该 S6 原厂件。禁止复用 Z1 的 `1/boot.img`
  当 S6 回滚件——跨设备回滚 = 板砖路径。**
- 注意: S6 与 Z1 的 USB 下载身份相同 (0e8d:2006, serial Z1MT65720001),
  主机无法按名区分两台设备, 须按物理口/插拔时序区分; 同一时刻只接一台刷写。
- 时序纪律同 Z1: 先起 mtkclient (脚本自己轮询), 再由用户按开机键; 下载线直插勿经 hub。

## 验证点 (v13.4, Z1)
1. 串口 (C3 桥, 921600): **现在应能看到 Run /init 之后的输出** —
   `[acm] configfs mounted` → `acm function created` → `acm linked into c.1` →
   `UDC detected/bound` → `/dev/ttyGS0 ready`; 串口末尾应出 getty 提示符
   (W1 修复后 /dev/console=ttyS0)。若仍静默 → 查 dts bootargs 是否真生效
   (新证据点)。
2. 主机插 USB (开机后): lsusb 出 0e8d:2006, 新增 /dev/ttyACM*;
   **先 ls -l /dev/serial/by-id 分辨哪个是 C3 桥** (gadget 枚举名 usb-Z1_Z1_ACM_*,
   桥是 Espressif)；z1_auto_switch.sh/grab_acm.sh/tools/acm_capture.sh 硬编码
   ttyACM0 有抢口风险, 勿在 gadget 上线时盲跑。
3. 触摸取证 (串口或 ttyGS0 shell):
   `i2cprobe /dev/i2c-1 0x15` → CST836U 存在性;
   `i2cprobe /dev/i2c-1 dump 0x15 0xa6 6` → FW/模块/项目/芯片 ID
   (协议细节见 mainline_recon/TOUCH_CST836U_20260829.md)。
4. eMMC: mmc0 new MMC card + 分区枚举照 v13.2a; p6 "extends beyond EOD"
   是原厂 GPT 哨兵值, 无害 (定案 P6_EOD_VERDICT_20260815.md), 挂 p5/p6 前仍须核对边界。

## 验证点 (s6_v4, S6)
- S6 无已知 UART 测试点: 首启插 USB (开机后) 看主机是否枚举 0e8d:2006 ACM;
  设备侧 [acm] 自报走 ttyGS0。若枚举成功即拿到交互通道 (getty 在 ttyGS0)。
- 触摸取证: `i2cprobe /dev/i2c-1 0x38 0x15` (FTS 老系列默认 0x38)。
- 串口: 若日后找到 UART 测试点, console 名是 ttyS0 @ 921600。

## cmdline (boot 头, 本链不生效仅镜像自洽)
console=tty0 console=ttyS0,921600n8 root=/dev/ram panic=10 loglevel=7
clk_ignore_unused regulator_ignore_unused oops=panic sysrq_always_enabled
rdinit=/init earlycon=mtk8250,0x11005000
- regulator_ignore_unused 保留 (影响面评审裁决: 08-14 起防 39.5s regulator
  晚清理掉电重启双保险, WMT 文档矛盾系旧语境)。
