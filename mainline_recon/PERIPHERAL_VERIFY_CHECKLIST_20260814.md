# Z1 外围设备上板验证清单 — 只读审计报告 (2026-08-14)

> 生成方式: 只读分析 `fork_linux/.config` + `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts` + 上板 log（主: `mainline_recon/z1_boot_mainline_20260814_002746.log`；交叉验证: `mainline_recon/z1_boot_mainline_20260813_201957.log`）。
> 注意: 上板 log 是二进制混合文件，grep 必须加 `-a`（text mode）才能命中；直接用 `grep 'xxx'` 会 0 命中误导。

## 0. 一句话结论

| 设备 | 上板当前状态 | 结论 |
|---|---|---|
| USB CDC-ACM (g_serial) | **可用** ✅ | musb + g_serial 已 up，`g_serial ready`；ttyGS0 待插线确认 |
| 矩阵按键 (mt6779-keypad) | **可用** ✅ | input0 已注册，当前 dts 只映射 2 键 (VolUp/VolDown) |
| PMIC 电源键 (mtk-pmic-keys) | **不可用** ❌ | `CONFIG_MFD_MT6323_NO_IRQ_DIAGNOSTIC=y` → MFD 只建 regulator cell，keys 子设备从未创建，驱动注册但不 probe |
| Goodix 触摸 | **不可用** ❌ | dts `touchscreen@5d status="disabled"`（节点压根没启用） |
| 显示 (simplefb) | **可用** ✅ | fb0 = simplefb 已注册，fbcon 已切换，240x320x16 |
| ST7785M panel (DRM) | **不可用** ❌ | `CONFIG_DRM` 未开 + dts `panel@0 status="disabled"`，本轮本就靠 simplefb 而非 DRM |
| 电池 (adc-battery) | **不可用（defer 死循环）** ❌ | `adc-battery returned 517`；根因 = MFD diag 模式不建 `mt6323-auxadc` cell → 无 IIO channel |

**总根因（电池 + PMIC 键两个坑是同一个）**: `CONFIG_MFD_MT6323_NO_IRQ_DIAGNOSTIC=y` 让 mt6323 MFD 只注册 `mt6323-regulator` 一个子设备（见 `drivers/mfd/mt6397-core.c:403-421`），`mt6323-auxadc` / `mt6323-keys` / `mt6323-rtc` 等 cell 全部不创建。auxadc 驱动和 keys 驱动的 initcall 都执行了（注册了 driver），但**永远没有对应 platform device 可绑定**。

---

## 1. CONFIG 核对表 (fork_linux/.config)

全部按任务清单核对，另补几个关键项。

| 配置名 | 状态 | 影响设备 |
|---|---|---|
| CONFIG_USB_MUSB_MEDIATEK | =y | USB musb 平台驱动 ✅（log 已 probe） |
| CONFIG_USB_INVENTRA_DMA | =y | USB DMA ✅ |
| CONFIG_USB_MUSB_HDRC | =y | musb 核心 ✅ |
| CONFIG_USB_MUSB_GADGET | =y | musb 只做 device（配 dr_mode=peripheral）✅ |
| CONFIG_USB_GADGET | =y | gadget 框架 ✅ |
| CONFIG_USB_G_SERIAL | =y | **实际生效的 gadget = g_serial（legacy）**，已 `g_serial ready` ✅ |
| CONFIG_USB_F_ACM / CONFIG_USB_F_SERIAL | =y / =y | f_acm 可用但 **CONFIG_USB_CONFIGFS 未开**，当前不走 configfs 路径 |
| CONFIG_USB_CONFIGFS | 未开 | 若想用 f_acm/configfs 需打开（当前 g_serial 已够用，非缺口） |
| CONFIG_INPUT | =y | 输入子系统 ✅ |
| CONFIG_INPUT_KEYBOARD | =y | 键盘框架 ✅ |
| CONFIG_KEYBOARD_MT6779 | =y | 矩阵键驱 (mt6779-keypad) ✅（注意不是 KEYBOARD_MTK） |
| CONFIG_KEYBOARD_MTK_PMIC | =y | PMIC 电源键驱动已编 ✅ 但**无设备可绑**（见 §3） |
| CONFIG_TOUCHSCREEN_GOODIX | =y | goodix 驱动已编 ✅ 但**dts 节点 disabled** |
| CONFIG_POWER_SUPPLY | =y | power_supply 核心 ✅ |
| CONFIG_GENERIC_ADC_BATTERY | =y | adc-battery 驱动已编 ✅ 但**defer 死循环** |
| CONFIG_MEDIATEK_MT6323_AUXADC | =y | auxadc IIO 驱动已编 ✅ 但**无 MFD cell 可绑** |
| CONFIG_MFD_MT6397 | =y | mt6323 MFD ✅ |
| **CONFIG_MFD_MT6323_NO_IRQ_DIAGNOSTIC** | **=y ⚠️ 罪魁** | 只注册 regulator cell → 电池 + PMIC 键全废 |
| CONFIG_DRM | **未开** | 无 DRM 路径（本轮显示靠 FB/simplefb，预期内） |
| CONFIG_DRM_MEDIATEK | **未开** | 同上 |
| CONFIG_FB / CONFIG_FB_SIMPLE / CONFIG_FRAMEBUFFER_CONSOLE | =y / =y / =y | simplefb + fbcon ✅（已实测注册） |
| CONFIG_IIO / CONFIG_IIO_TRIGGERED_BUFFER | =y / =y | IIO 框架 ✅ |
| CONFIG_BATTERY_BQ27XXX | 未开 | 无独立 fuel gauge，本轮电池靠 adc-battery，无影响 |

**配置层面结论**: USB / 键盘 / 触摸驱动 / 显示 / adc-battery 的**驱动代码全部已编进内核**。设备上不工作的 3 项 (PMIC 键 / goodix / 电池) **不是缺 CONFIG 编不出来，而是 DT 节点 disabled 或 MFD cell 不创建**。

---

## 2. DTS 节点核对表 (mt6572-z1.dts)

| 设备 | dts 节点 / 状态 | .config 是否配套 | 上板预期行为 |
|---|---|---|---|
| USB | `&usb` L279-282 `status="okay"`, `dr_mode="peripheral"`; `&u2phy0` L284-286 `okay` | ✅ 全配套 | **预期可用**；musb 只做 device，g_serial 绑定，插线后 /dev/ttyGS0 |
| uart0 | `&uart0` L275-277 `okay` | ✅ SERIAL_8250_MT6577=y | console=ttyS0@115200（本 boot cmdline），log L950 probe OK |
| 矩阵键 | `&keyboard` L404-414 `okay`; 8x9, 2 键: MATRIX_KEY(0,0,VOLUMEUP) / (1,0,VOLUMEDOWN) | ✅ KEYBOARD_MT6779=y | **预期可用**；input0 = mt6779-keypad。**注意 dts 只映射 2 键**，非 CLAUDE.md 旧文的 8 键 |
| PMIC 键 | `&mt6323keys` L322-327 `power`(默认启用) + `home` disabled | ✅ 驱动已编 | **预期不可用** — MFD diag 模式不创建 mt6323-keys cell（根因见 §0） |
| i2c1 | `&i2c1` L416-421 `okay`, 400kHz, pinctrl 有 | ✅ | i2c-1 bus 已 probe (log L1115)；bus 本身可用 |
| Goodix 触摸 | `touchscreen@5d` L429-449 **`status="disabled"`**；compatible=goodix,gt911; IRQ GPIO129/RST GPIO128; keymap 已写 (Back/Home/APPSELECT) | ✅ 驱动已编 | **预期不可用** — 节点 disabled，驱动不会 probe。**需先 dts 改 enabled + 重编才能测** |
| DSI/ST7785M | `&dsi` L467-476 `panel@0 status="disabled"`；`&dsi` 在 mt6572.dtsi 也是 disabled | ❌ CONFIG_DRM 未开 | **预期不可用** — 本轮显示不指望 DRM |
| simplefb | `chosen/framebuffer0` L45-52 (0xbfd00000, 240x320, stride 512, r5g6b5); `framebuffer_reserved` L84-87 no-map | ✅ FB_SIMPLE+FB_CONSOLE=y | **预期可用** — 已实测 fb0 注册 + fbcon 切换 (log L849-853) |
| battery | `battery` L257-263 `okay` (simple-battery 静态参数) | ✅ | 静态节点 OK（本身无驱动行为） |
| adc_battery | `adc_battery` L265-272 `okay`; io-channels 指向 `&mt6323_adc` (BATSNS=ch7, BAT_TEMP=ch2) | ✅ 三个 CONFIG 都在 | **预期不可用（defer）** — mt6323_adc 的 cell 不创建 → 无 IIO channel。**dts 里 L249-255 注释已过时**（写 "not set" 的三个 CONFIG 现在全 =y） |

> 注: dts 中 `battery`/`adc_battery` 状态虽都是 okay，但运行时不依赖 dts 状态——它 defer 是因为 `io-channels` 引用的 `mt6323_adc` 从未被实例化。

---

## 3. 上板 log 原文摘录

来源: `mainline_recon/z1_boot_mainline_20260814_002746.log`（`grep -a`，行号为物理行号）。交叉验证 log `z1_boot_mainline_20260813_201957.log` 结果逐条一致。

### USB (全部成功)
```
L1053  [    6.475204] probe of usb_phy_generic.2.auto returned 0 after 11969 usecs
L1055  [    6.505891] probe of musb-hdrc.3.auto returned 0 after 22369 usecs
L1056  [    6.513555] probe of 11100000.usb returned 0 after 50647 usecs
L1069  [    6.600409] g_serial gadget.0: Gadget Serial v2.4
L1070  [    6.606082] g_serial gadget.0: g_serial ready
L1071  [    6.611449] probe of gadget.0 returned 0 after 11387 usecs
```
→ 板侧 gadget 栈完全起来。log 未出现 ttyGS0 行是正常的（节点在插线/绑定后才出现），下次插线确认。

### 矩阵键 (成功)
```
L1086  [    6.712356] input: mt6779-keypad as /devices/platform/soc/10002000.keyboard/input/input0
L1087  [    6.722683] probe of 10002000.keyboard returned 0 after 11246 usecs
```

### PMIC 键 (驱动注册，无设备可绑)
```
L1089  [    6.739411] calling  pmic_keys_pdrv_init+0x0/0x10 @ 1
L1090  [    6.745654] initcall pmic_keys_pdrv_init+0x0/0x10 returned 0 after 261 usecs
```
→ initcall 只注册 driver；**log 全篇没有 `probe of ...mt6323keys`，也没有第二个 input 设备**。

### Goodix (驱动注册，节点 disabled)
```
L1097  [    6.795927] calling  goodix_ts_driver_init+0x0/0x10 @ 1
L1098  [    6.802143] initcall goodix_ts_driver_init+0x0/0x10 returned 0 after 72 usecs
```
→ 无 probe。

### 显示 (成功)
```
L849   [    0.090737] simple-framebuffer bfd00000.framebuffer: framebuffer at 0xbfd00000, 0x28000 bytes
L850   [    0.090781] simple-framebuffer bfd00000.framebuffer: format=r5g6b5, mode=240x320x16, linelength=512
L851   [    0.091960] Console: switching to colour frame buffer device 30x40
L852   [    0.092480] simple-framebuffer bfd00000.framebuffer: fb0: simplefb registered!
```

### 电池 (defer 死循环 + 根因)
```
L1132  [    7.052661] probe of adc-battery returned -517 after 31 usecs
L1371  [    9.213451] mt6397 1000f000.pwrap:mt6323: MT6323 regulator-only diagnostic mode; IRQ controller disabled
L1376  [    9.248049] probe of adc-battery returned 517 after 117 usecs
L1377  [    9.263967] probe of adc-battery returned 517 after 107 usecs
```
→ `-517 = -EPROBE_DEFER`。deferred probe 阶段重试仍 517（L1376/L1377），因为 auxadc 的 MFD cell 不存在，IO channel 永远不就绪。

### 辅助 (i2c1 / uart0 / eMMC 均成功)
```
L1115  [    6.922208] probe of 1100a000.i2c returned 0 after 1012 usecs    # i2c1
L950   [    5.721964] probe of 11005000.serial returned 0 after 5437919 usecs  # uart0 (console=ttyS0)
L1273  [    8.481990] probe of mmc0:0001 returned 0 after 160871 usecs      # eMMC
```

---

## 4. 下次上板一次性验证清单

> 板子起来后先 `dmesg | grep -Ei 'musb|g_serial|keypad|goodix|simplefb|adc-battery|regulator-only|auxadc'` 全览。以下命令按设备分列。

### 4.1 USB CDC-ACM
```sh
ls -l /dev/ttyGS*          # 期望: /dev/ttyGS0（若没出现，插 USB 线后再 ls —— musb 挂线才使能 gadget）
dmesg | grep -a 'g_serial'  # 期望: "g_serial gadget.0: Gadget Serial v2.4" + "g_serial ready"
```
回环收发（板侧开 cat，主机侧往串口发字符应能回显）:
```sh
cat /dev/ttyGS0 &          # 后台收
echo hello > /dev/ttyGS0   # 期望 cat 回显 hello（g_serial 默认 loopback 功能）
```
主机侧: 插 USB 后应出现 CDC ACM 口 (ttyACMx)，`cat /dev/ttyACMx` / `echo` 收发。

### 4.2 矩阵按键 (mt6779-keypad)
```sh
cat /proc/bus/input/devices   # 期望: Name="mt6779-keypad", Handlers=event0
evtest /dev/input/event0      # 若有 evtest: 按 VolUp/VolDown 应报 KEY_VOLUMEUP(115)/KEY_VOLUMEDOWN(114)
# busybox 无 evtest 时:
od -An -tx1 /dev/input/event0  # 按 VolUp/VolDown，看 16 字节 event 头出现非 0 的 keycode 字段
```
注意: **当前 dts 只映射 2 键**。若想测更多物理键，需在 `&keyboard` 的 `linux,keymap` 增补行并重编；矩阵行列换算风险见 CLAUDE.md §4a（`row_shift=get_count_order(n_cols)`，9 列 → row_shift=4，物理行列可能错位需上板调 dts）。

### 4.3 PMIC 电源键 (预期失败，用于确认根因)
```sh
ls /dev/input/                # 期望: 只有 event0；**没有** power key 的 event
dmesg | grep -a 'regulator-only'  # 期望命中: "MT6323 regulator-only diagnostic mode; IRQ controller disabled"
```
→ 这就是 PMIC 键没起来的实锤。**要修**：见 §5。

### 4.4 Goodix 触摸 (预期失败，节点 disabled)
```sh
ls /dev/input/                      # 期望: 无触摸 event
i2cdetect -y -r 1 2>/dev/null       # 若 busybox 有 i2cdetect: 看 0x5d 是否有地址响应（只确认硬件在不在，不影响 probe）
dmesg | grep -a 'goodix\|gt911'     # 期望: 只有 initcall 行，无 probe 行
```
→ **当前 dts 就是 disabled，本轮无法测触摸**。要测需: `touchscreen@5d` status 改 `"okay"` + 重编刷机（goodix.c 的 DT keymap patch 已就位，`goodix,keymap = <KEY_BACK KEY_HOME KEY_APPSELECT ...>` 已写在 dts）。

### 4.5 显示 (simplefb)
```sh
ls -l /dev/fb0                            # 期望存在
cat /sys/class/graphics/fb0/virtual_size  # 期望: 240,320
cat /sys/class/graphics/fb0/bits_per_pixel # 期望: 16
dmesg | grep -a 'simple-framebuffer'      # 期望: fb0: simplefb registered!
echo -e '\x1b[31mVISIBLE\x1b[0m' > /dev/tty0   # 板屏应显示彩色字（console=tty0 + fbcon 已切到 fb0）
```
上电后屏幕应该已显示内核 console（fbcon 在 L851 已切换 30x40）。ST7785M DRM panel 本轮不用测（dts disabled + 无 DRM）。

### 4.6 电池 (adc-battery，预期 defer，用于确认缺口)
```sh
ls /sys/class/power_supply/     # 期望: 目前只有 gpio 充电器/电池相关，**无 adc-battery**
cat /sys/class/power_supply/adc-battery/voltage_now   # 期望: No such file（未 probe）
ls /sys/bus/iio/devices/        # 期望: 空 —— auxadc cell 从未实例化
dmesg | grep -a 'adc-battery'   # 期望: "probe of adc-battery returned 517"（EPROBE_DEFER 实锤）
```

---

## 5. 电池/PMIC 键修复缺口 (IIO 依赖链)

**根因链**:
```
CONFIG_MFD_MT6323_NO_IRQ_DIAGNOSTIC=y
  → mt6397_probe 走 mt6323_diagnostic_devs（只含 mt6323-regulator，mt6397-core.c:163-168）
  → mt6323-auxadc MFD cell 不创建（本应来自 mt6323_devs:126-132）
  → mt6323_adc DT 节点无 platform device → auxadc 驱动(已编=CONFIG_MEDIATEK_MT6323_AUXADC=y) 永远不 probe
  → adc-battery io-channels 永远 -EPROBE_DEFER
  同理 mt6323-keys cell 不创建 → PMIC 电源键不 probe
```

**修复路径**（需重编 + 重打包 + 刷机，不在本次只读范围内，仅记录）:
1. `.config` 置 `CONFIG_MFD_MT6323_NO_IRQ_DIAGNOSTIC=n`；
2. 重编 zImage + 重打包 boot.img（appdtb 拼接 + mkbootimg 流程见 CLAUDE.md §4a）+ 刷机；
3. **风险**: 关掉 diag 模式后 `mt6397_probe` 会走 `mt6397_irq_init`（mt6397-irq.c:172），需要 pwrap 写 `MT6323_INT_CON0/1` 清零 + 建 IRQ domain。**Z1 pwrap write-deny gate 若仍挡写，irq_init 会失败 → mt6323 MFD probe 直接失败（连 regulator 都没了）**。这是当初 fork 置 diag 模式的原因，需上板实测：若 irq_init 失败，退路是给 `mt6397_irq_init` 加"只建 cell 不初始化 IRQ"的开关，或让 auxadc 走非 MFD 路径。
4. 若只想救电池不动 keys: 可在 `mt6323_diagnostic_devs` 里手动补一个 `mt6323-auxadc` cell（改 `drivers/mfd/mt6397-core.c`），auxadc probe 只要 pwrap regmap（`dev_get_regmap(dev->parent->parent)`），不依赖 IRQ，应该能直接工作。

**不需要重编的部分**: USB、矩阵键、显示（当前 img 已全 OK）；goodix 只要 dts 改 enabled + 重编（驱动已编）。

---

## 6. 附件: 关键文件/行号速查

- `fork_linux/.config`: CONFIG_MFD_MT6323_NO_IRQ_DIAGNOSTIC=y (L2668), CONFIG_KEYBOARD_MT6779=y (L1715), CONFIG_KEYBOARD_MTK_PMIC=y (L1716), CONFIG_GENERIC_ADC_BATTERY=y (L2311), CONFIG_MEDIATEK_MT6323_AUXADC=y (L4843), CONFIG_USB_G_SERIAL=<SERIAL> (L3984), CONFIG_FB_SIMPLE=y (L3603)
- `fork_linux/drivers/mfd/mt6397-core.c`: L126-161 `mt6323_devs`(全 cell), L163-168 `mt6323_diagnostic_devs`(仅 regulator), L403-421 diag 分支
- `fork_linux/drivers/iio/adc/mt6323-auxadc.c`: L249-283 probe（只需 pwrap regmap）
- `fork_linux/drivers/mfd/mt6397-irq.c`: L172-219 `mt6397_irq_init`（关 diag 后的必经路径）
- `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts`: &usb L279, &keyboard L404, &i2c1+touchscreen L416-450, &mt6323keys L322, chosen/framebuffer0 L45-52, battery L257, adc_battery L265, &dsi L467
- `fork_linux/arch/arm/boot/dts/mediatek/mt6572.dtsi`: keyboard@10002000 L236, usb@11100000 L484, u2phy0 L506, i2c1@1100a000 L457
- log: `mainline_recon/z1_boot_mainline_20260814_002746.log`（grep 需 `-a`）
