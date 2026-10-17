# Z1 mainline ADB — 内核侧规划 (2026-08-15)

> 只读分析产物。目标: Z1 mainline 7.0-rc7 内核支持 ADB。
> 结论一句话: **mainline 无 f_adb, ADB 走 functionfs(f_fs) + 用户态 adbd; 本 Z1 原厂 adbd 自带 FunctionFS 支持, 内核只需开 CONFIG_USB_CONFIGFS + CONFIG_USB_CONFIGFS_F_FS, UDC 用现成 musb-hdrc, 不新增驱动代码, vermagic 不变, zImage 增 ~100KB。**

---

## 1. 当前 fork_linux/.config 的 USB gadget 状态 (已确认)

| CONFIG | 当前值 | 说明 |
|---|---|---|
| `CONFIG_USB_GADGET` | `=y` | gadget 框架在 |
| `CONFIG_USB_MUSB_HDRC` | `=y` | MUSB 控制器核心在 |
| `CONFIG_USB_MUSB_GADGET` | `=y` | **gadget-only 模式**(无 host/dual-role) |
| `CONFIG_USB_MUSB_MEDIATEK` | `=y` | MTK MUSB 胶水在 (`drivers/usb/musb/mediatek.c`) |
| `CONFIG_USB_MUSB_TUSB6010/OMAP2PLUS/DSPS` | `=m` | 其它 SoC 的 MUSB 胶水, 不 probe, 无碍(建议顺手关掉省体积, 非必须) |
| `CONFIG_USB_LIBCOMPOSITE` | `=y` | composite 库在(configfs 的前置) |
| `CONFIG_USB_F_ACM` / `CONFIG_USB_F_SERIAL` | `=y` | function 库在(被 configfs 或 g_serial 用) |
| `CONFIG_USB_G_SERIAL` | `=y` | **legacy 预组 g_serial gadget**(当前 ttyGS0 来源) |
| `CONFIG_USB_CONFIGFS` | **未开** | ← 本次要开 |
| `CONFIG_USB_F_FS` | **未开** | functionfs 库(f_ffs.c 源码在树里, 未编) |
| `CONFIG_USB_FUNCTIONFS` | **未开** | legacy 预组 g_ffs gadget(不推荐) |
| `CONFIG_USB_GADGETFS` | **未开** | 旧 gadgetfs(不需要) |
| `CONFIG_USB_U_SERIAL` | `=y` | 串口 function 工具库 |
| `CONFIG_USB_U_ETHER` / `F_ECM/F_RNDIS/F_SUBSET` | `=m` | 网卡 function(不需要 ADB, 可留可关) |
| `CONFIG_CONFIGFS_FS` | `=y` | **configfs 文件系统已在**(运行时挂载用) |
| `CONFIG_DMA_SHARED_BUFFER` | `=y` | `USB_F_FS` select 的依赖, 已满足 |
| `CONFIG_GENERIC_PHY` | `=y` | 在 |
| `CONFIG_PHY_MTK_MT6572_USB2_HANDOFF` | `=y` | MT6572 USB2 PHY 驱动在(`phy-mtk-mt6572-usb2-handoff.c`) |
| `CONFIG_USB_DWC2/DWC3` | 未开 | 无 dts 节点不 probe, 无冲突 |

**DTS 侧已就绪(无需改动)**: `mt6572.dtsi` `usb@11100000` compatible = `"mediatek,mt2701-musb","mediatek,mtk-musb"`(musb/mediatek.c 匹配第二个), `z1.dts` 覆写 `dr_mode="peripheral"` + `status="okay"`, PHY 节点 `u2phy0` compatible `mediatek,mt6572-usb2-phy` 有驱动。

---

## 2. mainline ADB 方案分析

### 铁证: 本 Z1 原厂 adbd 自带 FunctionFS 支持, 直接复用

`out/boot_extracted/rd/sbin/adbd` 与 `out/forensic/live/rd/sbin/adbd` 是同一份 **static ARM ELF (EABI5, 202968B)**, strings 命中:

```
/dev/usb-ffs/adb/ep0      ← functionfs 挂载点 ep0
/dev/usb-ffs/adb/ep1      ← IN 端点
/dev/usb-ffs/adb/ep2      ← OUT 端点
[ usb_init - using FunctionFS ]
init_functionfs
/dev/android_adb_enable   ← 旧 android gadget 接口(仅兜底)
```

即原厂 adbd **先走 functionfs, 失败才退 `/dev/android_adb_enable`**。这意味着内核只要提供 functionfs(`/dev/usb-ffs/adb` 挂载 + 端点), **用户态不需要重编/交叉编译 adbd**, 直接用原厂二进制。

### mainline 无 f_adb — 已确认

`find drivers -name '*adb*'` 全树仅 `drivers/macintosh/adb*.c`(Mac 老 ADB 总线, 与 Android ADB 无关)。mainline 7.0-rc7 没有 `drivers/usb/gadget/function/f_adb.c`。内核侧**不需要也不能**新增 f_adb(避免维护驱动), 正确姿势是 **f_fs(functionfs) + 用户态 adbd 写描述符**。

### 两种内核配置路线

**方案1(推荐): CONFIG_USB_CONFIGFS + CONFIG_USB_CONFIGFS_F_FS**
标准 Android 5+ 的做法, configfs 层建 composite gadget, 可同时挂多个 function:
- `functions/ffs.adb` → ADB
- `functions/acm.usb0` → 保留原 USB-CDC 串口能力(ttyGS0)
- 运行时: 挂 configfs → 建 gadget → 挂 functionfs 到 `/dev/usb-ffs/adb` → 起 adbd → `echo musb-hdrc > UDC` 绑定。
- 优点: 灵活、与串口共存、是上游维护的正道; 缺点: 需要 initramfs 里跑一段 setup 脚本(用户态)。

**方案2(备选/最小): CONFIG_USB_FUNCTIONFS=y(legacy g_ffs 预组 gadget)**
只开一个 CONFIG 就能用, 预组 gadget 启动即占住整个 UDC, **不能再同时挂 ACM 串口**, 且与 configfs 互斥。仅当完全放弃串口时考虑。不推荐。

**结论: 采用方案1。** 内核改动 = 只加 CONFIG, 零驱动代码。原厂 adbd 的 `usb_init - using FunctionFS` 路径直接可用。

---

## 3. musb (MUSB_MEDIATEK) 是否支持 configfs/functionfs — 支持, 无需驱动改动

- `drivers/usb/musb/musb_gadget.c:1795` 调用 `usb_add_gadget_udc(musb->controller, &musb->g)` → musb 作为一个标准 UDC 注册进 `usb_gadget` 框架。
- configfs/functionfs 工作在 composite 层(**UDC 之上**), 只依赖 UDC 注册名, 与 UDC 驱动实现无关 → musb 天然支持。
- UDC 名 = `"musb-hdrc"`(`musb_core.c:95` `#define MUSB_DRIVER_NAME "musb-hdrc"`, `musb_gadget.c:1786` `musb->g.name = musb_driver_name`)。运行时 configfs 绑定写 `echo musb-hdrc > /sys/kernel/config/usb_gadget/z1/UDC`。
- `dr_mode="peripheral"` 时 musb 纯 gadget 模式, 不碰 OTG/HNP/SRP, configfs 绑定无 extcon/OTG 前置。
- PHY 链路完整: `mediatek.c` 用 `devm_of_phy_get_by_index` 取 `&u2phy0`, 对应 `PHY_MTK_MT6572_USB2_HANDOFF` 已在 .config。→ **UDC 探测链路(控制器+PHY+clk)当前已全部就绪, 与 g_serial 时代完全相同的通路。**

---

## 4. 需开的 CONFIG + 体积影响 + vermagic 影响

### 4.1 CONFIG 变更清单

**打开 (3 项):**
```
CONFIG_USB_CONFIGFS=y                 # configfs 复合 gadget 框架
CONFIG_USB_CONFIGFS_F_FS=y            # configfs 下提供 functionfs function (自动 select CONFIG_USB_F_FS=y)
CONFIG_USB_CONFIGFS_ACM=y             # 顺带保留 USB-CDC 串口能力(同一 composite 里跟 adb 共存)
```
(`CONFIG_USB_F_FS` 被 `USB_CONFIGFS_F_FS` select, 不必手写; 它 select 的 `DMA_SHARED_BUFFER` 已在。)

**关闭 (1 项, 关键):**
```
# CONFIG_USB_G_SERIAL is not set      # legacy 预组 g_serial 会在启动时自动抢占唯一 UDC "musb-hdrc",
                                      # 导致 configfs 后面绑不上 ("can't bind UDC")。必须关。
```
(串口能力由上面的 `USB_CONFIGFS_ACM` 以 configfs function 形式接回, 不丢失。`CONFIG_USB_F_SERIAL`/`USB_F_ACM` 库选项本身保留 `=y` 无害。)

**保留 (不动):** `USB_GADGET`、`USB_LIBCOMPOSITE`、`USB_MUSB_HDRC`、`USB_MUSB_GADGET`、`USB_MUSB_MEDIATEK`、`GENERIC_PHY`、`PHY_MTK_MT6572_USB2_HANDOFF`、`CONFIGFS_FS`、`DMA_SHARED_BUFFER`。

**顺手(可选):** 关 `USB_MUSB_TUSB6010/OMAP2PLUS/DSPS`(=m 不 probe, 可留)。

### 4.2 zImage 体积影响 — 微小, 不进超分区风险区

- `drivers/usb/gadget/configfs.o` text 13957B(~14KB); `function/f_ffs.o` 预计 text 40-60KB。合共 **~60-80KB 原始码**, 进 gzip/xz 压缩流后 **~30-40KB**。
- 现状: zImage 3.76MB, 最新 debugll boot.img 5.17MB ≤ 6MB 分区预算。加 ~40KB 后 ~5.21MB, **安全**。
- 不需要像历史 slim 那样再砍 config。

### 4.3 vermagic — 不变, 现有 .ko 全部继续可加载

- vermagic 由 `UTS_RELEASE` 决定 = `"7.0.0-rc7-z1+"`(`CONFIG_LOCALVERSION="-z1"` + `LOCALVERSION_AUTO=n`, 且按纪律 make 命令行**绝不**设 `LOCALVERSION=`)。
- 本次只**加/减 CONFIG_USB_* 符号**, 不碰任何 LOCALVERSION/setlocalversion 输入 → **UTS_RELEASE 字符串不变**。
- 已编好的 5 个 CONNSYS .ko(vermagic `7.0.0-rc7-z1+`)**照常 insmod, 无需重编**。
- 唯一需遵守: 全程默认 make, 不传 LOCALVERSION 环境变量。

---

## 5. 完整配置清单 + 编译命令

### 5.1 .config 修改命令

```bash
cd /home/<user>/claude/6572/fork_linux
./scripts/config --enable USB_CONFIGFS \
                 --enable USB_CONFIGFS_F_FS \
                 --enable USB_CONFIGFS_ACM \
                 --disable USB_G_SERIAL
# 校验:
grep -E 'CONFIG_USB_(CONFIGFS|CONFIGFS_F_FS|CONFIG_CONFIGFS_ACM|G_SERIAL|F_FS)' .config
# 期望:
# CONFIG_USB_CONFIGFS=y
# CONFIG_USB_CONFIGFS_F_FS=y
# CONFIG_USB_CONFIGFS_ACM=y
# CONFIG_USB_F_FS=y            (被 select)
# # CONFIG_USB_G_SERIAL is not set
```

### 5.2 重编 (按项目纪律: 改 config 后 make clean)

```bash
cd /home/<user>/claude/6572/fork_linux
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- olddefconfig
# 确认 auto.conf 仍含 DEBUG_LL(若沿用 debugll 配置) + USB_CONFIGFS
grep -E 'CONFIG_USB_CONFIGFS|CONFIG_DEBUG_LL' include/config/auto.conf
make clean && make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- mt6572-z1.dtb zImage
```

### 5.3 打包 boot.img (appdtbdual 风格, 沿用 debugll 基线)

```bash
cd /home/<user>/claude/6572
cat fork_linux/arch/arm/boot/zImage fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb > /tmp/zImage_appdtb_adb
./build/mtkbootimg/mkbootimg --kernel /tmp/zImage_appdtb_adb \
  --ramdisk out/forensic/linux/ramdisk.gz \
  --dt fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb \
  --base 0x10000000 --kernel_offset 0x8000 --pagesize 2048 --mtk 1 \
  --cmdline "console=tty0 console=ttyMT0,921600n8 earlycon=mtk8250,0x11005000 root=/dev/ram panic=0" \
  -o out/boot_mainline_adb.img
# 产物 ≤ 6MB 校验 + 归档 bridge_backups/products/
```

### 5.4 上板验收点 (内核侧)

1. dmesg: `musb-hdrc` 注册 UDC + `musb-hdrc.1.auto: USB MODE` 正常(与 g_serial 时代一致)。
2. `ls /sys/class/udc/` → 出现 `musb-hdrc`。
3. 内核带 `CONFIG_USB_CONFIGFS=y` + `CONFIG_CONFIGFS_FS=y` 后, 用户态可 `mount -t configfs none /sys/kernel/config`。
4. 内核带 `CONFIG_USB_F_FS=y` 后, `mount -t functionfs adb /dev/usb-ffs/adb` 成功, 出现 `ep0/ep1/ep2`。
5. `echo musb-hdrc > .../UDC` 绑定后主机 `lsusb` 出现 VID:PID, `adb devices` 识别(VID 建议 0x18d1, PID 建议 0x4ee2 经典 ADB)。

### 5.5 用户态前置(内核外, 供主会话衔接, 本轮只规划不实施)

- initramfs 需含: 原厂 `adbd`(static, 直接拷) + 一段 setup(挂 configfs/functionfs、建 gadget、绑 `musb-hdrc`、起 adbd)。
- configfs 里建 `ffs.adb` 挂到 `/dev/usb-ffs/adb`(adbd 硬编码路径, 必须此名), 可与 `acm.usb0` 同 gadget 共存。
- 现有 `out/boot_extracted/rd/sbin/adbd`(202968B static ARM)可直接用。

---

## 附: 关键文件/路径索引

- 内核配置: `/home/<user>/claude/6572/fork_linux/.config`
- musb UDC 注册: `drivers/usb/musb/musb_gadget.c:1795`; UDC 名: `musb_core.c:95`
- musb MTK 胶水: `drivers/usb/musb/mediatek.c` (compatible `mediatek,mtk-musb`)
- PHY: `drivers/phy/mediatek/phy-mtk-mt6572-usb2-handoff.c`
- functionfs 文件系统: `drivers/usb/gadget/function/f_fs.c:2100` (fs type `functionfs`)
- configfs gadget Kconfig: `drivers/usb/gadget/Kconfig` (`USB_CONFIGFS` @228, `USB_CONFIGFS_F_FS` @369)
- DTS: `arch/arm/boot/dts/mediatek/mt6572.dtsi:484` (usb 节点), `mt6572-z1.dts:287` (dr_mode=peripheral)
- 原厂 adbd: `out/boot_extracted/rd/sbin/adbd`, `out/forensic/live/rd/sbin/adbd`
- 本报告: `/home/<user>/claude/6572/mainline_recon/ADB_KERNEL_20260815.md`
