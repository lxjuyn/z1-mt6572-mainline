# Z1 eMMC 剩余工作离线源码审查 (2026-08-14)

> 审查方式：纯只读源码分析，未改源码 / 未编译 / 未碰硬件。
> fork_linux HEAD = `29517eb7a`（connsys .ko rebuild）。
> 涉及文件：`drivers/mmc/host/mtk-sd.c` / `drivers/mmc/core/{mmc.c,block.c,core.c,mmc_ops.c,host.h}` / `arch/arm/boot/dts/mediatek/mt6572-z1.dts` / `.config`。
> 结论速览：**两个修复都在、未被覆盖；解码路径纯读；boot 不自动碰 boot/RPMB；大块 ~5s abort 的约束源是 DAT_TIMEOUT(HZ*5) 软件看门狗，前提是有效吞吐 ~50KB/s（400kHz/1bit）——决定性变量是上板实际 mmc0 时钟。**

---

## 1. 任务1：两个修复核对 —— 均在，未被后续提交覆盖

### 修复A（git `522c2abbe`，drop card_busy → core 走 CMD13 轮询）—— ✅ 在
- `mtk-sd.c:3048-3064` `mt_msdc_ops` **没有 `.card_busy` 成员**，且有长注释说明 Z1 理由（MSDC_PS bit16=DAT0 被控制器 BUSY_CHECK 假象压低，R1B CMD6（如块读前的 PART_CONFIG 切换）后 core 若无 card_busy 回退 CMD13 轮询，卡实际空闲 rsp 00000900=TRAN/READY_FOR_DATA）。
- `msdc_card_busy()` 函数本体仍在（`mtk-sd.c:1838-1845`），但**未挂进 ops**，成了死代码。
- git 核实：`git log -- drivers/mmc/host/mtk-sd.c` 最顶上两条就是 `b5b0a4507` 和 `522c2abbe`；之后没有任何提交再改 mtk-sd.c。`git status` 对 drivers/mmc 干净。

### 修复B（git `b5b0a4507`，PB1 外科清除 BUSY_CHECK_SEL|DDR_CMD_FIX_SEL）—— ✅ 在
- `mtk-sd.c:2184-2194` `if (host->z1_direct_cid)` 分支：`pb1_val &= ~(MSDC_PB1_BUSY_CHECK_SEL | MSDC_PB1_DDR_CMD_FIX_SEL);`，随后才 `writel(pb1_val, MSDC_PATCH_BIT1)`（2195 行）。
- 注释明确"**Do NOT write the full 0xffff0009 baseline** here -- that also clears CMDTA and breaks CMD1 during enumeration"——这是对 `d7fc80d98`（回退整写 0xffff0009 的 `7a74f3153`）教训的固化。
- 外科清除只清这两 bit，保留 CMDTA（`MSDC_PATCH_BIT1_CMDTA`）与 WRDAT_CRC（`MSDC_PB1_WRDAT_CRC_TACNTR`），符合 commit 意图。

**覆盖检查**：`b5b0a4507` 之后动过 mtk-sd.c 的提交为 0（只有 dts/.config/connsys/audio 的提交 `b8bf01d5a`、`84e1b0fb9`、`2cd361bd4`、`29517eb7a`，均未碰 mmc host 驱动）。两个修复完好。

---

## 2. 任务2：mmc_decode_cid / mmc_read_ext_csd 路径 CMD6 写检查 —— 解码纯读；boot 有标准 mainline CMD6 配置写，不写用户区

### 解码路径全部只读
| 函数 | 行为 | 写命令？ |
|---|---|---|
| `mmc_decode_cid` (`mmc.c:65`) | 纯字段解码 + `add_device_randomness` | 无 |
| `mmc_read_ext_csd` (`mmc.c:679`) → `mmc_get_ext_csd` | CMD8（`MMC_SEND_EXT_CSD`）读 EXT_CSD | 读，无写 |
| `mmc_decode_ext_csd` (`mmc.c:379`) | 纯解码；`mmc_part_add`（登记分区描述符）、`mmc_manage_enhanced_area`（`mmc.c:269`）、`mmc_manage_gp_partitions`（`mmc.c:330`）全为内存登记 | 无 |

### boot 期 CMD6 写全部来自标准 mainline `mmc_init_card`，全是 EXT_CSD 寄存器配置写
1. **ERASE_GROUP_DEF=1**（`mmc.c:1750`，rev≥3）：EXT_CSD[162] 配置写，4KB 擦除组使能。
2. **PART_CONFIG 恢复 user 区**（`mmc.c:1779-1786`，**仅当 `part_config & EXT_CSD_PART_CONFIG_ACC_MASK` 非零才发**）：EXT_CSD[179] 配置写，把卡从 boot0/boot1/RPMB/GP 拉回 user 区。**这是安全门关键保护写，不是风险**；且正是修复A（CMD13 轮询）覆盖的 R1B CMD6 场景，现在能正确完成。
3. **POWER_OFF_NOTIFICATION=POWER_ON**（`mmc.c:1792`，rev≥6）：EXT_CSD[34] 配置写。
4. **HS_TIMING=1**（`mmc.c:1085` 等）：EXT_CSD[179] 配置写。
5. **HPI_MGMT=1**（`mmc.c:1858`，卡支持 HPI 才发）：EXT_CSD[1] 配置写。
6. **CACHE_CTRL=1**（`mmc.c:1883`，`cache_size>0` 才发，超时≥800ms）：EXT_CSD[33] 配置写。

**BUS_WIDTH 切换**：Z1 dts `bus-width=<1>`，host caps 无 `MMC_CAP_4_BIT_DATA|MMC_CAP_8_BIT_DATA` → `mmc_select_bus_width`（`mmc.c:1010` 开头 `return 0`）**根本不发 CMD6 BUS_WIDTH**，保持 1-bit。

### 平台层无额外写
`arch/arm/mach-mediatek/` 与 `mtk-sd.c` 中 `grep mmc_switch|MMC_SWITCH` = 0 命中。Z1 专属 `z1_direct_cid` 路径只做：CMD1 的 vendor rawcmd 编码（`mtk-sd.c:1265`，仅在 SDC_CMD 寄存器塞 BLKLEN=512）、BOOTRST 释放、PAD_TUNE 寄存器、PB1 外科清——**无任何 EXT_CSD/用户区写**。

### 安全门结论
- **启动不发写用户数据区 / 写 boot / 写 RPMB 的命令** —— 满足安全门。
- 启动确有若干 EXT_CSD **寄存器配置** CMD6 写（mainline eMMC 标准行为，不可回避且必需：HS 切换、擦除组、缓存、电源关通知）。若把"不得发 CMD6 写"字面理解为"零 CMD6"，标准 mainline 无法满足；但按"不写用户区/不破坏卡"的意图解释，当前 boot 安全。
- 唯一会因卡初始状态变化的写是 PART_CONFIG 恢复 user 区（条件触发，作用是把卡拉回 user 区，安全方向）。

---

## 3. 任务3：mmc_block boot/RPMB 注册 —— 只注册、不访问、启动不自动碰

### 注册流程
- `mmc_blk_probe`（`block.c:3261`）→ `mmc_blk_alloc`（main 分区，part_type=0，`block.c:2657`）+ `mmc_blk_alloc_parts`（`block.c:3003`，遍历 `card->part[]`）。
- **boot0/boot1**：`mmc_decode_ext_csd` 里 `mmc_part_add(...,"boot%d", idx, true, MMC_BLK_DATA_AREA_BOOT)`（`mmc.c:463-470`），`force_ro=true` → `set_disk_ro`（`block.c:2610`）→ **只读块设备**。注册条件 `EXT_CSD_BOOT_MULT && mmc_host_can_access_boot(host)`（host.h:52，仅当 `MMC_CAP2_BOOTPART_NOACC` 时禁用；mtk-sd 未设 → 会注册）。
- **RPMB**：`mmc_blk_alloc_rpmb_part`（`block.c:2929`）创建 **char 设备** `mmcblk0rpmb`，无 backing block queue，ioctl-only（`MMC_DRV_OP_IOCTL_RPMB`）。
- 上板实测设备表吻合：`/dev/mmcblk0boot0`、`/dev/mmcblk0boot1`、`/dev/mmcblk0rpmb` 均存在（`z1_emmc_test_233012.log`）。

### 分区切换只在用户触发时发生
`mmc_blk_part_switch` 调用点：`mmc_blk_open`（`block.c:2416`）、每请求 `mmc_blk_mq_issue_rq`（`block.c:2416`，仅 `main_md->part_curr != part_type` 才发 CMD6 PART_CONFIG）、ioctl（`block.c:546`/`1137`）、reset/remove 路径。
- main 分区 `part_curr` 初始=0==part_type(0)（kzalloc）→ **启动后第一个 user 区读请求不发 CMD6 切分区**。
- 若 LK 把卡留在非 user 分区，`mmc_init_card` 的 PART_CONFIG 恢复（见任务2）会在块层起来前切回 user。

### BOOT_WP / boot 写
`EXT_CSD_BOOT_WP` 写（`block.c:1142`）只在 `MMC_DRV_OP_BOOT_WP` ioctl 触发；boot 分区 block 设备本身 `set_disk_ro`。

### 结论
**内核启动不会自动访问/切换 boot0、boot1、RPMB**；只创建设备节点。访问需用户 open（boot，只读）或 ioctl（RPMB）。`CONFIG_MMC_BLOCK=y` 下行为与上板观察一致。

---

## 4. 任务4：大块超时 ~5s abort 分析 —— 约束源是 DAT_TIMEOUT(HZ*5) 软件看门狗，前提是有效吞吐 ~50KB/s

### 完整链路
1. **单请求上限 512KB**：`mmc->max_req_size = 512*1024`、`max_blk_count=1024`（`mtk-sd.c:3758-3759`）。负对照 `dd if=/dev/mmcblk0p2 bs=512K count=1` 正好是一个满额请求。
2. **核心 data 超时是固定值，不随块数缩放**：`mmc_blk_data_prep → mmc_set_data_timeout(&brq->data, card)`（`block.c:1489`）。对 MMC 读：`data->timeout_ns = csd.taac_ns * 10`、`timeout_clks = csd.taac_clks * 10`（`core.c:659-685`，mult=10，写再乘 r2w_factor）。**与 `brq->data.blocks` 无关** —— 512KB 请求的超时和 1 个扇区一样。
3. **控制器硬件 DTOC**：host 每请求 `msdc_set_timeout(host, data->timeout_ns, data->timeout_clks)`（`mtk-sd.c:1299-1302`）→ `msdc_timeout_cal`（`mtk-sd.c:934-959`）把 ns 换算成 **2^20 sclk 周期为单位**，写 `SDC_CFG.DTOC`，**cap 255**（`mtk-sd.c:963-973`）。DTOC 至少 1 单位。
4. **软件看门狗**：`msdc_start_data` 时 `mod_delayed_work(&host->req_timeout, DAT_TIMEOUT)`（`mtk-sd.c:1318`、`1587`），`#define DAT_TIMEOUT (HZ * 5)`（`mtk-sd.c:387`）= **5.0s**。若 5s 无完成中断，`msdc_request_timeout`（`mtk-sd.c:1847-1870`）模拟 `MSDC_INT_DATTMO` → `data->error = -ETIMEDOUT` → 请求失败。

### 关键放大器：Z1 dts 锁死 1-bit 总线
`mt6572-z1.dts:340-341`：`bus-width = <1>; max-frequency = <50000000>;`
- 1-bit 数据路径 = 8-bit 方案的 1/8 吞吐。而 DTOC 的单位（2^20 周期）**不感知总线宽**，超时预算按 8-bit 隐含假设。
- 50MHz/1bit 有效吞吐 ≈ 6.25MB/s → 512KB 理论传输 ~82ms。
- **400kHz/1bit 有效吞吐 = 50KB/s**（正是 R1B 根因文档 `EMMC_R1B_BUSY_ROOTCAUSE_20260810.md` 里 "5s watchdog @ 50KB/s" 的出处）→ 256KB 需 5.12s。

### 两个 abort 候选，谁约束取决于实际时钟
| 场景 | 512KB 理论耗时 | 约束超时 | 结果 |
|---|---|---|---|
| HS 成功，时钟 50MHz/1bit | ~82ms | 硬件 DTOC：min=1 单位=**21ms@50MHz**（若 taac_ns 小）；若 taac_ns 大致 48 单位≈1s | 若 DTOC<82ms → **硬件 DATTMO，亚秒级 abort（21ms~1s）**；若 DTOC≥82ms → 正常完成 |
| HS 失败/停在 init clock，400kHz/1bit | ~10.5s（256KB≈5.12s） | 软件看门狗 **DAT_TIMEOUT=5s** 先到 | **~5s abort**（与"≥256KB 必超时"预测吻合） |

- `data->timeout_ns` 取决于卡 CSD TAAC（未在本仓取得具体 CSD 值，属上板实测项）。`mmc_compare_ext_csds`/`mmc_bus_test` 对 Z1 不触发（1-bit 无 4/8-bit 能力），不影响。
- CMD13 轮询（修复A）只用于 R1B（CMD6/erase 等）busy 轮询；**普通大块数据读（CMD18）不走 CMD13 轮询**，走的是上面第 2-4 步的数据超时链。

### 结论
- **~5s abort 预期对应 `DAT_TIMEOUT (HZ*5)` 软件看门狗**，其前提是有效吞吐 ~50KB/s（400kHz/1bit，即 HS 切换未把时钟拉上去）。
- 若 HS 成功到 50MHz，大块 abort 会更快（硬件 DTOC 21ms~1s）甚至不 abort——**决定性变量是上板实际 mmc0 时钟**，必须实测。
- 结构根因：核心 `data->timeout_ns = TAAC*10` **不随块数缩放** + Z1 **1-bit 总线**使大请求耗时放大 8 倍，两者叠加使 ≥256KB 单请求逼近/越过超时预算。

---

## 5. 汇总：已解决 / 待上板测 / 源码隐患

### ✅ 已解决（源码确认）
- 读路径 R1B busy：`.card_busy` 已摘，core 走 CMD13 轮询（`522c2abbe` 在）。
- 写路径 stall：PB1 外科清 `BUSY_CHECK_SEL|DDR_CMD_FIX_SEL`（`b5b0a4507` 在），保留 CMDTA/WRDAT_CRC。
- 安全门：解码路径纯读；boot 不写用户区/boot/RPMB；boot 只读 + RPMB 仅 ioctl。
- boot/RPMB：只注册节点，启动不自动切换访问。

### ⏳ 待上板测（源码无法定论）
1. **实际 mmc0 时钟/总线宽**：`cat /sys/kernel/debug/mmc0/ios`（`CONFIG_DEBUG_FS=y` 已开）或 dmesg 找 `mmc0: new high speed MMC card` / clock 行。**这决定大块超时机制是 DAT_TIMEOUT(5s) 还是硬件 DTOC(亚秒)**。
2. **大块超时负对照**：`dd if=/dev/mmcblk0p2 bs=512K count=1 of=/dev/null`
   - abort ~5s → 时钟停在 400kHz/1bit（50KB/s），验证 DAT_TIMEOUT 看门狗路径；
   - abort ~21ms~1s → 时钟已到 50MHz/1bit，验证硬件 DTOC 路径；
   - 成功 → 50MHz/1bit 且 DTOC 预算足够，负对照需加大 size 或用坏区制造 stall。
3. **boot0/boot1 读**：同一 CMD13 路径可测（`cat /dev/mmcblk0boot0`，只读）。
4. **RPMB 行为**：ioctl-only，无应用层工具时跳过即可。
5. **卡健康**（pre_eol_info/life_time）：需 debugfs `/sys/kernel/debug/mmc0/ext_csd` 或 mmc-utils 读 EXT_CSD[262]/[267]（离线未测）。

### ⚠️ 源码隐患/风险（供后续决策）
1. **1-bit 总线是最大结构性风险**：`bus-width=<1>` 使大块单请求耗时放大 8 倍，而 DTOC 预算不感知总线宽。若上板确认 50MHz HS 但大块仍偶发 DATTMO，可评估：
   - dts 试 `bus-width=<4>`（需确认 Z1 板级 MC0_DAT1-3 有布线，见 `mmc0_pins` pinctrl 只声明了 DAT0）；
   - 或给 mmc core/block 层加大 timeout（非标准做法，需谨慎）。
2. **HS 切换失败无显式 fallback 日志风险**：若 HS 失败时钟停在 400kHz，大块必然 5s abort，且早期 `z1_emmc_test*` 日志里就是这种"时钟上不去"的特征——**务必用 debugfs 确认 clock 再下结论**。
3. **`data->timeout_ns` 不随块数缩放**是 mmc core 通用行为（非 Z1 特有），但 Z1 的 1-bit 把它放大成了实际故障点；mmc_test 或大块 io 压测可量化。
4. **boot/RPMB 节点暴露**：`MMC_CAP2_BOOTPART_NOACC` 未设，设备节点对用户可见；boot 有 ro 保护（写被 block 层拒），RPMB 需认证 ioctl，风险低但建议测试脚本明确"勿碰"。
5. **`msdc_card_busy` 死代码**：函数保留但未挂 ops；未来若有人重新挂载 `.card_busy` 会立即回归 R1B busy 假象——建议留注释警示（已有一行注释，可加强）。

---

*审查基于 `fork_linux` HEAD `29517eb7a`，只读完成。所有行号以该提交为准。*
