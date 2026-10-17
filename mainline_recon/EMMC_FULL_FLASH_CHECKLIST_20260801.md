# eMMC 完整修复 — 明天刷写验证清单 (2026-08-01)

## 待刷镜像
- `out/boot_mainline_z1_emmc_full.img` (5,550,080 B ≤ 6MB, ANDROID! magic, dt=17380)
- cmdline: `console=tty0 console=ttyMT0,115200n8 earlycon=mtk8250,0x11005000,115200 root=/dev/ram panic=0`
- 归档: `bridge_backups/products/products_emmc_full_20260801_001947/` (含 SHA256SUMS)
- 备份: `bridge_backups/mtk-sd.c_pre_emmc_full_*`、回滚兜底 `1/boot.img`

## 本轮改动 (mtk-sd.c)
1. **eMMC HW 复位 (BOOTRST) 移入正常 probe 路径** — `msdc_init_hw` 之后、`mmc_add_host` 之前 `msdc_z1_emmc_hw_reset()` + vendor PB1=0xffff0009
2. **删掉 z1_direct_cid 门禁** — request_irq 后不再 read_cid/freeze，直接 `mmc_add_host(mmc)`；`msdc_ops_request` 命令门禁删除（放行 CMD3/7/8/6/data）
3. 诊断函数标 `__maybe_unused`（保留参考），`request_irq:` label 删除

## 刷写
```
cd /home/<user>/claude/6572
tools/flash_with_retry.sh bootimg out/boot_mainline_z1_emmc_full.img /home/<user>/claude/6572/1/boot.img 8
# 用户插 download 线 + 按开机键
```

## 抓 log (CH340 @ 115200!)
```
stty -F /dev/ttyACM0 115200 raw -echo
timeout 240 cat /dev/ttyACM0 > mainline_recon/z1_boot_emmc_full_$(date +%Y%m%d_%H%M%S).log
# 拔 download 线后开机
```
注意: CH340 用 **115200**（不是 921600）— 这是防 console 挂死的关键。

## 验证点 (按顺序)
1. **`Z1 eMMC released from stuck-busy; pb1=ffff0009`** — hw 复位在正常路径执行
2. **mmc core 枚举**: `mmc0: new high speed MMC card at address ...` / `mmcblk0: mmc0:XXXX ...` (来自 mmc core 的 mmc_add_card)
3. **`mmcblk0` 出现** — `/dev/mmcblk0` 存在 (块设备注册成功)
4. **EXT_CSD 读取正常** — `mmc0: new DDR MMC card` / `mmcblk0: ... 3728 MiB`
5. 若卡在 CMD1 轮询/枚举 — 抓 log 看卡在哪条命令，回滚 `1/boot.img`

## 回滚
```
tools/flash_with_retry.sh bootimg /home/<user>/claude/6572/1/boot.img /home/<user>/claude/6572/1/boot.img 8
```

## 期望失败模式 (已有先例)
- 若 mmc core 的 IRQ-based 命令又挂 (CMD1#3 wedge 再现) → 需要把 bounded-poll 移植进 mmc core 的 CMD 路径 (msdc_start_command)，或调 msdc_init_hw 的 reset 跳过逻辑 (line 2021/2042 `if (!host->z1_direct_cid)`)
- 若 mmcblk0 出现但读写失败 → 调 pad tune / dts bus-width

## 更新 (2026-08-01 01:15) — 最终镜像 emmc_full5

**用 `out/boot_mainline_z1_emmc_full5.img`（5,572,608 B ≤ 6MB）** 替代 emmc_full3。
新增：
- **CONFIG_MMC_BLOCK=y** → `mmcblk0` 会注册（上次 emmc_full3 卡枚举成功 `mmc0: new MMC card` 但无 mmcblk，因为 MMC_BLOCK 没开）
- **CONFIG_I2C_MT65XX / U_SERIAL_CONSOLE / KEYBOARD_MTK_PMIC / REGULATOR_MT6323 / RTC_DRV_MT6397** 全开
- **dts**：`&pmic` + `&mt6323regulator` 使能（PMIC_EINT=GPIO7），Goodix 保持 disabled（硬件未证实）
- cmdline 加 `console=ttyGS0`（U_SERIAL_CONSOLE 需 g_serial console 注册后才有输出）
- 归档 `bridge_backups/products/products_emmc_full5_20260801_011514/`

**验证点新增**：
6. `mmcblk0` 出现（块设备注册成功）
7. `mt6323-keys` / `rtc-mt6397` / `mt6323-regulator` probe 日志（PMIC 子系统）
8. 插 USB 后 /dev/ttyACMx 若有内核日志（console=ttyGS0）+ shell 提示符（getty 修复后）
