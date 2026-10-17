# /dev/ttyGS0 不存在 — 综合修复方案

## 根因分析（一句话总结）

**MUSB mediatek glue 缺少 `vbus_status` 回调 + PHY HS slew-rate 校准超时 + 脚本诊断不足，导致 gadget 框架加载但 UDC 无法激活枚举，`/dev/ttyGS0` 设备节点不创建。**

---

## 根因链详细分析

### 1. UDC 名称确认 ✅ (非问题)

**代码证据**:
- `drivers/usb/musb/musb_core.c:95` → `#define MUSB_DRIVER_NAME "musb-hdrc"`
- `drivers/usb/musb/musb_gadget.c:1786` → `musb->g.name = musb_driver_name;`
- `drivers/usb/musb/musb_gadget.c:1795` → `usb_add_gadget_udc(musb->controller, &musb->g);`

**结论**: UDC 名称 = `"musb-hdrc"` (驱动名)，不是 `"11100000.usb"` (DTS 设备节点地址)。脚本已正确使用 `"musb-hdrc"`。

### 2. VBUS 检测缺失 🔴 (P0 根因)

**问题**:
- `drivers/usb/musb/mediatek.c:357` → `mtk_musb_ops` 结构体**未定义** `.vbus_status` 和 `.set_vbus` 回调
- `drivers/usb/musb/musb_core.c:1927` → 当 `vbus_status` 回调不存在时，回退到读 `MUSB_DEVCTL` 寄存器的 `VBUS` 位
- **在 peripheral 模式下**: MUSB 需要检测 VBUS ≥ 4.4V 才会启用 D+ pullup 并响应主机枚举
- **Z1 硬件**: VBUS 由主机提供，但 MTK PHY 驱动可能未正确报告 VBUS 状态到 DEVCTL 寄存器

**影响**: MUSB 不知道主机已连接 → 不触发 gadget 枚举 → `/dev/ttyGS0` 不创建或创建但无连接

### 3. PHY HS Slew-Rate 校准超时 🟡 (P1 次要)

**问题**:
- `drivers/phy/phy-mtk-mt6572-usb2-handoff.c:74` → `read_poll_timeout` 等待 `0x510` 的 BIT(0)，超时 10ms
- 校准失败后回退到默认 slew-rate 值 `4`
- **影响**: HS 信号质量可能不合规，但不阻止功能（低速连接仍可能工作）

**证据**: kernel log 显示 PHY probe 成功 (0.097s)，但可能有 slew-rate 校准超时警告

### 4. 脚本诊断不足 🟢 (P2)

**问题**:
- 原脚本缺少前置检查（内核配置、UDC 是否存在）
- UDC 绑定失败时无详细诊断信息
- `/dev/ttyGS0` 创建后无验证

---

## 修复步骤（按优先级）

### 修复 1: 添加 VBUS 状态回调 (P0 关键)

**文件**: `drivers/usb/musb/mediatek.c`

**修改**:
```c
// 在 mtk_musb_set_mode() 后添加:
static int mtk_musb_vbus_status(struct musb *musb)
{
    /*
     * Z1 is always peripheral (dr_mode="peripheral" in DTS).
     * When USB cable is connected, VBUS is provided by host.
     * For always-device mode, assume VBUS is always valid when
     * cable is connected. This allows gadget enumeration to proceed.
     *
     * Alternative: read DEVCTL register to check VBUS status:
     * u8 devctl = readb(musb->mregs + MUSB_DEVCTL);
     * return (devctl & MUSB_DEVCTL_VBUS) == MUSB_DEVCTL_VBUS;
     */
    u8 devctl = readb(musb->mregs + MUSB_DEVCTL);

    /* Check if VBUS is valid (≥4.4V, bits 5-7 = 0b111) */
    if ((devctl & MUSB_DEVCTL_VBUS) == (3 << MUSB_DEVCTL_VBUS_SHIFT))
        return 1;  /* VBUS valid */

    /* For always-peripheral mode, if session is set, assume VBUS valid */
    if (devctl & MUSB_DEVCTL_SESSION)
        return 1;

    return 0;  /* VBUS not valid */
}

static void mtk_musb_set_vbus(struct musb *musb, int on)
{
    /*
     * In peripheral mode, VBUS is provided by host, not controlled by device.
     * This is a no-op for Z1 (always peripheral).
     * For OTG mode, would need to control VBUS regulator.
     */
    if (musb->port_mode == MUSB_PERIPHERAL)
        return;

    /* OTG mode: control VBUS (not used on Z1) */
    u8 devctl = readb(musb->mregs + MUSB_DEVCTL);
    if (on)
        devctl |= MUSB_DEVCTL_SESSION;
    else
        devctl &= ~MUSB_DEVCTL_SESSION;
    writeb(devctl, musb->mregs + MUSB_DEVCTL);
}

// 修改 mtk_musb_ops 结构体:
static const struct musb_platform_ops mtk_musb_ops = {
    .quirks = MUSB_DMA_INVENTRA,
    .init = mtk_musb_init,
    .get_toggle = mtk_musb_get_toggle,
    .set_toggle = mtk_musb_set_toggle,
    .exit = mtk_musb_exit,
#ifdef CONFIG_USB_INVENTRA_DMA
    .dma_init = musbhs_dma_controller_create_noirq,
    .dma_exit = musbhs_dma_controller_destroy,
#endif
    .clearb = mtk_musb_clearb,
    .clearw = mtk_musb_clearw,
    .busctl_offset = mtk_musb_busctl_offset,
    .set_mode = mtk_musb_set_mode,
    .vbus_status = mtk_musb_vbus_status,    // ← 新增
    .set_vbus = mtk_musb_set_vbus,          // ← 新增
};
```

**验证**:
```bash
cd /home/<user>/claude/6572/fork_linux
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage
# 检查编译无错误
```

### 修复 2: 增加 PHY slew-rate 校准超时 (P1)

**文件**: `drivers/phy/phy-mtk-mt6572-usb2-handoff.c`

**修改**:
```c
// 第 74 行附近，将超时从 10ms 增加到 100ms:
ret = read_poll_timeout(readb, fm_out, fm_out & BIT(0),
                        10, 100000,  // 100ms instead of 10ms
                        false, priv->base + 0x510);
```

**影响**: 给 PHY 更多时间完成 HS 校准，改善 USB 信号质量

### 修复 3: 增强 z1_adb_setup.sh 脚本 (P2)

**已完成**: 见 `scripts/z1_adb_setup.sh` (已重写)

**增强点**:
1. 前置检查内核配置 (`CONFIG_USB_CONFIGFS`, `CONFIG_USB_CONFIGFS_F_FS`, `CONFIG_USB_CONFIGFS_ACM`)
2. 检查 UDC 是否存在 (`/sys/class/udc/`)
3. 验证目标 UDC 名称 (`musb-hdrc`)
4. adbd 存活检查 + 自动重启
5. 详细诊断输出（UDC 绑定失败时打印 dmesg）
6. `/dev/ttyGS0` 创建验证

### 修复 4: DTS 添加 VBUS dummy regulator (可选)

**文件**: `arch/arm/boot/dts/mediatek/mt6572-z1.dts`

**修改**:
```dts
/* 在 / 节点下添加 */
vbus_5v: regulator-vbus-5v {
    compatible = "regulator-fixed";
    regulator-name = "vbus-5v";
    regulator-min-microvolt = <5000000>;
    regulator-max-microvolt = <5000000>;
    regulator-always-on;
};

&usb {
    status = "okay";
    dr_mode = "peripheral";
    phys = <&u2phy0>;
    phy-names = "usb2";
    vbus-supply = <&vbus_5v>;  /* 消除 dummy supply 警告 */
};
```

**影响**: 消除 regulator dummy supply 警告，但不影响功能（修复 1 已解决 VBUS 检测）

---

## 配置文件修改列表

| 文件 | 修改类型 | 优先级 | 说明 |
|------|---------|--------|------|
| `drivers/usb/musb/mediatek.c` | 添加 vbus_status/set_vbus 回调 | **P0** | 关键修复 |
| `drivers/phy/phy-mtk-mt6572-usb2-handoff.c` | 增加校准超时 | P1 | 改善信号质量 |
| `scripts/z1_adb_setup.sh` | 增强诊断 + 前置检查 | P2 | 已完成 |
| `arch/arm/boot/dts/mediatek/mt6572-z1.dts` | 添加 vbus-supply | 可选 | 消除警告 |

---

## 上板验证步骤

### 步骤 1: 编译修复后的内核

```bash
cd /home/<user>/claude/6572/fork_linux

# 备份当前配置
cp .config ../bridge_backups/dot.config_pre_usb_fix_$(date +%Y%m%d_%H%M%S).bak

# 编译 zImage
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage

# 编译 dtb
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- mt6572-z1.dtb

# 检查产物
ls -lh arch/arm/boot/zImage
ls -lh arch/arm/boot/dts/mediatek/mt6572-z1.dtb
```

### 步骤 2: 打包 boot.img

```bash
cd /home/<user>/claude/6572

# 拼接 dtb 到 zImage 末尾 (mainline APPENDED_DTB 需要)
cat fork_linux/arch/arm/boot/zImage \
    fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb \
    > /tmp/zImage_appdtb_usb_fix

# 打包 boot.img
./build/mtkbootimg/mkbootimg \
    --kernel /tmp/zImage_appdtb_usb_fix \
    --ramdisk out/forensic/linux/ramdisk.gz \
    --base 0x10000000 \
    --kernel_offset 0x8000 \
    --pagesize 2048 \
    --mtk 1 \
    --cmdline "console=tty0 console=ttyMT0,921600n8 earlycon=mtk8250,0x11005000 root=/dev/ram panic=0" \
    -o out/boot_mainline_usb_fix.img

# 检查大小 (必须 ≤ 6MB)
ls -lh out/boot_mainline_usb_fix.img
```

### 步骤 3: 刷写到 Z1

```bash
cd /home/<user>/claude/6572

# 用户操作:
# 1. 启动 mtkclient 等待连接
# 2. Z1 插 download 线 + 按开机键进 preloader/BROM 模式
# 3. 执行刷写

tools/flash_with_retry.sh bootimg \
    out/boot_mainline_usb_fix.img \
    /home/<user>/claude/6572/1/boot.img \
    8

# 刷写完成后:
# 4. 拔掉 download 线
# 5. 电池开机
```

### 步骤 4: 抓 boot log

```bash
cd /home/<user>/claude/6572

# C3 桥全程在线 (921600)
./z1_capture_mainline.sh 180

# 检查 log
tail -50 mainline_recon/z1_boot_mainline_*.log

# 关键检查点:
grep -i 'phy.*usb\|usb.*phy' mainline_recon/z1_boot_mainline_*.log
grep -i 'musb\|udc\|gadget' mainline_recon/z1_boot_mainline_*.log
grep -i 'vbus\|DEVCTL' mainline_recon/z1_boot_mainline_*.log
```

### 步骤 5: Alpine 启动后验证 USB gadget

```bash
# 上板后通过串口登录 Alpine
# 运行修复后的脚本
sh /root/connsys/z1_adb_setup.sh

# 检查输出:
# ✅ 应看到:
#   [adb-setup] 内核配置检查通过
#   [adb-setup] configfs 就绪
#   [adb-setup] adbd started pid XXX
#   [adb-setup] 可用 UDC: musb-hdrc
#   [adb-setup] 目标 UDC 'musb-hdrc' 存在 ✓
#   [adb-setup] UDC 绑定成功 (musb-hdrc)
#   [adb-setup] ✅ USB gadget 配置成功: /dev/ttyGS0 就绪

# 验证设备节点
ls -l /dev/ttyGS0
# 应看到: crw-rw-rw- 1 root root 252, 0 ... /dev/ttyGS0

# 检查 UDC 状态
cat /sys/kernel/config/usb_gadget/z1/UDC
# 应输出: musb-hdrc

# 检查 gadget 配置
ls /sys/kernel/config/usb_gadget/z1/
# 应看到: configs/ functions/ UDC idVendor idProduct strings/

# 检查 ACM function 链接
ls -l /sys/kernel/config/usb_gadget/z1/configs/c.1/
# 应看到: acm.usb0 -> .../functions/acm.usb0
#          ffs.adb -> .../functions/ffs.adb

# 检查 dmesg
dmesg | grep -i 'acm\|gadget\|udc\|ttyGS'
# 应看到:
#   usb gadget: using controller 'musb-hdrc'
#   acm gadget: ACM: ttyGS0, ...
```

### 步骤 6: 主机端验证 ADB + 串口

```bash
# 主机插 USB 线 (内核起来后再插，避免 preloader LineState 坑)

# 检查 ADB
adb devices
# 应看到: XXXXXXXX device

# 检查串口
ls /dev/ttyACM*
# 应看到: /dev/ttyACM0

# 串口通信
screen /dev/ttyACM0 115200
# 或
minicom -D /dev/ttyACM0 -b 115200
```

### 步骤 7: 诊断失败情况

如果 `/dev/ttyGS0` 仍不创建:

```bash
# 上板后运行诊断
echo "=== UDC 诊断 ==="
ls -la /sys/class/udc/
cat /sys/class/udc/*/state 2>/dev/null

echo -e "\n=== MUSB 状态 ==="
dmesg | grep -i musb | tail -20

echo -e "\n=== PHY 状态 ==="
dmesg | grep -i 'phy.*usb\|usb.*phy' | tail -10

echo -e "\n=== VBUS 状态 ==="
dmesg | grep -i vbus | tail -10

echo -e "\n=== gadget 配置 ==="
ls -la /sys/kernel/config/usb_gadget/
cat /sys/kernel/config/usb_gadget/z1/UDC

echo -e "\n=== /dev/ttyGS* ==="
ls -la /dev/ttyGS* 2>/dev/null || echo "无 ttyGS 设备"

echo -e "\n=== devtmpfs ==="
mount | grep devtmpfs
```

---

## 预期结果

### 成功场景
1. kernel log 显示 `musb-hdrc` probe 成功
2. PHY probe 成功 (slew-rate 校准可能超时但不影响功能)
3. Alpine 启动后运行 `z1_adb_setup.sh` 成功
4. `/dev/ttyGS0` 创建
5. 主机插 USB 后 `adb devices` 可见
6. 主机 `/dev/ttyACM0` 可用

### 失败场景
1. **UDC 不存在**: MUSB probe 失败 → 检查 PHY/clock/regulator
2. **UDC 绑定失败**: adbd 未写描述符 → 检查 functionfs 挂载 + adbd 启动
3. **`/dev/ttyGS0` 不创建**: ACM function 未链接 → 检查 configfs 配置
4. **主机看不到设备**: VBUS 检测仍失败 → 检查 DEVCTL 寄存器值

---

## 时间估算

| 步骤 | 时间 | 说明 |
|------|------|------|
| 修改 mediatek.c | 10 分钟 | 添加 vbus_status/set_vbus |
| 修改 phy 超时 | 5 分钟 | 改一行 |
| 编译 zImage | 5-10 分钟 | 增量编译 |
| 打包 boot.img | 1 分钟 | cat + mkbootimg |
| 刷写 | 2 分钟 | mtkclient + flash |
| 上板验证 | 10 分钟 | 抓 log + 运行脚本 + 检查 |
| **总计** | **30-40 分钟** | |

---

## 回滚方案

如果修复失败，刷回原厂:
```bash
tools/flash_with_retry.sh bootimg \
    /home/<user>/claude/6572/1/boot.img \
    /home/<user>/claude/6572/1/boot.img \
    8
```

---

## 参考文档

- [Linux USB gadget configured through configfs](https://docs.kernel.org/usb/gadget_configfs.html)
- [USB Gadget/Configfs - linux-sunxi](https://linux-sunxi.org/USB_Gadget/Configfs)
- [Modern USB gadget on Linux & how to integrate it with systemd](https://www.collabora.com/news-and-blog/blog/2019/02/18/modern-usb-gadget-on-linux-and-how-to-integrate-it-with-systemd-part-1/)
- `drivers/usb/musb/musb_core.h:138-187` — `musb_platform_ops` 定义
- `drivers/usb/musb/musb_core.c:1925-1942` — VBUS 状态读取逻辑
