# MT6572 Mainline Clone 备料报告 (2026-07-10)

## A. Clone 结果

| 仓库 | 结果 | 备注 |
|---|---|---|
| `mt6572-mainline/linux` fork | ❌ 失败 | GitHub TLS recv error，换成 mirror/ghproxy 也不行 |
| `torvalds/linux` (官方内核) | ✅ 成功 | depth=1 完整 clone 到 `mainline_recon/torvalds/` (2.0GB) |
| fork 关键文件（驱动+DTS） | ✅ 成功 | 通过 curl/WebFetch 逐个抓取到 `mainline_recon/fork_files/` |

**torvalds 内核版本**: Linux **7.0-rc7** (Makefile: `VERSION=7, PATCHLEVEL=0, SUBLEVEL=0, EXTRAVERSION=-rc7`)

---

## B. fork 与 upstream 对比

### B.1 Upstream (torvalds 7.0-rc7) 已有的 MT6572 内容

```
arch/arm/boot/dts/mediatek/mt6572.dtsi              ← 108行 SoC基础
arch/arm/boot/dts/mediatek/mt6572-lenovo-a369i.dts  ← 56行 手机板
arch/arm/boot/dts/mediatek/mt6572-jty-d101.dts      ← 61行 平板板
```

mt6572.dtsi 含: cpu, timer@10008000, uart0@11005000(disabled), uart1@11006000(disabled), gic@10211000, sysirq@10200100, wdt@10007000, fixed-clocks(clk26m/clk13m/clk32k)

### B.2 Fork 独有 — extra driver 文件清单 (已全部下载到 fork_files/)

**Clock 驱动** (上游完全没有, fork 🟢 OK):
| 文件 | 作用 |
|---|---|
| `drivers/clk/mediatek/clk-mt6572-topckgen.c` | 主时钟控制器 |
| `drivers/clk/mediatek/clk-mt6572-infracfg.c` | 基础设施时钟 |
| `drivers/clk/mediatek/clk-mt6572-apmixedsys.c` | PLL/音频混合时钟 |

**Pinctrl** (上游没有):
| 文件 | 作用 |
|---|---|
| `drivers/pinctrl/mediatek/pinctrl-mt6572.c` | GPIO/pinmux (⚠️ 缺 eMMC r1r0 高速 pins) |

**MMC** (上游 mtk-sd.c 无 mt6572 compat, fork 用 mt2701 fallback):
| 文件 | 说明 |
|---|---|
| `drivers/mmc/host/mtk-sd.c` | fork 版本可能加了 mt6572 compat 或只用 mt2701-mmc |
| **upstream mtk-sd compat 列表**: mt2701/2712/6779/6795/7620/7622/7986/7988/8135/8173/8183/8196/8516 — **无 mt6572** |

**PMIC/MT6323** (上游已有 fork 扩展):
| 文件 | 作用 |
|---|---|
| `drivers/regulator/mt6323-regulator.c` | 电压调节器 |
| `drivers/power/reset/mt6323-poweroff.c` | 电源管理关机 |
| `drivers/nvmem/mt6323-efuse.c` | eFuse 读写 |
| `drivers/leds/leds-mt6323.c` | LED 控制 |
| `drivers/iio/adc/mt6323-auxadc.c` | ADC (Roman Vivchar v5 upstreaming中) |

**SoC/显示**:
| 文件 | 作用 |
|---|---|
| `drivers/soc/mediatek/mt6572-mmsys.h` | 多媒体子系统路由表 |
| `drivers/pmdomain/mediatek/mt6572-pm-domains.h` | 电源域 (仅 disp/mfg 域工作) |

### B.3 Fork 自报驱动状态表 (README.md 原文)

**Buses**:
| 组件 | 驱动 | 状态 |
|---|---|---|
| UART | mediatek,mt6577-uart | 🟢 OK |
| I2C | mediatek,mt6572-i2c | 🟢 OK, needs upstreaming |
| SPI | — | 🔴 DEAD |
| USB | mediatek,mtk-musb | 🟢 OK |
| USB PHY | mediatek,generic-tphy-v1 | 🟢 OK |

**Clocks** (全部 needs upstreaming):
| 组件 | 状态 |
|---|---|
| topckgen/infracfg/apmixed | 🟢 OK |
| mmsys | 🟡 partial (cg1 dbi 时钟缺) |
| mfgcfg | 🟢 OK |
| fhctl | 🔴 DEAD |
| audio | 🔴 DEAD |

**Storage**:
| 组件 | 驱动 | 状态 |
|---|---|---|
| eMMC | mediatek,mt2701-mmc | 🟢 OK, no PMT parser |
| microSD | mediatek,mt2701-mmc | 🟢 OK |
| NAND | — | 🔴 DEAD |

**Display**:
| 组件 | 状态 |
|---|---|
| DRM | 🟡 partial — fallback 到 mt2701 plat data, 需自己数据(只有1个RDMA) |
| DSI | 🟡 partial — needs upstreaming, 可能与 mt2701 兼容 |
| DSI PHY | 🟢 — 用 mt2701-mipi-tx |
| IOMMU/OVL/RDMA/SMI/LARB | 🟢 OK, needs upstreaming |
| GPU (Mali-400) | 🟢 OK |
| WDMA/CMDQ/DBI/DPI | 🔴 DEAD |
| BLS (背光) | 🟡 partial — 亮度不工作 |

**SoC/杂项**:
| 组件 | 状态 |
|---|---|
| pwrap | 🟢 OK (mediatek,mt6572-pwrap) |
| power domain | 🟡 partial (仅 disp/mfg pd) |
| thermal | 🟡 partial (温度偏高) |
| WDT/reset | 🟢 OK |
| PMIC regulator/efuse/thermal/ADC | 🟢 OK |

**⚠️ Fork 完全没有的** (README 确认 DEAD/不存在):
| 子系统 | 状态 | 说明 |
|---|---|---|
| **WiFi / BT (WCN combo)** | 🔴 DEAD | Connectivity 段写 "not existent in the upstream" |
| **音频 ASoC** | 🔴 DEAD | "needs afe/i2s/etc drivers" |
| **摄像头** | 🔴 DEAD | |
| **触摸屏** | 🔴 DEAD (JTY/Lenovo板) | 仅 Prestigio PAP5500 DUO 有 GT911 |
| **Charger** | 🔴 DEAD | 所有板都没 |
| **SPI** | 🔴 DEAD | |
| **NAND** | 🔴 DEAD | |
| **CPUIDLE** | 🔴 DEAD | "probably needs new driver, wfi may work" |

### B.4 Fork DTS 板级文件 (4块板 + PMIC dtsi)

```
mt6572.dtsi                    ← SoC基础 (fork版比upstream多很多节点)
mt6572-lenovo-a369i.dts        ← 512MB 手机
mt6572-jty-d101.dts            ← 平板
mt6572-prestigio-pap5500-duo.dts ← Prestigio 手机 (新增板)
mt6572-energy-phone-colors.dts   ← Energy Phone Colors (新增板)
mt6323.dtsi                    ← PMIC MT6323 设备树 (有!)
```

**注意**: fork 的 mt6572.dtsi 比 upstream 的丰富——多出的都是 fork 自带驱动(clk/pinctrl/pwrap/msdc/dsi/mmsys 等)需要的节点。

---

## C. 对 Z1 的评估

### 已有且可直接用的 (fork 已实现)
- ✅ **eMMC**: mt2701-mmc compatible (🟢 OK)，Z1 的 SanDisk 4GB eMMC 可识别
- ✅ **microSD**: 同上 (🟢 OK)
- ✅ **Clock**: topckgen/infracfg/apmixed 全 🟢
- ✅ **UART/ttyMT0**: mt6577-uart fallback (🟢 OK)
- ✅ **I2C**: mt6572-i2c (🟢 OK)
- ✅ **USB**: mtk-musb + generic-tphy-v1 (🟢 OK)
- ✅ **PMIC MT6323**: regulator/efuse/adc/thermal/led/poweroff 全 🟢
- ✅ **PWRAP**: mt6572-pwrap (🟢 OK)
- ✅ **WDT/reset**: mt6572-wdt (🟢 OK)
- ✅ **SMP/hotplug**: platsmp.c (🟢 OK)

### 部分可用 (需补代码/DTS配置)
- 🟡 **DRM/显示**: fallback mt2701 plat data, 需 Z1 自己的 ddp_data(只有1个RDMA)。🟡 partial
- 🟡 **DSI**: 可能兼容 mt2701, 需面板 ST7785M 适配 (改编 panel-sitronix-st7789v.c 的 init sequence)
- 🟡 **Pinctrl**: fork 有但缺 eMMC r1r0 高速 pin (基础模式 OK)
- 🟡 **背光**: MPWM 可能可用 mt6323 控制

### 完全缺 (Z1 后续大活)
- ❌ **WiFi/BT (MTK WCN combo mt66xx)**: 不管 mainline 还是 fork 都没有。需从 3.4 vendor 内核移植 `drivers/misc/mediatek/connectivity/`
- ❌ **音频**: AFE/I2S 都没有。Max 那 11-patch 系列是 SOC 基础不是 ASoC

---

## D. Z1 跑 mainline 最短步骤 (7.0-rc7 + fork 驱动)

### Step 1: 搭建代码树
```bash
cd /home/<user>/claude/6572/mainline_recon
# 已有: torvalds/(7.0-rc7), fork_files/(fork额外驱动)
# 把 fork_files 里多出来的驱动文件拷到 torvalds 对应位置
# cp -r fork_files/drivers/clk/mediatek/clk-mt6572* torvalds/drivers/clk/mediatek/
# cp -r fork_files/drivers/pinctrl/mediatek/pinctrl-mt6572.c torvalds/drivers/pinctrl/mediatek/
# cp fork_files/drivers/soc/mediatek/mt6572-mmsys.h torvalds/drivers/soc/mediatek/
# ... 等等
# 用 fork 的 mt6572.dtsi + mt6323.dtsi 替换 upstream 的
```

### Step 2: 写 Z1 板 DTS
文件: `arch/arm/boot/dts/mediatek/mt6572-j72-z1.dts`
基于 `mt6572-lenovo-a369i.dts` 抄板:
- model = "JCB Z1"
- compatible = "wel,j72_z1_v0_1_wel", "mediatek,mt6572"
- memory: 0x80000000 ~ 0xBFFFFFFF (1GB)
- stdout-path = "serial0:921600n1"
- &uart0 { status="okay" }
- &mmc0 { non-removable, bus-width=8, compatible="mediatek,mt2701-mmc" }
- &pwrap { 接 mt6323 }
- 暂不加 dsi/panel/wifi/bt/音频

### Step 3: 编译
```bash
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
# (需要装 ARM toolchain)
make multi_v7_defconfig
# make menuconfig: 开 CONFIG_MACH_MT6572, CONFIG_MMC_MTK, CONFIG_SERIAL_8250_MT6577, CONFIG_CLK_MT6572, CONFIG_PINCTRL_MT6572 等
make -j$(nproc) zImage dtbs
```

### Step 4: 打包 + 刷
```bash
# mkbootimg: kernel=zImage, dtb=mt6572-j72-z1.dtb, ramdisk=initramfs.cpio.gz, cmdline="console=ttyMT0,921600n1 rdinit=/init"
# mtkclient: python3 mtk.py w bootimg z1-mainline-boot.img
```

### 先验目标: UART console 出来 + eMMC 认到 mmcblk0

---

## E. 关键路径与 URL

| 资源 | 路径/URL |
|---|---|
| torvalds 内核 (本地) | `/home/<user>/claude/6572/mainline_recon/torvalds/` |
| fork 驱动文件 (本地) | `/home/<user>/claude/6572/mainline_recon/fork_files/` |
| upstream mt6572.dtsi | `mainline_recon/torvalds/arch/arm/boot/dts/mediatek/mt6572.dtsi` |
| fork mt6572.dtsi (增强版) | `mainline_recon/fork_files/arch/arm/boot/dts/mediatek/mt6572.dtsi` |
| fork README | `mainline_recon/fork_files/README.md` |
| Lenovo A369i DTS (Z1模板) | `mainline_recon/fork_files/arch/arm/boot/dts/mediatek/mt6572-lenovo-a369i.dts` |
| mt6323.dtsi (PMIC) | `mainline_recon/fork_files/arch/arm/boot/dts/mediatek/mt6323.dtsi` |
| mtk-sd compat列表 | `mainline_recon/torvalds/drivers/mmc/host/mtk-sd.c` |
| fork Git | `https://github.com/mt6572-mainline/linux` (master, 66K+ commits) |
| fork 组织 | `https://github.com/mt6572-mainline` |
