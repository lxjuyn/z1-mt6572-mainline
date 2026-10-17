# MTK Combo WiFi/BT Driver移植依赖分析报告

## 0. 概述

**目标设备:** 佳伯听力机 Z1 (MTK MT6572, Cortex-A7 双核, armv7)  
**主线内核目标:** Linux 7.0-rc7  
**Combo 芯片:** MT6627/MT6620 系列 (WiFi+BT+FM+GPS via WMT)  
**接口:** SDIO (WiFi) + UART (BT via STP)  

---

## 1. 源码树 Clone 状态

### 1.1 mt6572_kernel_drsami (主参考树)
- **来源:** `https://github.com/drsami/mt6572_kernel.git`
- **状态:** 成功 clone (depth=1, 最终通过 --no-checkout + reset 方式)
- **提交:** c4b775c4 "Initial source from Acer"
- **体积:** 1.6 GB
- **路径:** `/home/<user>/claude/6572/wifi_recon/mt6572_kernel_drsami`

### 1.2 aquaris_bq (交叉参考树)
- **来源:** `https://github.com/bq/aquaris-E5.git`
- **状态:** 成功 clone (通过 `--filter=blob:none --depth 1 --no-checkout` + sparse checkout)
- **体积:** 19 MB (仅拉取 mediatek/kernel/drivers/combo/ 和 mediatek/config/)
- **路径:** `/home/<user>/claude/6572/wifi_recon/aquaris_bq`
- **用途:** aquaris-E5 使用的是 MTK 6582 + CONSYS 方案 (MTK_COMBO_CHIP=CONSYS_6582), 驱动代码与 MT6572/MT6620 同源, 可交叉参考。aquaris_bq 的 proc 代码已用 `proc_create()` (更新), 有 `USE_NEW_PROC_FS_FLAG` 编译开关, 比 drsami 树更新。

---

## 2. Combo 源码目录结构总览

两棵源码树的 combo 目录结构基本一致:

```
mediatek/kernel/drivers/combo/
├── Makefile              # 顶层 Makefile, 编译 common + drv_wlan
├── common/               # WMT core + STP + HIF-SDIO 公共代码
│   ├── core/             # WMT 核心协议栈 + STP 核心 + PSM (电源状态管理)
│   │   ├── wmt_core.c / wmt_ctrl.c / wmt_func.c / wmt_ic_6620.c / wmt_ic_6628.c / wmt_lib.c
│   │   ├── stp_core.c / stp_exp.c / psm_core.c / btm_core.c
│   │   └── include/      # wmt_core.h, stp_core.h, wmt_lib.h, psm_core.h, ...
│   ├── include/          # 顶层公开头文件
│   │   ├── wmt.h, wmt_exp.h, stp_exp.h, wmt_plat.h, wmt_plat_stub.h
│   │   └── mtk_wcn_cmb_hw.h
│   ├── linux/            # Linux 平台适配层 (关键)
│   │   ├── include/      # hif_sdio.h, osal.h, osal_typedef.h
│   │   ├── pri/          # WMT 设备驱动 + thermal management
│   │   │   ├── wmt_dev.c (wmt platform device, char dev, thermal ctrl)
│   │   │   ├── wmt_tm.c (thermal zone + stats timer)
│   │   │   ├── stp_dbg.c, stp_exp.c, stp_uart.c, wmt_tm.c
│   │   └── pub/          # 公共接口实现
│   │       ├── hif_sdio.c (SDIO bus driver - WiFi + WMT 共用)
│   │       ├── hif_sdio_chrdev.c
│   │       ├── osal.c (OS abstraction: timer, workqueue, wake_lock, fifo)
│   │       ├── stp_chrdev_bt.c, stp_chrdev_gps.c, wmt_chrdev_wifi.c
│   ├── platform/alps/    # Android/ALPS 平台板级适配
│   │   ├── wmt_plat_alps.c (GPIO, EINT, RTC, wake_lock, GPIO ctrl)
│   │   ├── mtk_wcn_cmb_hw.c (power on/off/reset via hardware sequence)
│   │   ├── mtk_wcn_cmb_stub_alps.c (stub for mt_dcm, pmu)
│   │   └── wmt_plat_stub.c
├── drv_wlan/             # WiFi 驱动 (wlan_mt66xx.ko 源码)
│   ├── mt6620/wlan/      # MT6620 专用驱动
│   ├── mt6628/wlan/      # MT6628 专用驱动 (结构与 mt6620 完全一致)
│   │   ├── common/       # wlan_lib.c, wlan_oid.c, wlan_bow.c, wlan_p2p.c
│   │   ├── mgmt/         # 802.11 协议栈: ais_fsm, assoc, auth, scan, rlm, sec_fsm...
│   │   ├── nic/          # NIC 层: nic.c, nic_rx.c, nic_tx.c, que_mgt.c...
│   │   └── os/linux/     # Linux glue layer (最关键移植点)
│   │       ├── gl_init.c           # 设备创建/注册, wiphy_new, net_device setup
│   │       ├── gl_cfg80211.c       # cfg80211 接口 (connect/disconnect/scan)
│   │       ├── gl_p2p_cfg80211.c   # P2P cfg80211 接口
│   │       ├── gl_kal.c            # 内核抽象层, channel 管理, tx/rx
│   │       ├── gl_p2p.c            # P2P glue
│   │       ├── gl_wext.c           # WEXT 接口 (deprecated)
│   │       ├── gl_wext_priv.c      # WEXT private ioctls
│   │       ├── gl_proc.c           # /proc 调试接口
│   │       ├── gl_bow.c            # Block Ack optimization
│   │       ├── gl_rst.c            # Reset handling
│   │       ├── platform.c          # Platform 适配
│   │       └── hif/sdio/sdio.c     # SDIO HIF 实现
└── common_mt6630/        # MT6630 variant (同目录结构, aquaris_bq 独有)
```

### 主要 .c/.h 文件清单

| 模块 | 关键文件 | 行数估计 |
|------|---------|---------|
| **WMT Core** | wmt_core.c, wmt_ctrl.c, wmt_func.c, wmt_lib.c, wmt_ic_6620.c, wmt_ic_6628.c | ~500K |
| **STP Core** | stp_core.c, psm_core.c, btm_core.c | ~250K |
| **Linux HIF-SDIO** | hif_sdio.c, osal.c, wmt_dev.c, stp_sdio.c, stp_uart.c | ~300K |
| **WiFi wlan_lib** | wlan_lib.c, wlan_oid.c, wlan_bow.c | ~600K |
| **WiFi mgmt** | ais_fsm.c, scan.c, p2p_fsm.c, rlm.c, sec_fsm.c | ~1M |
| **WiFi nic** | nic.c, nic_rx.c, nic_tx.c, que_mgt.c | ~500K |
| **Glue Layer** | gl_init.c, gl_cfg80211.c, gl_kal.c, gl_p2p.c | ~800K |
| **Platform** | wmt_plat_alps.c, mtk_wcn_cmb_hw.c | ~120K |

---

## 3. 依赖 MTK 私有内核 API 清单 (关键产出)

### 3.1 MTK WMT/STP/HIF 私有内核符号

这些 API 在 MTK 3.4.67 内核中通过 MTK kernel patch 引入, 主线内核没有:

| # | API 符号/头文件 | 使用位置 | 移植到 7.0 需要 |
|---|----------------|---------|----------------|
| 1 | `mtk_wcn_hif_sdio_client_reg()` / `_unreg()` | hif_sdio.h, 被 wlan 驱动调用 | **需实现 stub 层**, 将 SDIO bus 注册改为主线的 `sdio_register_driver()` + 自定义 client 架构 |
| 2 | `mtk_wcn_hif_sdio_readb/writeb/readl/writel` | hif_sdio.h | **需映射到 `sdio_f0_readb()`/`sdio_claim_irq()` 等主线 API** |
| 3 | `mtk_wcn_hif_sdio_read_buf/write_buf` | hif_sdio.h | **需映射到 `sdio_readb()` + DMA 缓冲区 API** |
| 4 | `mtk_wcn_hif_sdio_bus_set_power()` | hif_sdio.h | **需映射到 `mmc_set_ios()`/`mmc_power_up()`** |
| 5 | `mtk_wcn_hif_sdio_wmt_control()` | hif_sdio.h, WMT cmd 通道 | **需实现 SDIO function 2 上的 cmd/response 机制** |
| 6 | `mtk_wcn_hif_sdio_update_cb_reg()` | hif_sdio.h, 回调注册 | **需改为主线中断注册 + SDIO IRQ** |
| 7 | `mtk_wcn_stp_init()` / `_deinit()` / `_enable()` / `_ready()` | stp_core.h | **需实现 STP 协议栈, 主线没有 STP** (BT 需直接走 HCI UART) |
| 8 | `mtk_wcn_stp_send_data()` / `_receive_data()` / `_parser_data()` | stp_exp.h | **需实现 STP L2CAP 到 HCI 的映射** |
| 9 | `mtk_wcn_stp_register_if_tx()` / `_if_rx()` | stp_exp.h | **需注册 SDIO/UART 传输接口** |
| 10 | `wmt_lib_init()` / `_deinit()` / `_tx()` / `_rx()` | wmt_lib.h | **需实现 WMT cmd 通道** (WMT 是 MTK 私有协议) |
| 11 | `wmt_lib_get_icinfo()`, `wmt_lib_set_patch_name()`, `wmt_lib_set_patch_info()` | wmt_lib.h | **需实现固件加载逻辑** |
| 12 | `wmt_lib_msgcb_reg()` / `_unreg()` | wmt_lib.h | **需实现 WMT 消息回调机制** |
| 13 | `wmt_lib_psm_lock_aquire()` / `_release()` / `_notify_stp_sleep()` | wmt_lib.h | **需实现电源状态管理** |
| 14 | `mtk_wcn_wmt_therm_ctrl()` | wmt_tm.c, wmt_dev.c | **需实现 thermal 接口 stub** |
| 15 | `mtk_wcn_cmb_hw_pwr_on()` / `_pwr_off()` / `_rst()` / `_init()` / `_deinit()` | mtk_wcn_cmb_hw.h | **需实现 power sequencer** (通过 GPIO/PMIC 控制) |
| 16 | `wmt_plat_gpio_ctrl()`, `wmt_plat_eirq_ctrl()`, `wmt_plat_sdio_ctrl()` | wmt_plat.h | **需实现板级 GPIO 控制** (GPIO lib API) |
| 17 | `wmt_plat_irq_ctrl()`, `wmt_plat_wake_lock_ctrl()`, `wmt_plat_audio_ctrl()` | wmt_plat.h | **需实现中断+音频接口板级适配** |

### 3.2 Vendor 头文件 `<mach/...>` 依赖

这些头文件是 MTK 3.4.67 内核独有的, 主线内核不存在:

| # | 头文件 | 使用位置 | 移植到 7.0 需要 |
|---|--------|---------|----------------|
| 1 | `<mach/mtk_wcn_cmb_stub.h>` | wmt_plat.h, psm_core.c, wmt_lib.h, wmt_plat_stub.c, wmt_plat_alps.c | **需重写整个 CMB stub 层** -- 包含 PMU, GPIO, RTC, dcm 等 MTK 专有抽象。MTK 的 consys 框架 (kernel 5.10+) 有部分类似代码, 但接口完全不同 |
| 2 | `<mach/mt_gpio.h>` | wmt_plat_alps.c | **映射到 `linux/gpio/consumer.h` (gpiod API)** |
| 3 | `<mach/mt6573_gpio.h>` / `mt6575_gpio.h` / `mt6516_gpio.h` | wmt_plat_alps.c, sdio.c | **统一用 gpiod API, 通过 DT 获取 GPIO** |
| 4 | `<mach/mt6573_eint.h>` / `<mach/eint.h>` | wmt_plat_alps.c | **映射到 `irq_get_irq_data()` / `irq_set_chained_handler()`** |
| 5 | `<mach/mt6573_boot.h>` / `mt6575_boot.h` / `mt6577_boot.h` | wmt_plat_alps.c | **可能只需 DT 获取 Boot 状态, 具体看用途** |
| 6 | `<mach/mt6573_pll.h>` / `mt6573_devs.h` | wmt_plat_alps.c | **可能不需要, MTK PLL/clk 已不在 `<mach/>` 下** |
| 7 | `<mach/mtk_rtc.h>` | wmt_plat_alps.c | **映射到 `rtc_class.h` 或直接使用 `getnstimeofday64()`** |
| 8 | `<mach/mt_dcm.h>` | mtk_wcn_cmb_stub_alps.c | **时钟门控, 映射到 `clk_prepare_enable()/clk_disable_unprepare()`** |
| 9 | `<mach/mtk_thermal_monitor.h>` | wmt_tm.c | **映射到 `thermal_zone_device_register()`** |

### 3.3 已废弃/变更的 Linux 核心 API

#### 定时器 API 变更

| # | 旧 API (3.4.67) | 新 API (7.0) | 使用位置 | 影响等级 |
|---|----------------|-------------|---------|---------|
| 1 | `init_timer(&timer)` + `timer->function = f; timer->data = d;` | `timer_setup(&timer, f, 0)` + `from_timer()` | osal.c:614-617, wmt_tm.c:1330, gl_kal.c:3301 | **高** - 需要包装函数签名 |
| 2 | `add_timer(timer)` | 无直接等价, 通过 `mod_timer()` 激活 | osal.c:625 | **中** - 需合并到 timer_setup |
| 3 | `del_timer(timer)` / `del_timer_sync(timer)` | 仍可用, 但返回类型和语义有变化 | osal.c:632-639, gl_kal.c:3324 | **低** |
| 4 | `mod_timer(&timer, jiffies + ms*HZ/1000)` | `mod_timer(&timer, jiffies + ms_to_jiffies(ms))` | osal.c:646 | **低** |
| 5 | `DEFINE_TIMER()` 宏 | 已移除 | (未发现使用) | 无 |

#### Workqueue API 变更

| # | 旧 API (3.4.67) | 新 API (7.0) | 使用位置 | 影响等级 |
|---|----------------|-------------|---------|---------|
| 1 | `INIT_DELAYED_WORK(&work, func)` 2-arg 版本 | 3.1+ 可用 2-arg, 7.0 仍兼容但推荐 `delayed_work` | gl_init.c:1794 | **低** (7.0 仍支持 2-arg) |
| 2 | `INIT_WORK(&work, func)` 2-arg 版本 | 仍可用 | hif_sdio.c | **低** |
| 3 | `schedule_work()` | 仍可用 | hif_sdio.c | **低** |
| 4 | `queue_work()` / `flush_work()` / `cancel_delayed_work()` | 7.0 中 `flush_work()` 被移除, 改为 `cancel_work_sync()` 后检查 | hif_sdio.c | **待研** |

#### Wake Lock API 变更

| # | 旧 API (3.4.67) | 新 API (7.0) | 使用位置 | 影响等级 |
|---|----------------|-------------|---------|---------|
| 1 | `wake_lock_init(&lock, WAKE_LOCK_SUSPEND, "name")` | `wake_lock_init(&lock, WL_STATE_SUSPEND, "name")` (enum 值变了) + `WAKE_LOCK_SUSPEND` 宏在 4.17+ 移除 | wmt_plat_alps.c:338, osal.c:1091 | **高** - 需完整替换为 `system_resume_block()`/`system_request_suspend()` |
| 2 | `wake_lock()` / `wake_unlock()` | 已移除 | wmt_plat_alps.c, osal.c | **高** |
| 3 | `wake_lock_destroy()` | 已移除 | wmt_plat_alps.c:359 | **高** |

#### procfs API 变更

| # | 旧 API (3.4.67) | 新 API (7.0) | 使用位置 | 影响等级 |
|---|----------------|-------------|---------|---------|
| 1 | `create_proc_entry(name, mode, parent)` | `proc_create(name, mode, parent, &fops)` | wmt_tm.c:1151, wmt_dev.c:648, gl_proc.c:718 | **高** - `create_proc_entry` 在 4.9+ 移除 |
| 2 | `entry->read_proc = f; entry->write_proc = f;` | `proc_create() { .read = f, .write = f }` | wmt_tm.c:1153-1154, wmt_dev.c:653-654 | **高** |
| 3 | `remove_proc_entry()` | 仍可用 | stp_sdio.c:2991 (已用 `proc_remove()` 条件编译) | **低** |

#### cfg80211 API 变更

| # | 旧 API (3.4.67) | 新 API (7.0) | 使用位置 | 影响等级 |
|---|----------------|-------------|---------|---------|
| 1 | `wiphy_new(&ops, sizeof(priv))` 2-arg | 5.17+ 改为 `wiphy_new_nm(&ops, sizeof_priv, name, dev)` | gl_init.c:2103 | **中** - 2-arg 宏在 7.0 仍存在, 但可能有警告 |
| 2 | `cfg80211_ap_settings` 结构体 | 拆分为 `cfg80211_new_assoc_params` + `cfg80211_bss` (5.8+ 变更) | gl_p2p_cfg80211.c | **高** - `start_ap` handler 签名变了 |
| 3 | `.ndo_do_ioctl = wlanDoIOCTL` | 7.0 中 `ndo_do_ioctl` 已被完全移除 | gl_init.c:2062 | **高** - WEXT ioctls 在 5.4+ 移除 |
| 4 | `wireless_handlers` 字段 | `wiphy->wext` 被移除, `wireless extensions` 在 5.4+ 完全移除 | gl_init.c:2158, 2214 | **高** |
| 5 | `struct ieee80211_supported_band` 中的 `.bitrates` | 5.x 改为 `.bitrates` + `n_bitrates` 语义不变, 但在 6.x 中 `rate_bits` 等字段变更 | gl_init.c:805-812 | **中** - MCS 数据结构可能需要调整 |
| 6 | `.ht_cap.mcs.rx_mask` 为数组 vs 位图 | 5.x 改 `.rx_mask` 从数组变为位图 (2 字节) | gl_init.c:786 | **高** - 2018 年 API 变更 |

#### Thermal API 变更

| # | 旧 API (3.4.67) | 新 API (7.0) | 使用位置 | 影响等级 |
|---|----------------|-------------|---------|---------|
| 1 | `mtk_thermal_zone_device_register()` (MTK patch API) | `thermal_zone_device_register()` (主线, 签名不同) | wmt_tm.c:1127 | **中** - 需使用 `thermal_zone_device_register_with_data()` |
| 2 | `mtk_thermal_cooling_device_register()` | `thermal_cooling_device_register()` | wmt_tm.c:1205 | **中** |
| 3 | `thermal_zone_bind_cooling_device()` | 仍可用 | wmt_tm.c:298 | **低** |

#### net_device 变更

| # | 旧 API (3.4.67) | 新 API (7.0) | 使用位置 | 影响等级 |
|---|----------------|-------------|---------|---------|
| 1 | `.ndo_set_multicast_list` | 3.2+ 改为 `.ndo_set_rx_mode` | gl_init.c:2059 | **低** (代码有版本宏保护) |
| 2 | `.nd_set_name = "wlan%d"` | 在 3.1 中已被移除 | (未发现直接使用) | 无 |
| 3 | `wireless_send_events()` (WEXT) | 已移除 | (可能存在) | 待研 |

### 3.4 Android 特有 API

| # | API | 使用位置 | 移植到 7.0 需要 |
|---|-----|---------|----------------|
| 1 | `xlog_printk()` (Android log kernel) | wmt_tm.c, 多处 | **替换为 `pr_debug()`/`pr_info()`/`pr_err()`** |
| 2 | `DBGLOG()` 宏 (驱动内部) | 所有驱动文件 | **替换为 `pr_<level>()`** |
| 3 | WAKE_LOCK / wakelock | wmt_plat_alps.c, osal.c | **替换为 system_pm_qos 或无** |
| 4 | `ANDROID_LOG_DEBUG/INFO` (Android 日志级别) | wmt_tm.c | **替换为标准 printk 级别** |

### 3.5 驱动初始化和板级绑定

| # | 旧方式 | 新方式 | 影响等级 |
|---|--------|--------|---------|
| 1 | WMT 通过 `wmt_dev.c` 中的 platform driver + `/proc` + 字符设备初始化 | 需通过 **Device Tree** 描述 WMT 资源 (GPIO, IRQ, SDIO functions) | **高** |
| 2 | SDIO 驱动通过 `hif_sdio.c` 注册 sdio_driver, 但用 MTK 私有 bus 层 | 需直接用 `sdio_register_driver()` | **高** |
| 3 | GPIO 配置通过 `mt6573_gpio.h` + `mt_gpio.h` | 通过 **Device Tree + gpiod API** | **高** |
| 4 | 电源序列通过 `mtk_wcn_cmb_hw.c` + `<mach/mtk_wcn_cmb_stub.h>` | **需完全重写 power sequencer** (PMIC/GPIO 控制) | **高** |

### 3.6 私有 API 统计

- **MTK WMT/STP/HIF 私有符号:** 约 45 个 (extern 声明)
- **Vendor `<mach/>` 头文件:** 约 10 个不同文件
- **已废弃核心 API (需改):** 定时器 5 处, wake_lock 10+ 处, procfs 15+ 处, cfg80211 结构体 3 处, ioctls 2 处, thermal 2 处
- **Android 专有 API:** `xlog_printk` 等日志宏 3+ 处
- **总计需要改造的 API 调用点:** 约 **80-100 处**

---

## 4. 芯片型号结论

### 4.1 固件证据

从 `/home/<user>/claude/6572/out/firmware_backup/` 中:

- **目录:** `firmware/mt6627/` 存在, 包含 `mt6627_fm_*_patch.bin` (FM patch)
- **固件:** `WIFI_RAM_CODE` 存在 (未明确标型号)
- **同时有:** `WIFI_RAM_CODE_MT6628` 和 `WIFI_RAM_CODE_MT6630` (兼容备选)
- **bin/6620_wmt_lpbk** 和 **bin/6620_launcher** 存在 (WMT loader 用 6620 ID)
- **WMT_SOC.cfg:** `co_clock_flag=1` (WiFi 和 BT 共时钟)

### 4.2 ProjectConfig.mk 证据

**mt6572_kernel_drsami / mt6572/ProjectConfig.mk:**
```
MTK_WLAN_CHIP=MT6620
MTK_COMBO_CHIP=MT6620E3
```

**aquaris_bq / common/ProjectConfig.mk:**
```
MTK_COMBO_CHIP=CONSYS_6582
MTK_WLAN_CHIP=CONSYS_MT6582
```

**aquaris_bq / vegetahd/ProjectConfig.mk:**
```
MTK_WLAN_CHIP=MT6620
MTK_COMBO_CHIP=MT6620E3
```

### 4.3 build.prop 证据

```
ro.mediatek.platform=MT6572
persist.mtk.wcn.combo.chipid=-1
```

### 4.4 结论

**Z1 实际使用的芯片极大概率为 MT6627 (或 MT6620), 两者在 MTK 驱动框架中为同一驱动 (MT6620 driver), 只是固件文件名不同。**

证据链:
1. `persist.mtk.wcn.combo.chipid=-1` 表示 WMT 自动检测 chipid
2. ProjectConfig.mk 明确设为 `MT6620`/`MT6620E3`
3. 固件目录 `firmware/mt6627/` 存在 FM patch
4. `bin/6620_launcher` 和 `bin/6620_wmt_lpbk` -- MTK 的 WMT 启动器用 chip ID, 6620 和 6627 驱动代码完全兼容
5. WMT_SOC.cfg 中 `co_clock_flag=1` 说明 WiFi 和 BT 共享同一个晶振

**MT6627 = MT6620 的变种** (同一 silicon revision 或 MTK 内部编号不同), **驱动代码共用 MT6620 driver 树**, 唯一区别是固件文件名和 patch 加载路径。

---

## 5. 移植关键路径总结

移植到 mainline 7.0 需要处理的核心模块:

1. **MTK WMT 协议栈** -- MTK 私有协议, 需重写或移植 consys 框架 (kernel 5.10+)
2. **STP (Serial Transport Protocol)** -- MTK 私有蓝牙传输层, UART 上的 STP 需映射到标准 HCI
3. **HIF-SDIO** -- MTK 私有 SDIO bus 抽象, 需改用标准 `sdio_*` API
4. **Power sequencer (CMB stub)** -- `<mach/mtk_wcn_cmb_stub.h>`, 需通过 DT + gpiod 重写
5. **WiFi wlan 驱动** -- 大部分 802.11 协议栈与主线兼容, 但需改:
   - `cfg80211_ap_settings` 结构体 (start_ap handler)
   - `mcs.rx_mask` 从数组改为位图
   - 移除 WEXT ioctls / `ndo_do_ioctl`
   - 移除 wake_lock
   - procfs 改用 `proc_create()`
6. **Platform 层** -- GPIO/IRQ/RTC/thermal 全部通过 DT 重写

---

## 6. 相关文件路径索引

| 文件 | 路径 |
|------|------|
| 驱动入口 | `/home/<user>/claude/6572/wifi_recon/mt6572_kernel_drsami/mediatek/kernel/drivers/combo/drv_wlan/mt6628/wlan/os/linux/gl_init.c` |
| WiFi cfg80211 | `.../gl_cfg80211.c` |
| P2P cfg80211 | `.../gl_p2p_cfg80211.c` |
| WiFi glue/kernel | `.../gl_kal.c` |
| P2P glue | `.../gl_p2p.c` |
| WEXT ioctl | `.../gl_wext.c`, `.../gl_wext_priv.c` |
| WMT core | `.../common/core/wmt_core.c` |
| STP core | `.../common/core/stp_core.c` |
| SDIO HIF | `.../common/linux/pub/hif_sdio.c` |
| OSAL | `.../common/linux/pub/osal.c` |
| WMT platform | `.../common/platform/alps/wmt_plat_alps.c` |
| CMB HW | `.../common/platform/alps/mtk_wcn_cmb_hw.c` |
| WMT thermal | `.../common/linux/pri/wmt_tm.c` |
| WMT device | `.../common/linux/pri/wmt_dev.c` |
| STP SDIO | `/home/<user>/claude/6572/wifi_recon/aquaris_bq/mediatek/kernel/drivers/combo/common/linux/stp_sdio.c` |
| STP UART | `.../common/linux/stp_uart.c` |
| 头文件 | `.../common/include/mtk_wcn_cmb_hw.h`, `.../common/include/wmt_plat.h` |
| Vendor 头 | `<mach/mtk_wcn_cmb_stub.h>`, `<mach/mt_gpio.h>`, `<mach/eint.h>` |
| ProjectConfig (mt6572) | `/home/<user>/claude/6572/wifi_recon/mt6572_kernel_drsami/mediatek/config/mt6572/ProjectConfig.mk` |
| build.prop | `/home/<user>/claude/6572/out/firmware_backup/build.prop` |
| WMT SOC config | `/home/<user>/claude/6572/out/firmware_backup/firmware/WMT_SOC.cfg` |
