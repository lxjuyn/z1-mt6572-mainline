# Z1 Boot 稳定性修复实施记录 (2026-08-14)

> 三个排查 workflow（9 agent）完成，结论一致后实施修复。

## 根因结论（三 workflow 独立一致）
| 现象 | 根因 | 修复 |
|---|---|---|
| ~39.5s 重启循环 | regulator late-cleanup 关 vmc/vmch/vusb（零消费者）| ✅ always-on `37a481a21` + **本次 regulator_ignore_unused 双保险** |
| ~19.7s 间歇复位 | LK 遗留硬件 TOPRGU WDT 饿死（timeout=15s 无人喂）| **本次喂狗实验** |
| initcall "卡死" | 非卡死（10s 静默窗口）；历史 adc-battery defer 已修 `a00567cbe` | 已确认 |
| panic=0 | 真 panic 会挂死 → WDT 无标识复位 | **本次改 panic=10 + oops=panic** |

## 本次实施（产物 `boot_mainline_z1_stable.img` 6019072B ≤6MB）
1. **cmdline 强化**（诊断 + 双保险）：
   ```
   console=ttyS0,115200n8 root=/dev/ram panic=10 ignore_loglevel clk_ignore_unused \
   regulator_ignore_unused earlycon=mtk8250,0x11005000 oops=panic sysrq_always_enabled \
   panic_print=0x1f rdinit=/init
   ```
   - `regulator_ignore_unused`：内核自带，停 regulator 晚清理（39s 重启双保险，免重编内核）
   - `panic=10`：真 panic 打印 10s 再重启（不再被 WDT 无标识复位）
   - `oops=panic`：oops 转 panic（防 vcn28 反复误 disable 带伤复位）
   - `earlycon=mtk8250,0x11005000` + `sysrq_always_enabled` + `panic_print=0x1f`：抓早期/panic 输出取证
2. **喂狗 init**（验证/消除 19.7s WDT 复位）：
   - init 脚本加喂狗段：`busybox watchdog -t 5 -T 10 /dev/watchdog`（后台）
   - 若 busybox watchdog 不可用 fallback `echo V > /dev/watchdog`

## 上板验证判据
1. `dmesg | grep "feeding watchdog"` 出现 = 喂狗生效
2. `vusb/vmc/vmch: disabling` = 0 条（regulator_ignore_unused + always-on 双保险）
3. **连续 3 次 boot 均存活 >10min**（不再 ~20s/~40s 重启）
4. 真 panic 时 UART 能看到 `Kernel panic` + 10s 倒计时

## 归档
- `bridge_backups/products/products_stable_boot_20260814_223012/`（stable.img + panicdbg.img + watchdog ramdisk）
- `bridge_backups/products/boot_mainline_z1_connsys_mod_pre_panicdbg.img`（原版备份）

## 待办
- 上板验证后：若喂狗确消除 19.7s 复位，定案 WDT 根因；可评估 dts watchdog 处理或 init 常驻喂狗
- vcn28 unbalanced disables（P2）：WMT 驱动误 disable，独立排查
