# S6 (alps I3) 触摸 FTS 与 MCU 并口屏 LCM 路线 — 2026-08-29 调研

定位: 补充 S6_STATUS_20260826.md §六路线的"中期新工作量"两项, 基于
浏览器联网调研 + 树内代码核对。S6 首启 (s6_v4) 不依赖本两项——
首启目标仍是 串口/ACM 可见性 + initrd + /init 跑通。

## 一、触摸: FocalTech FTS

现状: S6 触摸为 FocalTech FTS (S6_STATUS §三), I2C1 已 okay (400kHz),
芯片型号/地址/GPIO 全部未捕获, dts 无节点。

树内可用面 (fork_linux drivers/input/touchscreen/edt-ft5x06.c of_match,
L1521-1535): "focaltech,ft6236/ft5426/ft5452/ft3518/ft8201/ft8716/ft8719"
等 —— FocalTech **老/中代**芯片在 mainline 有现成驱动;
`CONFIG_TOUCHSCREEN_EDT_FT5X06=m` 已开。

路线分叉 (取决于 S6 实际 IC 代际):
- **老/中代 (FT5x06/FT6x36/FT5426/FT8716 类, I2C 地址通常 0x38)**:
  直接用 edt-ft5x06 加节点, 几乎零移植。
- **新 FTS 代 (FT8006/FT8736 等 vendor "focaltech_touch" 框架)**:
  mainline 无驱动, 需从 MTK vendor 内核源 (alps 系 device kernel dump,
  GitHub 上以 `focaltech_touch`/`fts.c` 大量存在) 移植, 工作量中等。

取证路径 (本轮已交付工具): v13.4 的 ramdisk 带 `sbin/i2cprobe` (窄地址 +
寄存器只读 dump)。S6 首启拿到 ACM shell 后:
```
i2cprobe /dev/i2c-1 0x38 0x15          # FTS 默认 0x38; 0x15 为 Z1 同款交叉确认
i2cprobe /dev/i2c-1 dump 0x38 0xa6 6   # FocalTech 身份寄存器 (FIRMID 等)
i2cprobe /dev/i2c-1 dump 0x38 0x00 8   # 老系列: 0x00 PD/设备码, 0x02 TD_STAT
```
读出 ID 后对表定代际 → 选 edt-ft5x06 节点或立项 vendor port。

## 二、显示: MCU 并口屏 (gc9307 / nv3029s / st7789v)

S6_STATUS 已定案 simple-framebuffer 不可行 (MCU 屏自带 GRAM, LK 一次性
burst 写完即无持续刷新面)。本轮联网+树内复核后的路线清单:

1. **mainline 无 8080 并口 DBI 子系统**: DRM 面板生态的 MCU 屏支持走
   `mipi-dbi` (panel-mipi-dbi.c + tinydrm 系), 但其传输后端只实现了 **SPI**;
   无 8080 并口后端, 也无 MTK DBI 控制器驱动 (mtk_drm 的 mt6572 支持是
   OVL/RDMA/DSI 视频流水线, 即 Z1 的 DSI 屏路线)。
2. **vendor LCM 移植 (正解, 最大单项)**: 从 S6 原厂 LK/内核的
   `gc9307_mcu_6572_s6_24_hsd` / `nv3029s_mcu_6572` / `st7789v_mcu_6572`
   源移植: 用 MT6572 DISP 的 DBI/MCU 接口寄存器做每帧 burst 写。
   **阻塞: 工作区内无这些厂商源文件** (S6_STATUS §六-5 已记), 需用户提供
   S6 原厂固件 (boot.img/recovery/lk) 后提取。
3. **GPIO 位拆 (兜底原型, 仅控制台)**: 240x320 RGB565 一帧 150KB,
   GPIO bit-bang 现实吞吐 ~百 KB/s → ~1s/帧, 动画不可用, 仅够 console
   文本验证; 除非只想"屏幕出字", 否则不值得。
4. **绕过显示的替代**: 交互通道已有 ACM/USB + 串口兜底; 若显示需求只是
   "能用", 可长期以 USB 网卡/RNDIS+ssh 或 ACM shell 为主, 屏幕后置。

结论: S6 显示 = 等用户给原厂固件源 → vendor LCM 移植立项; 在此之前
不要在 simple-framebuffer/GPIO 位拆上投入 (dts 注释已同款告诫)。

## 三、s6_v4 已修复项 (本轮)

- initrd-end 陈旧暗雷 0x841e2810 → 0x8423664a (与 Z1 v13.1 同源 bug)。
- bootargs: 删无效 `console=ttyACM0,921600n8` (主机侧名), ttyMT0 →
  ttyS0 (mainline 8250 注册名; console 选择在本链须写 dts, 见
  PACKAGE_NOTE W1 说明), model 修正为 hearing aid。
- 镜像: out/boot_mainline_s6_v4.img (5,085,184 B, sha 92ab066b...),
  zImage 复用已验证版 (e099f0cd, 含 eMMC 预热)。

相关: out/PACKAGE_NOTE_v13_4_s6_v4.md, REVIEW_*_20260829.md,
TOUCH_CST836U_20260829.md, commit 409fb7313。
