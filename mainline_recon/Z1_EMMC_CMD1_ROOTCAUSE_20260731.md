# Z1 eMMC CMD1 R3=0 — 根因排查与 emmcreset 实验 (2026-07-31)

## 已锁定的证据事实(全部源码/上板实测,非猜测)

| # | 事实 | 证据 |
|---|---|---|
| 1 | MC0 引脚 mux **正确**: GPIO44/45/46/54 全为 mode 1 = MC0_CK/CMD/DAT0/RSTB | 上板 `mode44_46=0x11114444`(pin44-47=1)、`mode54=0x21111111`(pin54=1); mainline pinctrl-mtk-mt6572.h:425/434/443/514 与 dt-bindings/pinctrl/mt6572-pinfunc.h:239/244/249/288 一致 |
| 2 | eMMC 硬件复位 **已释放**: `EMMC_IOCON.BOOTRST=0` = RST_n 高 | 厂商 sd.c:2173-2190 `msdc_pin_reset()`: PULL_UP 清 0=release, PULL_DOWN 置 1=assert; 上板全程 bootrst=0 |
| 3 | **Z1 板上 vendor 内核从不写 BOOTRST** (`MSDC_EMMC_FLAG` 无 `MSDC_RST_PIN_EN`), 上电 RST 序列由 preloader/LK 完成 | board.c:463-470+523 `MSDC_EMMC_FLAG = (MSDC_SYS_SUSPEND\|MSDC_HIGHSPEED)`; sd.c:2181 `config_gpio_pin` NULL |
| 4 | 卡 **在位**: `MSDC_PS.CDSTS=1`; 复位释放; 400kHz 时钟就绪(CKSTB=1, actual=399750) | 上板 `ps=0x80fe0002` bit1=1; `cfg=0x7d9b` bit7=1 |
| 5 | **CMD 线低 + DAT0 低, DAT1-7 高**, 且 LK handoff 时刻就已如此(早于 mainline 任何写) | `ps=0x80fe0002`: bit24(CMD)=0, bit16(DAT0)=0, bit17-23(DAT1-7)=1; 与 `di1=0xbe7f8f46`(GPIO45/46 低) 交叉验证一致 |
| 6 | CMD1 R3 **从未非零**: 所有构建/所有轮次 resp0=0 | 全量 grep 历次 z1_boot_mainline_2026073*.log 无 `resp0=[1-9a-f]` |
| 7 | 命令编码正确: CMD1 rawcmd `0x02000181` = opcode1 + rsptyp3(R3) + blklen512(vendor 编码) | mtk-sd.c:1254 `(opcode&0x3f)\|((resp&7)<<7)`, 1265-1266 z1 blklen |
| 8 | 控制器/时钟/mode 语义全对: MSDC_CFG.MODE=1=SDMMC(厂商 MSDC_SDMMC=1), CKDRVEN 厂商同样不写 | mt_sd.h:59 `MSDC_SDMMC (1)`; 无 MSDC_CFG_CKDRVEN 写入 |

## 核心异常

> 卡在位、复位释放、时钟就绪、mux 正确、命令编码正确 —— 但 **CMD 线恒低、DAT0 恒低**,
> CMD0 控制器侧 CMDRDY 但卡从不退出 busy, CMD1 从无任何响应。
> 卡曾由 preloader/LK 正常读取(LK 从 eMMC 载入本内核), 说明硬件/卡本身完好。
> 结论: 卡在 Linux handoff 时处于 **stuck-busy / boot-mode 状态**, 既不是 mainline init 造成
> (LK-raw 快照早已如此), 也不是复位没释放。

## 大胆单变量实验: eMMC 硬件复位 (BOOTRST assert→release)

`msdc_z1_emmc_hw_reset()`(mtk-sd.c:3266):
```
sdr_set_bits(EMMC_IOCON, BIT0); msleep(5);   // RST_n 低, assert
sdr_clr_bits(EMMC_IOCON, BIT0); msleep(10);  // RST_n 高, release, 卡重新上电初始化
```
- 这是 mainline init 和 vendor 内核(Z1 板)都不做的一步; 是清除 boot-mode/stuck-busy 卡态的唯一手段。
- 安全: 只碰 EMMC_IOCON.BIT0 + 只读快照; 不发任何 eMMC 命令, 不写媒体/EXT_CSD, 不注册 block device。
- 前后各打 pad/reg 快照(post-emmc-hw-reset + before-CMD0), 直接观察卡是否释放 busy。

probe 里顺带修正了上轮 broken 的 skip-init_hw 编辑: 恢复 `msdc_init_hw(host); goto request_irq;`
(16:18 基准), 因 LK-raw 快照已证明 CMD/DAT0 低态早于 init_hw, "skip init_hw" 前提不成立。

## 产物

`out/boot_mainline_z1_emmcreset.img` (5,554,176 B ≤ 6MB)
- appended-DTB(zImage+dtb, magic `d00d feed` 在 _edata 边界 ✓) + `--dt` 非零 dt_size 给 LK
- cmdline: `console=tty0 console=ttyMT0,921600n8 earlycon=mtk8250,0x11005000 root=/dev/ram panic=0`
- ramdisk: `out/forensic/linux/ramdisk.gz` (MTK ROOTFS 原厂字节)
- 归档: `bridge_backups/products/products_emmcreset_20260731_225603/` (img+zImage+dtb+zImage_appended+.config+改后mtk-sd.c+SHA256SUMS)
- 备份: `bridge_backups/mtk-sd.c_pre_emmc_hw_reset_20260731_225123.bak`
- 回滚兜底: `~/claude/6572/1/boot.img` (原厂)

## 待上板

```
cd /home/<user>/claude/6572
tools/flash_with_retry.sh bootimg out/boot_mainline_z1_emmcreset.img /home/<user>/claude/6572/1/boot.img 8
./z1_capture_mainline.sh 300
```

判读: `post-emmc-hw-reset` 与 `before-CMD0` 的 ps — 若 DAT0/CMD 变高且 CMD1 resp0 非零 = 根因坐实(stuck-busy 清掉)。
若仍低 → 卡复位后仍不初始化, 转向 VEMC/VIO18 供电或 LK 跳转前 MSDC 状态深入。

## ✅ 最终完成 (2026-08-01) — eMMC 识别全链路打通，完整 CID 捕获

**关键突破：CH340 降到 115200（UART console 不再挂死）**。此前所有冻结都是 921600 下 8250 console 持续高输出 busy-wait 挂死（printk 锁全停）；115200 慢 8 倍自然节奏化，完整输出 + 无冻结。

上板实测（log: `mainline_recon/z1_boot_115200_20260801_001300.log`）：
```
[11.072] Z1 CMD0 (GO_IDLE) ok
[11.079] Z1 CMD1 i=0 arg=00000000 resp=00ff8080
[11.208] Z1 CMD1 ready OCR=c0ff8080              ← busy bit(31) 置位
[11.215] Z1 raw CID: 90014a48 34473164 04050aa2 cc45c1eb
```
CID 与 mtkclient DA 层 `3447316490014a48cc45c1eb04050aa2` 逐字节一致（CMD2 响应字序 = DA 的 [w2][w1][w4][w3]）。**eMMC = 34473164 90014a48 cc45c1eb 04050aa2**。

完整证据链（全部上板实测）：
- **根因**：LK handoff 后 eMMC 卡 stuck-busy（CMD/DAT0 恒低）→ `BOOTRST` assert 5ms + release 清除 → CMD1 恢复响应
- **命令**：CMD0 ok → CMD1 OCR=0xc0ff8080(CCS=1, 电压0xFF, busy置位) → CMD2 完整 CID
- **安全**：全程无 mmc_add_host / 无 CMD6 / 无 EXT_CSD / 无用户区写
- **console**：115200 解决 UART 挂死；诊断输出详细完整

最终产物：`out/boot_mainline_z1_emmcreset9.img`（5.55MB ≤6MB，115200 console + ignore_loglevel + 全部修复），归档 `bridge_backups/products/products_emmcreset9_20260801_001037/`。
