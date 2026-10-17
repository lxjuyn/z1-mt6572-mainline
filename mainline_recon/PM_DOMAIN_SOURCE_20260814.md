# PM_DOMAIN_SOURCE — sync_state + genpd 关电是否可能触发 Z1 19.7s 重启（只读源码分析）

- 日期: 2026-08-14
- 方法: 只读源码 + 只读 boot log `mainline_recon/z1_boot_mainline_20260814_213931.log`，未改任何文件
- 结论先行: **sync_state + genpd power-off 在当前配置下不可能触发 19.7s 重启。** 该 5 条 `sync_state() pending` 是 STRICT 模式下 fw_devlink 的纯诊断 dev_info；`of_genpd_sync_state()` 根本不会执行，没有任何 power domain 被软件关电。第二段 boot 在完全相同的 19.67s 打印完全相同内容且存活 9 分钟以上，可证此机制与复位无关。

---

## 1. 用到的源码文件（绝对路径）

| 文件 | 作用 |
|---|---|
| `/home/<user>/claude/6572/fork_linux/drivers/pmdomain/mediatek/mtk-pm-domains.c` | mtk-power-controller 主驱动（probe、域注册、power_on/power_off 序列） |
| `/home/<user>/claude/6572/fork_linux/drivers/pmdomain/mediatek/mt6572-pm-domains.h` | MT6572 3 个 power domain 定义 |
| `/home/<user>/claude/6572/fork_linux/drivers/pmdomain/mediatek/mtk-pm-domains.h` | MTK_SCPD_* cap 位定义 |
| `/home/<user>/claude/6572/fork_linux/drivers/pmdomain/core.c` | genpd 核心：`genpd_power_off_unused` / `genpd_power_off` / `of_genpd_sync_state` / `stay_on` |
| `/home/<user>/claude/6572/fork_linux/drivers/base/core.c` | fw_devlink sync_state 核心：`fw_devlink_dev_sync_state` / STRICT 模式 / `device_links_supplier_sync_state_resume` |
| `/home/<user>/claude/6572/fork_linux/drivers/base/dd.c` | `deferred_probe_timeout_work_func`（19.67s 触发点） |
| `/home/<user>/claude/6572/fork_linux/include/linux/pm_domain.h` | `GENPD_FLAG_NO_STAY_ON` 等 flag 文档 |
| `/home/<user>/claude/6572/fork_linux/arch/arm/boot/dts/mediatek/mt6572.dtsi` | scpsys 节点 + DIS/MFG/CONN power-domain 子节点 + 各 consumer |
| `/home/<user>/claude/6572/fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts` | Z1 板级：connsys/connectivity/wifi 节点 |
| `/home/<user>/claude/6572/fork_linux/drivers/soc/mediatek/mtk6572-connsys.c` | fork 自带 in-tree CONNSYS 平台驱动（当前 disabled 的节点） |

---

## 2. sync_state 机制逐行分析（为什么 19.67s 打 "pending"，以及它到底关不关电）

### 2.1 结论性事实

- `.config` 中 `# CONFIG_FW_DEVLINK_SYNC_STATE_TIMEOUT is not set`（`/home/<user>/claude/6572/fork_linux/.config`）。
- 因此 `drivers/base/core.c:1681-1687` 中 `fw_devlink_sync_state` 为 **`FW_DEVLINK_SYNC_STATE_STRICT` (=0)** 默认值。
- 19.67s 的消息来自 `drivers/base/core.c:1776-1801 fw_devlink_dev_sync_state()`，在 `fw_devlink_sync_state == FW_DEVLINK_SYNC_STATE_STRICT` 分支打印 `dev_info(sup, "sync_state() pending due to %s\n", ...)` **然后直接 return 0，不做任何强制 sync_state**。
- 即：**STRICT 模式下，只要还有 consumer 未 probe，supplier 的 sync_state 回调就无限期不执行。** `of_genpd_sync_state()` 不会运行 ⇒ `stay_on` 永不清除 ⇒ **不会发生由 sync_state 触发的 genpd 关电**。

### 2.2 触发链（时间对账）

- `drivers/base/dd.c:305-321 deferred_probe_timeout_work_func()`：`deferred_probe_timeout=10`（dd.c:261 默认）→ initcall 结束后 10s 触发。
- log: `of_platform_sync_state_init` @9.485901s + 10s ≈ 19.49s，消息 @19.670879s（~0.18s workqueue 调度抖动）→ 完全吻合。
- `deferred_probe_timeout_work_func` 调 `fw_devlink_probing_done()` → `fw_devlink_dev_sync_state()` 对每个 `DL_FLAG_MANAGED` 且未 ACTIVE 且 supplier 有 sync_state 的 link 打 pending 消息。**5 个 consumer（1000e000.smi / 10203000.iommu / 13010000.gpu / 14010000.larb / 18070000.connectivity）全程无任何 probe 尝试**（log 中 `probe of 1000e000.* / 10203000.* / 13010000.* / 14010000.* / 18070000.*` 0 命中），故这 5 条 link 永不 ACTIVE → sync_state 无限期 pending。
- 另一个入口 `drivers/of/platform.c:600-605 of_platform_sync_state_init`（late_initcall_sync，log @9.485901s）调 `device_links_supplier_sync_state_resume()`（core.c:1187）只 flush `deferred_sync` 列表里**已收集**的设备；scpsys 设备不在列表里（consumer 没 bind），不会被调用。log 里该 initcall 只花 10us，正常返回。

### 2.3 为什么 19.67s 恰好是"最后一个内核输出"

- 5 条 pending 消息 @19.670879–19.721859s 是重启前最后内核输出。但**第二段 boot 在 19.670895s 起打印完全相同的 5 条消息，随后 shell 持续响应直到 capture 结束（~9 分钟 uptime）**（log 行 2821-2837）。⇒ 该消息不是复位的因，只是同刻发生的相关性。

---

## 3. genpd 关电逻辑：boot 后到底有没有域被关

### 3.1 两条关电路径，都实际不关

**路径 A：`genpd_power_off_unused`（late_initcall_sync，core.c:1372-1392）**
- log @9.449273s `PM: genpd: Disabling unused power domains`，@9.455268s initcall 返回 0，**无任何 "Failed to power off"**。
- 它对每个 genpd 排队 `genpd_power_off_work` → `genpd_power_off(genpd, false, 0)`（core.c:956-1033）。
- 首行保护（core.c:971-974）：`!genpd_status_on(genpd) || prepared_count>0 || always_on || rpm_always_on || genpd->stay_on || sd_count>0` 则直接 return。
  - **DIS / MFG**：probe 时被 `power_on()` 强制上电（见 §4），`pm_genpd_init(..., is_off=false)` ⇒ `genpd_set_stay_on(genpd, false)`（core.c:2375-2378）⇒ `stay_on = !NO_STAY_ON && !false = true`。DIS/MFG 无 `GENPD_FLAG_NO_STAY_ON`（mtk-pm-domains.c 只在 `MTK_SCPD_ALWAYS_ON` 时置 `GENPD_FLAG_ALWAYS_ON`，从不置 NO_STAY_ON）⇒ **stay_on=true 挡住，不关**。
  - **CONN**：`MTK_SCPD_KEEP_DEFAULT_OFF` ⇒ `pm_genpd_init(..., is_off=true)` ⇒ 软件 status=OFF ⇒ `genpd_status_on()` 为 false ⇒ **genpd_power_off 首行即 return，硬件根本不碰**。

**路径 B：`of_genpd_sync_state`（core.c:3502-3520）**
- 仅当 genpd provider 设备的 sync_state 被 driver core 调用才执行：`stay_on=false; genpd_power_off(...)`。
- 由 `of_genpd_add_provider_onecell`（core.c:2744）注册：`dev_set_drv_sync_state(dev, genpd_sync_state)` 设在 scpsys platform device 上（core.c:2660-2663, 2768）。
- 但在 STRICT 模式下（§2.1）该回调**永远不会被调用**（consumer 永不 bind，`fw_devlink_dev_sync_state` 只打 pending 消息就 return）。⇒ **`stay_on` 永不清除，路径 B 不执行。**

### 3.2 若真的被关会怎样（假想，非当前配置）

即使退一步假设 `of_genpd_sync_state` 执行：
- CONN 软件 status 已是 OFF，`genpd_power_off` 首行 return，**scpsys_power_off（真正 MTCMOS 关电序列，mtk-pm-domains.c:679-716：bus_protect_enable → sram_disable → ISO/CLK_DIS/RST_B/ON 清位 → 等 PWR_ACK=0）对 CONN 永远不会被调用**。
- DIS/MFG：若 stay_on 被清且 dev_list 空（5 个 consumer 全未 probe，无设备 attach 进 genpd dev_list），会走 scpsys_power_off 关掉 DIS/MFG MTCMOS。但**关 MTCMOS 只影响该域内模块（显示/GPU），不会复位 SoC**——能复位运行中 SoC 的只有看门狗。DIS/MFG 此刻本来就没有任何已 probe 的消费者，关掉属正常省电，log 里 `genpd_power_off_unused` 也无任何失败。

### 3.3 什么会真正关 CONNSYS？

- in-tree `mtk6572-connsys.c`（`drivers/soc/mediatek/`）只在 `connsys_pwr` 节点（z1.dts:113-123）`status="disabled"` 时绑定，**当前不 probe**。
- `connectivity@18070000`（z1.dts:175-195）绑的是 out-of-tree .ko（mtk_stp_wmt_soc.ko / mtk_wmt_wifi_soc.ko），当前未加载、0 probe。
- 即 **CONN 域的上下电完全由 LK 遗留状态 + out-of-tree 模块控制，in-tree genpd 不参与**。CONN 域上电只会发生在 out-of-tree 模块加载后 `dev_pm_domain_attach` + `pm_runtime_get_sync` 时；即使那样，CONNSYS 的软件复位也是通过 `watchdog` reset 控制器（z1.dts:189 `resets = <&watchdog MT6572_TOPRGU_CONNSYS_SW_RST>`），不是 genpd 关电。

---

## 4. MT6572 三个 power domain 的硬件对应（mt6572-pm-domains.h）

| 域 (id) | 硬件 | 定义要点 |
|---|---|---|
| `MT6572_POWER_DOMAIN_DIS` (1) | **显示子系统**：MMSYS/OVL/COLOR/SMI-common/larb0/M4U IOMMU | `.ctl_offs=SPM_DIS_PWR_CON`，`sram_pdn_bits=GENMASK(11,8)`，bus-protect `TOP_AXI_PROT_EN_DIS`。consumer：smi@1000e000、iommu@10203000、larb@14010000（mt6572.dtsi:355/407/574） |
| `MT6572_POWER_DOMAIN_MFG` (0) | **GPU Mali-400 + mfgcfg** | `.ctl_offs=SPM_MFG_PWR_CON`，`sram_pdn_bits=BIT(8)`。consumer：mfgcfg@13000000、gpu@13010000（mt6572.dtsi:539/556） |
| `MT6572_POWER_DOMAIN_CONN` (3) | **CONNSYS（WiFi+BT 一体化）** | `.ctl_offs=SPM_CONN_PWR_CON`，`sram_pdn_bits=0`，bus-protect `TOP_AXI_PROT_EN_CONN_M|CONN_S`，**`.caps = MTK_SCPD_KEEP_DEFAULT_OFF`**（唯一一个）。consumer：connectivity@18070000 + connsys_pwr@18070000 |

3 个域都无 `MTK_SCPD_ALWAYS_ON`。DIS/MFG 在 probe 时被 `power_on()`（非 KEEP_DEFAULT_OFF 分支，mtk-pm-domains.c:864-877）强制上电并 `stay_on=true`；CONN 走 KEEP_DEFAULT_OFF 分支只 `dev_warn("A default off power domain has been ON")`（若 LK 遗留硬件 ON），**不调用 power_on 也不调用 power_off**，软件状态直接标 OFF。

> 注意 `mtk-scpsys.c`（旧驱动）只匹配 mt2701/2712/6797/7622/7623a/8173，**不匹配 mt6572**；MT6572 走 `mtk-pm-domains.c` 的 `mediatek,mt6572-power-controller`（mtk-pm-domains.c:1120-1124）。

---

## 5. 关键判断：sync_state pending + genpd 关电 ≠ 19.7s 重启根因

### 5.1 逻辑上不可能

1. STRICT 模式下 `of_genpd_sync_state()` 永不执行 → `stay_on` 永不清除 → **没有任何 genpd 被 sync_state 关掉**。
2. `genpd_power_off_unused`（唯一会跑的关电路径）被 `stay_on`（DIS/MFG）和软件已 OFF（CONN）双重挡住，log 无任何关电失败。
3. CONN（唯一和"硬件复位"沾边的域）**in-tree 软件从不发出 power-off**（KEEP_DEFAULT_OFF + 软件 status 本就 OFF），其真正上下电由 out-of-tree 模块决定；即使模块加载，复位 CONNSYS 走的是 TOPRGU SW_RST，不是 genpd 关电。
4. 即便假设某个域真被关电，MTCMOS power-off 也不会复位运行中的 CPU——只有硬件 WDT 能在内核运行期复位 SoC。

### 5.2 实证上不可能

- boot1（行 1414-1418）与 boot2（行 2821-2825）在**完全相同时间戳**（19.670879 vs 19.670895…）打印完全相同的 5 条 pending 消息。
- boot1 之后复位；**boot2 之后存活 9 分钟**（shell 持续 echo 应答直到 capture 结束）。
- 同一内核代码、同一时序、同一消息，一次复位一次不复位 ⇒ 触发点在两次 boot 之间变化的**硬件状态**，不在 sync_state/genpd 这条代码路径。

### 5.3 与既有 REBOOT_19S_SCENE 结论一致

本分析为 `REBOOT_19S_SCENE_20260814.md` 的"sync_state 良性"判断提供了源码级背书，并进一步排除"关 CONNSYS 域触发 preloader 异常"的可能（in-tree genpd 根本不碰 CONN 电源）。真正的 19.7s 复位最可能仍是 **LK 武装的硬件 TOPRGU WDT 在首段冷启动 ~20s 触发**（mtk-wdt 两段 boot 均 @7.13s `Watchdog enabled (timeout=15 sec, nowayout=0)`；`CONFIG_WATCHDOG_PRETIMEOUT_GOV is not set` 使 wdt_bark ISR 空转不喂狗），第二段 boot 因 WDT_MODE_EN 硬件状态不同而存活——该假设 log 无法最终证实，需读 TOPRGU WDT_RST_STAT / 拔电池复测 / 打开 /dev/watchdog 喂狗验证。

---

## 6. 附：建议的验证/消噪（仅建议，未执行）

1. **喂狗区分 WDT**：initramfs 里 `echo V > /dev/watchdog`（打开后内核自动喂），看 19.7s 复位是否消失——直接区分"WDT 饿死复位" vs "其他硬件复位"。
2. **读复位原因**：LK 或 mtk-wdt probe 时打印 TOPRGU `WDT_RST_STAT`。
3. **消 sync_state 噪音（可选，非根因）**：5 个无驱动 consumer（smi/iommu/gpu/larb/connsys）使 scpsys sync_state 无限期 pending。可在 dts 给这些节点加 `status="disabled"`，或 cmdline 加 `fw_devlink.sync_state=timeout`，消掉 19.67s 噪音便于看清真正的复位点。注意：若后续真的要给 CONN 域接消费者（WiFi/BT），`KEEP_DEFAULT_OFF` + STRICT pending 的组合会导致 CONN 域**永不自动上电**，届时需 out-of-tree 模块显式 `pm_runtime_get_sync`（当前 `mtk6572-connsys.c` 就是这么做的，见其 probe 中 `dev_pm_domain_attach(PD_FLAG_ATTACH_POWER_ON)` + `pm_runtime_get_sync`）。

---
(本报告只读分析了源码与 `z1_boot_mainline_20260814_213931.log`，未改任何文件。)
