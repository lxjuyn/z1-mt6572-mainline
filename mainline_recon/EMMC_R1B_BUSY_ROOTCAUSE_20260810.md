# eMMC 块读 R1B busy 根因定案（2026-08-10 凌晨）

> 来源：workflow `wg5umsfjg`（10 agent root-cause + fix + crosscheck）
> 修复已实施：final5 = `out/boot_mainline_z1_emmc_cmd13.img`（git `522c2abbe`）

## 根因（定案）

**卡未真 busy。** CMD6(R1B) 正常完成：
- CMDRDY 触发、rsp `00000900` = CURRENT_STATE **tran** + READY_FOR_DATA
- 写入 EXT_CSD[179]=0x48 是良性配置写（PART_ACCESS=0 user + 厂商残留 bit，亚毫秒完成）
- 无 CMDTMO/CRC 错

**DAT0 持续低是 MSDC 控制器假象**：
- mt6572 `busy_check=false` → `msdc_init_hw` 置 `PB1 BUSY_CHECK_SEL(bit7)` + `DDR_CMD_FIX_SEL(bit14)`
- 该 PB1 状态使 CMD6 R1B 后 DAT0 引脚电平恒低（`MSDC_PS bit16` 直读 pad）
- core `mmc_poll_for_busy` 经 `msdc_card_busy` 无限等 busy → -110

**fork 自验铁证**：git `7a74f3153` 恢复厂商 PB1=0xffff0009（清两 bit）后误检即消失 → 假 busy 与 PB1 强相关，非卡真 busy。（该 commit 因整写 0xffff0009 顺带清零 CMDTA 致 CMD1 resp0=0 被回退，但佐证了根因方向。）

## 修复（final5 已实施）

**主修**：删 `mtk-sd.c:3048` `.card_busy = msdc_card_busy` 一行 → core 回退 **CMD13 轮询**：
- CMD13 = R1 无数据命令，`msdc_cmd_is_ready` 只查 CMDBUSY（不收 SDCBUSY 拦）
- 卡空闲回 0x900 放行，绕过 DAT0 假象

**兜底（若 CMD17 被 SDCBUSY 20ms 预检误拦）**：
- `msdc_init_hw` 里外科式 `pb1_val &= ~BIT(14)`（清 DDR_CMD_FIX_SEL，可选再清 bit7）
- 保留 CMDTA/WRDAT_CRC——**勿整写 0xffff0009**

**上板验证判据**：
1. final5 刷后 `cat /dev/mmcblk0p2` 不再 -110 → 根因解决 ✅
2. 若仍卡且是 `Controller busy detected`（SDCBUSY）→ 应用 PB1 外科清
3. 若 CMD13 轮询正常但数据读 CRC 错 → 400kHz 低吞吐/采样问题（另案）

## 数据超时边界（离线准备 workflow 确认）
- 单请求 ≤240KB 安全，≥256KB 必超时（5s watchdog @ 50KB/s）
- 明天用 `blockdev --setra` + 小块读验证
- boot0/boot1 分区读可测（同一 CMD13 路径），rpmb 可跳过

## 相关
- 修复镜像：`out/boot_mainline_z1_emmc_cmd13.img`（4870144B）
- 归档：`bridge_backups/products/products_emmc_cmd13_20260810_001443/`
- git：`522c2abbe`（已推 GitHub）
