# S6 MCU并口屏点亮调研 (8路联网+本地验证) 2026-08-31 01:07


======================================================================
## TOPIC: disp-regs
VERDICT: MT6572 DISP 基址=0x14000000(mmsys)，MCU/8080 并口 DBI 控制器独立块基址=0x14012000；写命令写 0x14012F00(PCMD0)、写数据写 0x14012F10(+0x10=A0/DC 高)，触发帧传输写 0x1401200C=(1<<15)，并口时序在 0x14012030(PCNF0)，串行配置 DBI_SCNF=0x14012028。已用真·MT6572 LK 源码+内核驱动+独立博客三方交叉验证。mainline 无 DBI 8080 驱动，S6 点屏必须走 vendor 移植（可仿 BSP 的 hx8369_mcu_6572 模板；gc9307/nv3029s/st7789v 驱动未找到）。

# MT6572 DISP / DBI (MCU 8080 并口) 寄存器调研 — S6 点屏

结论先行: DISP 块基址确实是 0x14000000(mmsys), DBI/MCU 并口控制器独立块基址 = **0x14012000**(LK 里叫 LCD_BASE/DISPSYS_DBI_BASE)。写命令/写数据不是靠某个"RD/WR 位", 而是靠 **A0/DC/RS 偏移 +0x0(命令)/+0x10(数据)** 写到 PCMD0 窗口寄存器。已从真实 MT6572 LK 源码 + 内核驱动 + 独立博客三方交叉验证。

## 来源(3+ 独立角度交叉)
1. **真·MT6572 LK 源码** = GitHub `l33tnoob/MT65x2_kernel_lk` (搜索命中显示为 ariafan/MT65x2_kernel_lk 的镜像, 实际仓库为 l33tnoob)。已 `git sparse-checkout` 拉到本地 `/tmp/mt65x2_lk`。关键文件:
   - `mediatek/platform/mt6572/lk/include/platform/mt_reg_base.h` (全部基址)
   - `mediatek/platform/mt6572/lk/include/platform/lcd_reg.h` (DBI 寄存器 struct 布局+offsetof 断言)
   - `mediatek/platform/mt6572/lk/lcd_drv.c` (写命令/数据/触发)
   - `mediatek/platform/mt6572/lk/disp_drv_dbi.c`, `disp_drv.c`, `platform.c`, `ddp_path.c`
   - 内核侧 `mediatek/platform/mt6572/kernel/drivers/video/lcd_reg.h` + `lcd_drv.c` (同 struct)
   - 例程 `mediatek/custom/common/lk/lcm/hx8369_mcu_6572/hx8369_mcu_6572.c` (MCU 并口屏参数模板)
2. **博客/文档**: cnblogs 《MTK LCM驱动分析》 https://www.cnblogs.com/wen123456/p/14036254.html — 明确指出 "MT6572 串行 DBI 配置寄存器 = 0x14012028 (DBI_SCNF)", 与源码 base+0x28 完全吻合; 并给出并口引脚名 (LPCE0B=CS, LPA0=RS, LPWRB=WR, LPRDB=RD)。
3. **本地 mainline 树** `/home/<user>/claude/6572/fork_linux/...` 交叉核对时钟门控与 dts 基址。

## 1) DISP 基址块全表 (mt_reg_base.h, 与本地 mt6572.dtsi 一致)
- MMSYS_CONFIG_BASE(mmsys/clock-gate/syscon) = **0x14000000**
- MDP_RDMA 0x14001000 | RSZ0 0x14002000 | RSZ1 0x14003000 | MDP_WDMA 0x14004000 | WROT 0x14005000 | TDSHP 0x14006000
- DISP_OVL **0x14007000** | DISP_RDMA **0x14008000** | WDMA 0x14009000 | BLS(背光PWM) 0x1400A000 | COLOR 0x1400B000
- DSI 0x1400C000 | DPI 0x1400D000 | MUTEX 0x1400E000 | CMDQ 0x1400F000
- SMI_LARB0 0x14010000 | SMI_COMMON 0x14011000
- **DISP_DBI_BASE = 0x14012000** (= LCD_BASE = DISPSYS_DBI_BASE, MCU/8080 并口控制器)
- CAM 0x14013000 | SENINF 0x14014000 | MIPI_RX 0x14015000

## 2) DBI 块寄存器偏移 (base 0x14012000, 来自 lcd_reg.h LCD_REGS struct + offsetof 静态断言)
- 0x000 STATUS / 0x004 INT_ENABLE / 0x008 INT_STATUS
- 0x00C START —— **写 (1<<15) 触发一次帧/传输** (LCD_StartTransfer)
- 0x010 RESET
- 0x01C/0x020 SIF_TIMING[0/1] (串行 WR_1ST/WR_2ND/RD_1ST/RD_2ND/CSH/CSS)
- 0x028 SERIAL_CFG = **DBI_SCNF** (= 博客说的 0x14012028, 交叉验证吻合)
- 0x02C SIF_CS
- 0x030/0x034/0x038 PARALLEL_CFG[0/1/2] = **PCNF** (并口时序): 位域 WST[5:0](写等待), C2WS[13:10](CS→写setup), C2WH[17:14], RLT[23:18](读延迟), C2RS[29:26], C2RH[31:28]
- 0x03C PARALLEL_DW (并口数据宽度/CS高宽)
- 0x040 TEARING_CFG | 0x044 CALC_HTT | 0x048 SYNC_LCM_SIZE | 0x04C SYNC_CNT | 0x050 SMI_CON
- 0x060 **WROI_CONTROL** (IF_FMT/DATA_FMT/COMMAND 计数/ENC)
- 0x064 WROI_CMD_ADDR | 0x068 WROI_DATA_ADDR | 0x06C WROI_SIZE
- 0x070 SRC_CON | 0x074 SRC_ADD | 0x078 SRC_PITCH
- 0x090 ULTRA_CON | 0x094 CONSUME_RATE | 0x098 DBI_ULTRA_TH | 0x09C GMC_ULTRA_TH
- 0xC00..0xC7C **CMDQ[0..31]** (32 条命令队列, WROI_CON.ENC 使能)
- **0xF00 PCMD0** (并口 port0), 0xF20 PCMD1 (port1), 0xF40 PCMD2 (port2)
- 0xF80 SCMD0 (串口令 port0), 0xFA0 SCMD1 (串口令 port1)
- 结构体总长 0xFC0 (sizeof(LCD_REGS) 断言)

## 3) 写命令 / 写数据 的实现 (lcd_drv.c + disp_drv.c) — RD/WR/DC 区分方式
- 常量: `LCD_A0_LOW_OFFSET=0x0` (命令, RS/DC 拉低), `LCD_A0_HIGH_OFFSET=0x10` (数据, RS/DC 拉高)。
- `lcm_send_cmd(cmd)` → `LCD_WriteIF(ctrl_if, LCD_IF_A0_LOW, cmd, cpu_write_bits)`
- `lcm_send_data(data)` → `LCD_WriteIF(ctrl_if, LCD_IF_A0_HIGH, data, cpu_write_bits)`
- `LCD_WriteIF` 按接口选基址: 并口0→`&LCD_REG->PCMD0`, 并口1→PCMD1, 并口2→PCMD2, 串口令0→SCMD0, 串口令1→SCMD1; 若 a0=A0_HIGH 则基址 +0x10; 再按 `cpu_write_bits` 用 8/16/32 位写。
- 因此 **并口 port0**: 写命令 = 0x14012F00, 写数据 = 0x14012F10。写宽度由 `LCM_DBI_CPU_WRITE_8/16/32_BITS` 决定 (8080 屏常用 16-bit RGB565 → 写 16 位)。
- 8080 的 WR/RD 不是寄存器位, 是硬件引脚: 写=LPWRB, 读=LPRDB, 命令/数据选择= LPA0(RS/DC), 片选= LPCE0B(CS), 时钟= LCDCLK, 数据= DBI[17:0](与 DPI data 复用)。读由 `LCD_ReadIF` (INREG 同一窗口) 实现。
- 帧传输: `LCD_StartTransfer` 清 INT_STATUS→写 START=(1<<15)。

## 4) 时钟门控 (访问 DBI 前必开)
- MMSYS CG (base 0x14000000): CON0=+0x100, SET0=+0x104, CLR0=+0x108, CON1=+0x110, SET1=+0x114, CLR1=+0x118。**与本地 mainline `clk-mt6572-mm.c` 的 mm0(set0x104/clr0x108/sta0x100)、mm1(set0x114/clr0x118/sta0x110) 完全一致。**
- LK `LCD_PowerOn`: `MASKREG32(0x10000080, 0x000F0020, 0x000F0020)` + 写 CG_CLR1(0x14000118) 0x70; 另有 TOPCKGEN swcg `CG_CTRL0`(0x10000080) 的 DBI_BCLK_SW_CG_BIT=BIT(5), DBI_PAD0..3=BIT(16..19)。
- `ddp_path.c` 用 `DISP_REG_CONFIG_CG_CLR0`(0x14000108) 解 OVL/RDMA 等门控。

## 5) mainline 现状 (决定移植路线)
- 本地 `fork_linux/drivers/gpu/drm/mediatek/` 对 mt6572 只支持: mmsys(mtk_drm_drv)/mutex/OVL/RDMA/DSI。**没有 DBI/MCU/8080 并口控制器驱动**。
- mainline `mipi_dbi`(tinydrm/panel-mipi-dbi) 只实现 **SPI** 后端, 无 8080 并口后端, 也无 MTK DBI 控制器。
- ⇒ S6 这块 MCU 并口屏在 mainline 上**必须走 vendor 移植**: 用 0x14012000 这组寄存器直接写命令/数据/触发, 或移植 vendor `lcd_drv.c` + lcm 驱动。这与工作区既有结论 (S6_FTS_LCM_ROUTE) 一致。

## 6) 候选面板驱动在 BSP 里的情况
- 搜遍 `mediatek/custom/common/lk/lcm/` 与 `kernel/lcm/`: **没有** gc9307_mcu_6572 / nv3029s_mcu / st7789v_mcu 这三个 IC 的驱动 (未找到)。
- 但有一个可用的 **MCU 并口参考驱动**: `hx8369_mcu_6572/hx8369_mcu_6572.c`, 其 `lcm_get_params()` 即移植模板: `type=LCM_TYPE_DBI; ctrl=LCM_CTRL_PARALLEL_DBI; dbi.port=0; clock_freq=LCM_DBI_CLOCK_FREQ_104M; data_width=18BITS; data_format=RGB666/RGB565; cpu_write_bits; parallel.write_setup=2/write_hold=2/write_wait=10/read_setup=2/read_hold=2/read_latency=10/wait_period=0`。移植 GC9307/NV3029S/ST7789V 时照此填参 + 塞各自 init 序列即可。
- 同目录另有 `nt35510_mcu_6572` 可作第二参考。

## 关键数字速查
- DBI 块基址: 0x14012000
- 写命令窗口(并口0): 0x14012F00 | 写数据窗口: 0x14012F10 (+0x10=A0/DC 高)
- 触发传输: 0x1401200C = (1<<15)
- 并口时序: 0x14012030 (PCNF0), 位域 WST/C2WS/C2WH/RLT/C2RS/C2RH
- 串行配置 SCNF: 0x14012028 (博客独立印证)
- 命令队列: 0x14012C00 起 32×4
- mmsys 时钟门控: 0x14000100/104/108 (CON0/SET0/CLR0), 0x14000110/114/118 (CON1/SET1/CLR1)
- 8080 控制引脚: CS=LPCE0B, RS/DC=LPA0, WR=LPWRB, RD=LPRDB, CLK=LCDCLK, DATA=DBI[17:0]

======================================================================
## TOPIC: gc9307-datasheet
VERDICT: GC9307 确认支持 8080 并口 (240RGBx320/262K/IM 引脚选 8/9/16/18-bit); 0xFE+0xEF 是 "Inter Register Enable1/2" 解锁对, 必须先写才能用 Level-3 电源/伽马寄存器 (C1/C3/C9/EC/A7/E8/F0-F3), 只有复位能清; 厂商 0x36=0x48 意为 MX+ML+BGR (列反向+底到顶扫描+BGR 色彩顺序), 0x3A=0x05 意为 DBI=16bpp RGB565 (DPI 字段为 Reserved 无关); 0x2C 是 Memory Write (RAMWR, 指针归窗起点后纯数据流自增, 任何新命令终止写), 0x3C 才是 Write Memory Continue — 实践上每帧发一次 2A/2B/2C 后全帧数据走数据总线即可。

## 1) GC9307 规格与接口 — 确认支持 8080 并口

**来源(交叉验证 3 处)**:
- GC9307 官方 datasheet (GalaxyCore, GC9307_DS_V1.01, 2018/04/24, 190页): https://www.displaysino.com/upload/default/20220916/d47ad3e1c21b78a4b9c1b83613e3ae2d.pdf (页面: https://www.displaysino.com/icDetails/GC9307.html ; 镜像: https://datasheet4u.com/datasheets/GALAXYCORE/GC9307/1577276 )
- Trezor 开源驱动(实际走 8080 式并行总线/STM32 FMC): https://github.com/trezor/trezor-core/blob/master/embed/extmod/modtrezorui/display-stm32_t.h
- Go 驱动寄存器定义: https://pkg.go.dev/github.com/photonicat/periph.io-gc9307

**关键数字**:
- 分辨率 **240RGB x 320**, 262K 色 (18bpp GRAM, 172,800 字节), 720 源极 + 320 栅极驱动, IOVCC 1.65~3.3V, VCI 2.5~3.3V
- **支持 8080 并口**: "8-/9-/16-/18-bit data bus MCU interface, 8080-I / 8080-II series", 另有 6/16/18-bit RGB 接口 + 3/4线 SPI + 2-lane SPI。datasheet §4.1.1 IM[3:0] 引脚表: `IM=0000` → 8080 8-bit bus I (D[7:0], 引脚 D[7:0]+WRX+RDX+CSX+D/CX); `IM=0001` → 8080 16-bit bus I (D[15:0])。Z1/S6 这种 LK MCU 并口一般是 16-bit IM=0001
- **D/CX 引脚** = 命令/数据选择 (D/CX=1 写数据, =0 写命令); WRX 上升沿写入; RDX 读; 不用时固定接对应电平
- **RDDID (0x04) 读回 = 0x009307** (datasheet §6.2.1, Trezor 也用此判型)。若 8080 总线能读 (RDX 可用), 读 0x04 三字节可区分三个候选 IC。旁证: displaysino 网站把 NV3029S 标为 "Compatible ST7789V", 与你们的候选清单一致
- 时序/上电可操作值: 硬件复位 RESX 低 ≥10us, 释放后等 ≤120ms 再发命令 (Trezor 实测等 120ms 稳定); Sleep Out (0x11) 后必须等 **5ms** 再发下一条命令; Sleep In (0x10) 后要等 120ms 才能 Sleep Out; SLPIN 到真正进 sleep 需 120ms

## 2) 0xFE / 0xEF = Inter Register Enable1/2 (扩展寄存器前置解锁)

**确认**: datasheet §6.4.9 "Inter Register Enable1 (FEh)" 和 §6.4.10 "Inter Register Enable2 (EFh)"。原文: "To set Inter_command high, you should write Inter register enable 1 (FEh) and Inter register enable 2 (EFh) **continuously**. Once Inter_command is set high, only hardware or software reset can turn it to low."

即: 先写 0xFE 再写 0xEF (顺序、连续), 解锁后 "Inter_command" 标志拉高, 只有硬件/软件复位能清除。**所有 Level-3 命令被此标志门控** (每个命令的 Restriction 写着 "Inter_command should be set high to enable this command"), 包括:
- E8h Frame Rate (帧率公式: 47.62KHz/(136*(RTN1+4)+RTN2))
- C1h Power Control 1, C3h Power Control 2, C9h Power Control 4, ECh Power Control 6, A7h Power Control 7
- F0h/F1h/F2h/F3h SET_GAMMA1~4

**交叉验证**: Trezor 驱动里注释一字不差: `CMD(0xFE); // Inter Register Enable1` `CMD(0xEF); // Inter Register Enable2`, 之后才写 0x35/0x3A/C3/C4/C9/.../F0~F3。CSDN 中文文章也写 `lcd_write_cmd(0xFE); // 进入...` (进入扩展寄存器)。你们从 lk.bin 提取的序列里 0xFE/0xEF 出现在前段, 作用就是解锁后续电源/伽马寄存器。**结论: 移植到 mainline 时必须保留这对解锁命令在最前面, 且任何软复位后要重新解锁。**

## 3) 0x36=0x48 和 0x3A=0x05 解码

**0x36 MADCTL (Memory Access Control), datasheet §6.2.18**, 位定义 (与通用 MIPI-DCS 一致): D7=MY(行地址顺序), D6=MX(列地址顺序), D5=MV(行列交换), D4=ML(垂直刷新顺序), D3=BGR(RGB/BGR 色彩顺序), D2=MH(水平刷新顺序), 默认 0x00。

**0x36 = 0x48 = 0b01001000 → MX=1, ML=1, BGR=1, 其余 0**:
- MX=1: 列地址反向 (右→左写)
- ML=1: 垂直刷新底→顶 (行扫描反向)
- BGR=1: **BGR 面板色彩顺序** (红蓝互换, 重要! 你们帧缓冲按 BGR 顺序送)
- MV=0: 无行列交换 → 保持 240x320 竖屏方向
即厂商给这块屏选的是"列反+垂直反扫+BGR"的组合, 是面板物理走线补偿, 不要随意改; 颜色偏蓝/偏红时先翻 BGR 位。

**0x3A COLMOD (Pixel Format Set), datasheet §6.2.22**, 参数 = `0 | DPI[2:0] | 0 | DBI[2:0]`, 默认 0x66 (DPI=DBI=110=18bpp):
- **0x3A = 0x05 = 0b00000101 → DPI[2:0]=000(Reserved, 8080 MCU 接口下无关), DBI[2:0]=101 = 16 bits/pixel = RGB565**
- 对照: 101=16bpp, 110=18bpp; Trezor 用 0x55 (DPI=DBI=101, 同样 16bpp RGB565)
**结论: 该屏在 8080 接口上配置为 16bpp RGB565。** 8-bit 数据总线时每像素 2 拍 (高8后低8), 16-bit 总线每像素 1 拍。

## 4) 0x2C = Memory Write (RAMWR), 严格说不是 "Continue"

datasheet §6.2.13 "Memory Write (2Ch)": "used to transfer data from MCU to frame memory... the column register and the page register are reset to the Start Column/Start Page positions... D[17:0] is stored in frame memory and the column/page register incremented. **Sending any other command can stop frame Write**." Restriction: "In all color modes, there is no restriction on length of parameters."

- 0x2C = **write_memory_start**: 写指针重置到 CASET(2Ah)/PASET(2Bh) 窗口起点, 然后纯数据流自增写入。发任何其他命令即终止像素写
- **0x3C 才是 Write Memory Continue** (§6.2.23): 从上一次 2Ch/3Ch 停下的位置继续写, 不重置指针; 超出窗口 (EC-SC+1)*(EP-SP+1) 的多余数据被忽略
- 实际含义与你们理解一致: 每帧流程 = 0x2A 设列窗(0~239) + 0x2B 设行窗(0~319) + 0x2C, 之后**全部帧数据走纯数据写 (D/CX=1), 不再带命令字节**; 多数驱动每帧重发 0x2C 把指针归位。对 mainline 移植: MIPI-DBI 兼容驱动 (如 drivers/gpu/drm/tiny/ 路线或自己写 8080 GPIO/DBI 驱动) 按 "CASET+PASET+RAMWR 后连续写" 实现即可

## 额外可操作参考 (Trezor 的 GC9307 完整 init, 走并行总线, 可直接对照你们提取的序列)
`0xFE; 0xEF; 0x35,0x00 (TEON); 0x3A,0x55 (16bpp); 0xC3,0x27; 0xC4,0x18; 0xC9,0x1F; 0xC5,0x0F; 0xC6,0x00; 0xC7,0x10; 0xC8,0x01; 0xFF,0x62; 0x99,0x3E; 0x9D,0x4B; 0x8E,0x0F; gamma: 0xF0 {8F 1B 05 06 07 42}; 0xF2 {5C 1F 12 10 07 43}; 0xF1 {59 CF CF 35 37 8F}; 0xF3 {58 CF CF 35 37 8F}` 然后 clear + 0x11 SLPOUT(等5ms) + 0x29 DISPON。

## 参考链接汇总
- GC9307 datasheet V1.01 PDF: https://www.displaysino.com/upload/default/20220916/d47ad3e1c21b78a4b9c1b83613e3ae2d.pdf
- displaysino IC 页: https://www.displaysino.com/icDetails/GC9307.html
- datasheet4u 镜像: https://datasheet4u.com/datasheets/GALAXYCORE/GC9307/1577276
- Trezor 驱动 (0xFE/0xEF 注释 + 8080 并行实现): https://github.com/trezor/trezor-core/blob/master/embed/extmod/modtrezorui/display-stm32_t.h
- periph.io GC9307 Go 驱动寄存器表 (MADCTL 位): https://pkg.go.dev/github.com/photonicat/periph.io-gc9307
- fbcp-ili9341 issue #189 (GC9307 讨论): https://github.com/juj/fbcp-ili9341/issues/189

======================================================================
## TOPIC: lcm-select
VERDICT: MTK ALPS LK 对多候选面板是"按 lcm_driver_list 编译顺序遍历 + 调用各驱动 compare_id() 经 DBI 读回面板 ID 判定"（lcm_name 在 LK 恒为 NULL；kernel 只用 LK 传来的 lcm= cmdline 名字 strcmp，不再硬件探测；全不匹配则 fallback 到列表最后一个）。S6 原厂 lk.bin/vmlinux 实锤三个驱动名齐全且含 compare_id 读 0x04 打印 4 字节的调试串。三款候选共用一条读 ID 命令 0x04（RDID，dummy+ID1+ID2+ID3，Sleep In 亦可读）：GC9307=00/93/07，ST7789V=85/85/52，NV3029S 的 RDDID 默认值未找到公开数据手册（只有 NV3029 家族数据手册存在，需上板实测）。因此可以一次 0x04 读回（4 字节）直接判别板上实装，0x009307→GC9307、0x858552→ST7789V、其余值→按 0xDA/DB/DC 补读并记录为 NV3029S。

## 一、MTK ALPS LK 多候选 LCM 选择机制（核心结论，有 MT6572 KK 真实源码为证）

源码：Intelinsidecom/kernel_ckt72_w_kk-mt6572（MT6572 KK1.0 内核，即 S6 同款平台）
- LK: mediatek/platform/mt6572/lk/disp_drv.c（`disp_drv_get_lcm_driver()`，行 285-423；`DISP_DetectDevice()` 行 592；`disp_get_lcm_name_boot()` 行 425）
- Kernel: mediatek/platform/mt6572/kernel/drivers/video/disp_hal.c（`disphal_get_lcm_driver()`，行 678+）
（https://github.com/Intelinsidecom/kernel_ckt72_w_kk-mt6572）

1. **lcm_count==1**：直接取 `lcm_driver_list[0]`，不做任何 ID 校验（注意：`compare_id` 校验代码在该函数里被整段注释掉，`isLCMFound=TRUE` 直接成立）。
2. **lcm_count>1 且 lcm_name==NULL（LK 开机路径，`DISP_DetectDevice` 传 NULL）**：for 循环按 `lcm_driver_list[]` **编译顺序**逐个试：
   - `lcm->set_util_funcs` + `get_params` + `disp_drv_init_ctrl_if`；
   - **若 `lcm->compare_id != NULL` 且 `lcm->compare_id()` 返回 TRUE → 命中，停止**；
   - **若 `compare_id == NULL` 或返回 FALSE → 继续，且 `lcm_drv = lcm`（fallback）**；
   - 三个都不匹配时，最终 `lcm_drv` 停在**列表最后一个**（即 st7789v_mcu_6572），不会 NULL panic。
   → **结论：机制是"按编译顺序遍历 + 每个驱动的 compare_id() 读回 ID 判定"，不是纯按顺序取第一个。没有 compare_id 的驱动等于永不命中。**
3. **Kernel 阶段不重复硬件探测**：LK 把选中名字写进 cmdline `lcm=1-<name>`（格式经 arzam16/empty-sixty-five 确认：`lcm=1-lg4573b` 等，去掉前缀"1-"即驱动名）；kernel 侧 `disphal_get_lcm_driver` 收到非空 `lcm_name` 时只做 `strcmp` 匹配，不再读面板。
4. **读 ID 的总线原语（MT6572 LK 已内置）**：`lcm_utils.read_data = lcm_read_data` → `LCD_ReadIF(ctrl_if, LCD_IF_A0_HIGH, &data, dbi.cpu_write_bits)`（disp_drv.c 行 127-141；lcd_drv.c 行 1436 `_LCD_ReadIF`，走 DISP_DBI_BASE 的 DBI 并行/串行控制器，readSetup/readHold/readLatency 即 lk.bin 里那三条 `readSetup <= 16U` 断言）。MCU 8080 并口读就是这条路。

## 二、S6 原厂固件的本地实锤（/home/<user>/claude/6572/mt6572-2/）

- `lk.bin` 和 `vmlinux_s6.bin` 都含三个驱动名：`gc9307_mcu_6572_s6_24_hsd`、`nv3029s_mcu_6572`、`st7789v_mcu_6572`，且含 LK 框架串 `[LCM Auto Detect], we have %d lcm drivers built in`、`[LCM Auto Detect] [%d] - [%s]`、`[LCM Specified]`、`%s lcm=%1d-%s`、`platform/mediatek/mt6572/lk/disp_drv.c`、`disp_drv_dbi.c`、`DISP_DetectDevice`。
- 关键证据：`-gc9360 buffer = 0x%x,buffer[1]=0x%x,buffer[2]=0x%x,buffer[3]=0x%x id=0x%x`（lk.bin 偏移 0x373b7、vmlinux 同款）——这是 `lcm_compare_id()` 里 `read_reg_v2(0x04, buffer, 4)` 后打印 4 字节（1 dummy + 3 ID）的调试串。**即厂商 compare_id 就是用 0x04 一次读回 4 字节判 ID**，且同时编进了 LK 和 kernel。

## 三、三款候选 IC 的 ID 读取命令（交叉验证）

三者都遵循 MIPI DBI Type C（ILI9341 族）命令集，**统一可用一条命令**：

- **0x04 (RDDID)**：读回 24 bit = dummy 1 字节 + ID1 + ID2 + ID3。所有 3 款都支持（GC9307 数据手册 6.2.1；ST7789V 数据手册 9.1.3，"Sleep In 状态下也可读"）。
- 逐字节备选：**0xDA/0xDB/0xDC (RDID1/2/3)** 分别返回 ID1/ID2/ID3（zh-jieli GC9307 SDK 驱动即 `lcd_read_cmd(0xda/0xdb/0xdc)`；https://gitlab.zh-jieli.com/e_badge/e_badge_707_sdk_200/-/blob/main/SDK/cpu/br35/ui_driver/lcd_drive/lcd_spi/lcd_spi_gc9307_RQ_BOE_172x320.c）。

期望返回值：
| IC | 0x04 读回(dummy,ID1,ID2,ID3) | 来源 |
|---|---|---|
| GC9307 | **0x00, 0x93, 0x07**（24'h009307，power-on/SW/HW reset 默认值均同；RDID1/2/3=00/93/07） | GC9307_DS_V1.01 数据手册 6.2.1/6.2.28-30（https://www.displaysino.com/upload/default/20220916/d47ad3e1c21b78a4b9c1b83613e3ae2d.pdf） |
| ST7789V | **0x85, 0x85, 0x52**（RDID1/2/3 对应 04h 的 2/3/4 参数） | ST7789V_SPEC_V0.1（https://dl.sipeed.com/MAIX/HDK/Chip_DS/ST7789V_SPEC_V0.1.pdf）+ 实测帖 Arduino forum "read reg(4): ID = 0x858552"（https://forum.arduino.cc/t/need-help-with-esp32-and-1-54-3-wire-9-bit-display/559876） |
| NV3029S | **未找到公开数据手册中的 RDDID 默认值（明确说"未找到"）**。仅知 Novatek NV3029 家族（NV3029 V1.9 / NV3029C v0.2 / NV3029E）同为 240RGBx320 且用 DCS 风格命令（Scribd: https://www.scribd.com/document/384823602/NV3029datasheet-V1-9 ），按家族惯例 0x04/0xDA-DC 应可读，但具体字节值需上板实测。 |

## 四、最终回答：能否一次 ID 读取判断实装面板？

**能，且推荐一条命令就够**：复位 + SLPOUT 后（实际上 Sleep In 也允许读），发 `0x04`，读 4 字节（丢弃首个 dummy），按 (ID1<<16)|(ID2<<8)|ID3 判：
- 0x009307 → GC9307
- 0x858552 → ST7789V
- 其他值 → 大概率 NV3029S（可再补读 0xDA/DB/DC 逐字节确认，记录其真实 ID 后写死）

注意事项：
1. 16-bit 并行读时需读满 24 bit（DBI `cpu_write_bits`/read 宽度要覆盖 24 bit 或分 3 次 8-bit 读 + 每字节前插 dummy——厂商 gc9360 实现就是读 4 字节）。
2. 0x04 的 3 个 ID 值三款互不相同且与 0x00/0xFF（总线浮空/无应答）明显区分，即使时序不完美也能判出。
3. 移植到 mainline 时：MT6572 DBI 控制器（DISP_DBI_BASE）mainline 无驱动，读回需在自写的 DBI 引擎驱动里实现 CPU-read（readSetup/readHold/readLatency 时序参数可先从提取的三套 init 序列里厂商值抄）。
4. 若走 LK 逆向：三个驱动名都在，若只想在 mainline 判别，把上面 0x04 读回逻辑做成一次性探测工具（比移植三个 LCM 驱动轻得多）。

## 五、参考链接汇总
- MT6572 LK/kernel 源码（disp_drv.c/disp_hal.c）：https://github.com/Intelinsidecom/kernel_ckt72_w_kk-mt6572
- LCM 加载流程详解（mt6582，机制同源，含 compare_id 全代码）：https://www.cnblogs.com/reality-soul/p/4637099.html
- 新一代 LK disp_lcm_probe 同类实现（对照）：https://android.googlesource.com/kernel/mediatek/+/android-mtk-3.18/drivers/misc/mediatek/video/mt8173/videox/disp_lcm.c
- lcm= cmdline 取值方法：https://github.com/arzam16/empty-sixty-five
- compare_id 读 0x04 的真实驱动实例（ILI9806E，MT6572 DSI）：https://github.com/baran0119/ALCATEL_LCM/blob/master/OT_6012D/mediatek/custom/common/kernel/lcm/ili9806e_6572_dsi_vdo/ili9806e_6572_dsi_vdo.c
- GC9307 数据手册：https://www.displaysino.com/upload/default/20220916/d47ad3e1c21b78a4b9c1b83613e3ae2d.pdf
- ST7789V 数据手册：https://dl.sipeed.com/MAIX/HDK/Chip_DS/ST7789V_SPEC_V0.1.pdf
- GC9307 DA/DB/DC 读取示例代码：https://gitlab.zh-jieli.com/e_badge/e_badge_707_sdk_200/-/blob/main/SDK/cpu/br35/ui_driver/lcd_drive/lcd_spi/lcd_spi_gc9307_RQ_BOE_172x320.c
- 本地实锤文件：/home/<user>/claude/6572/mt6572-2/lk.bin（名字表 0x37404-0x37440，ID 读打印串 0x373b7）、/home/<user>/claude/6572/mt6572-2/vmlinux_s6.bin

======================================================================
## TOPIC: fts-touch
VERDICT: FT5822(chip ID 0x58)与 FT5x46(0x54)均属 FocalTech FT5x 互容家族, 与 FT5x06/FT6x06 寄存器全兼容, I2C 固定 0x38, 身份寄存器 0xA3/0xA6/0xA8, 触摸报告从 0x03 起每点 6 字节; S6 vmlinux 内嵌固件项目码为 FTS5X46P001 → 实际芯片大概率为 FT5x46 系 (读 0xA3 定案); mainline edt-ft5x06 的 GENERIC_FT 路径可无缝覆盖基本触摸 (DT 用 focaltech,ft5452 或加别名), 只缺固件升级/手势等无关功能。

## 1) FT5822 / FT5x46 代际、地址、身份寄存器 (来自厂商源码, 已实抓验证)

这两个升级函数不是独立驱动文件, 而是 MTK 树里 FocalTech "FT5X26 目录" 统一驱动 (`drivers/input/touchscreen/mediatek/FT5X26/focaltech_flash.c`) 内按芯片分派的固件升级函数。用 GitHub code search (共 301 处命中, 分布在 OnePlus/小米/bq/MT6893 等几十个 MTK 内核仓) 定位到完整源码:

**芯片 ID 分派表** (bengris32/android_kernel_mediatek_4.19 `focaltech_flash.c` L2985-3005, 多个仓逐字相同):
```
CHIP_ID 0x55/0x08/0x0A → fts_5x06_ctpm_fw_upgrade
CHIP_ID 0x11..0x14    → fts_5x36_ctpm_fw_upgrade
CHIP_ID 0x06          → fts_6x06_ctpm_fw_upgrade
CHIP_ID 0x36          → fts_6x36_ctpm_fw_upgrade
CHIP_ID 0x64          → fts_6336GU_ctpm_fw_upgrade
CHIP_ID 0x54          → fts_5x46_ctpm_fw_upgrade   ← FT5x46
CHIP_ID 0x58          → fts_5822_ctpm_fw_upgrade   ← FT5822
CHIP_ID 0x59          → fts_5x26_ctpm_fw_upgrade
CHIP_ID 0x86/0x87     → FT8606/FT8716 (新代, writepram 引导)
```
固件长度限制 8..54KB。同仓 `focaltech_core.h` L280 注释直接写明: `FTS_CHIP_ID 0x58 /* FT3427、FT5x46 CHIP ID = 0x54 */`。

**代际**: 均为 FocalTech "FT5x 互容多点" 世代 (与 FT5x06/FT5x16/FT6x06 同族同寄存器布局)。FT5x46 datasheet (Crystalfontz 托管页: https://www.crystalfontz.com/controllers/FocalTech/FT5x46/): 内置 MCU, 最多 20 驱动线 + 28 感应线; FT5822 厂商参考固件文件名 `FT5822_Ref_V11_D10_20160414_app.i` (2016 年), 支持最多 5 点 (vendor 头 `CFG_MAX_TOUCH_POINTS 5`)。FT5822 公开 datasheet 未找到可靠原件 (WebSearch 返回的结果链接经抽查为伪造/失效页面, 明确标记未找到; 可信信息只有源码内行为)。

**I2C 地址**: 固定 0x38 (7-bit)。vendor `focaltech_core.c` L1398-1399 硬改: `if (i2c_client->addr != 0x38) i2c_client->addr = 0x38;` — 无论 DT/板文件写什么都强制 0x38。

**身份寄存器** (vendor `focaltech_core.h` L158-160, mainline 也一致):
- `0xA3` = FTS_REG_CHIP_ID (期望读回 0x54=FT5x46 / 0x58=FT5822)
- `0xA6` = FTS_REG_FW_VER (固件版本, 1 字节)
- `0xA8` = FTS_REG_VENDOR_ID (TP 模组厂 ID)
- 触摸报告: 0x00 模式/0x01 gesture/0x02 点数(低4位), 从 0x03 起每点 6 字节: XH[3](bit7:6=事件码 0=down/1=up/2=move, bit3:0=x[11:8]), XL[4], YH[5](bit7:4=touch id, bit3:0=y[11:8]), YL[6] (vendor 头 FTS_TOUCH_X_H_POS=3 等与 mainline GENERIC_FT 路径逐位一致)。

## 2) 与 FT6x06/FT5x06 的兼容性

完全寄存器兼容 (同族)。交叉验证:
- FocalTech 官方仓 `focaltech-systems/drivers-input-touchscreen-FTS_driver` README (已抓): "FT5x06 driver include IC types: FT5x06, FT5606, FT5x16, FT6x06, FT6x36, FT5x06i, FT5336, FT3316, FT5436i, FT5336i, **FT5x46**" — FT5x46 被官方归入同一驱动家族。
- Zephyr PR/issue (https://github.com/zephyrproject-rtos/zephyr/issues/27085) 明确: 对照官方驱动代码, "FT5x06...FT5336i and FT5x46 should be covered", 并确认这些型号 "same registers, field positions"。
- S6 vmlinux 取证 (`/home/<user>/claude/6572/mt6572-2/vmlinux_s6.bin`, strings 实测): 有 `mtk-tpd: Focaltech fts I2C Touchscreen Driver`、`fts_class`、`fts_get_upgrade_array`、`hidi2c_to_stdi2c` 失败串等 → S6 编入的是新一代 **FTS_driver 框架** (即官方 `drivers-input-touchscreen-FTS_driver` 的结构), 不是老的 ft5x06_ts.c; 两个升级函数名都在。
- **关键线索**: vmlinux 内嵌固件项目码字符串 `FTS5X46P001` (全 GitHub 搜 0 命中, 只在此固件), 且无 `FTS5822*` 项目码 → 出厂捆绑的校准固件是 **FT5x46 项目** → 板上实际芯片大概率是 FT5x46 家族 (0xA3 应读回 0x54), FT5822 (0x58) 升级路径只是同文件通编。上板 `i2cget -f -y <bus> 0x38 0xA3` 一读定案。

## 3) mainline edt-ft5x06 覆盖性 (本地 7.0-rc7 源码实查: /home/<user>/claude/6572/fork_linux/drivers/input/touchscreen/edt-ft5x06.c + torvalds master 对比)

**能覆盖基本触摸, 但没有现成 compatible**。事实:
- torvalds master 的 of_match 只有: `focaltech,ft3518/ft5426/ft5452/ft6236/ft8201/ft8716/ft8719` + edt/ev 系 —— **没有 ft5x46/ft5822 条目**。
- probe **不校验芯片 ID**: 先读 0xBB 找 "EP0" (EDT 自有命名), 未命中走 `GENERIC_FT` 回退 —— 恰好读 0xA6(fw ver)+0xA8(vendor id), 然后无条件按标准窗口工作: `tdata_cmd=0x00, tdata_offset=3, point_len=6`, 事件码 `buf[0]>>6`, x/y=`be16 & 0x0fff`, id=`buf[2]>>4` —— 与 vendor FT5x46/FT5822 触摸报告布局逐位相同。
- 因此可行做法: DT 写 `touchscreen@38 { compatible = "focaltech,ft5452"; reg = <0x38>; ... }` (ft5452_data: max_support_points=5, 与两颗芯片的 5 点上限一致), 或给 bindings/driver 加一行别名 `focaltech,ft5x46`/`focaltech,ft5822` 指向 edt_ft5452_data (一处 enum + 两行, 可上游)。
- 注意: probe 会尝试 `regulator_enable(iovcc)`/`vcc`, 有 reset-gpios/irq 支持; DT 需给 `touchscreen-size-x/y` (面板 240x320, 方向实测调)、interrupt-parent。vendor 配置 (MT6580 姊妹板) 显示 TP 供电走 `MT6323_POWER_LDO_VGP1` —— S6 同为 MT6323 PMIC, vcc-supply 大概率指向 mt6323_vgp1/vgp2, 上板实测确认。
- **不支持**: 固件升级 (0x54/0x58 的 writepram 序列只在 vendor 驱动)、手势唤醒 (focaltech_Gesture.c, 0xA5 PWR_MODE 相关)、HID-I2C 回退处理 (若芯片固件以 HID 模式启动需复位序列转标准 I2C; vendor 有 `hidi2c_to_stdi2c`)。听力机场景这些都不要, 无影响。
- 备选: 把官方 `focaltech-systems/drivers-input-touchscreen-FTS_driver` 从 3.4 移植过来 (它才是 S6 原厂同款框架, 有 sysfs/升级), 但工作量大且无必要 —— edt-ft5x06 GENERIC_FT 路径是最低成本且上游友好。

## 上板验证命令 (一次定案芯片型号)
```
i2cget -f -y <bus> 0x38 0xA3    # 0x54=FT5x46 / 0x58=FT5822
i2cget -f -y <bus> 0x38 0xA6    # fw 版本
i2cget -f -y <bus> 0x38 0xA8    # 模组厂
i2cdump -f -y <bus> 0x38 b 32   # 触摸窗口 0x00-0x1f (手摸屏看点数/坐标变化)
```

来源:
- FocalTech 官方驱动仓: https://github.com/focaltech-systems/drivers-input-touchscreen-FTS_driver (README 芯片列表)
- 分派表/寄存器定义实抓源: https://github.com/bengris32/android_kernel_mediatek_4.19/blob/main/drivers/input/touchscreen/mediatek/FT5X26/focaltech_flash.c 与 .../focaltech_core.h
- FT5x46 datasheet 页: https://www.crystalfontz.com/controllers/FocalTech/FT5x46/
- 寄存器兼容性旁证: https://github.com/zephyrproject-rtos/zephyr/issues/27085
- 本地: /home/<user>/claude/6572/fork_linux/drivers/input/touchscreen/edt-ft5x06.c (GENERIC_FT/probe/compatible 表), /home/<user>/claude/6572/mt6572-2/vmlinux_s6.bin (FTS5X46P001/mtk-tpd 字符串)
- 未找到: FT5822 公开 datasheet 原件 (搜索结果为伪造链接, 不采信)

======================================================================
## TOPIC: panel-mipi-dbi-8080
VERDICT: mainline 7.0-rc7 上 panel-mipi-dbi 只支持 SPI, 8080 并口需 backport Durnev 未合入的 mipi-dbi Type B GPIO 补丁, 或直接用树内自带的 staging/fbtft GPIO 位敲 (最快点亮); MTK DBI 控制器无人写过 mainline 后端, 社区也无移植 vendor disp/dbi 先例, simplefb 对 MCU 屏物理上不可行。

## 调研结论(本地 7.0-rc7 源树验证 + 联网交叉验证, 2026-08-31)

### 1) panel-mipi-dbi.c 只支持 SPI 后端 — 确认, 无 8080/Type B 支持
- 本地源树 `/home/<user>/claude/6572/fork_linux/drivers/gpu/drm/tiny/panel-mipi-dbi.c:364` 只调用 `mipi_dbi_spi_init(spi, dbi, dc)`; of_match 唯一 compatible = `"panel-mipi-dbi-spi"`。
- `include/drm/drm_mipi_dbi.h` 里全部 init 入口只有 `mipi_dbi_spi_init`, 无 `mipi_dbi_gpio_init`; `drivers/gpu/drm/drm_mipi_dbi.c`(1521 行) 中 "8080" 只出现在注释里。
- 官方文档同步确认: "Currently mipi_dbi only supports Type C options 1 and 3 with mipi_dbi_spi_init()" (https://docs.kernel.org/gpu/drm-kms-helpers.html, MIPI DBI Helper 一节)。
- 但存在一个**未合入的现成补丁**: Mikhail Durnev 的 `[PATCH v3 1/3] drm/mipi-dbi: Add support for Type B` — 用 GPIO 位敲 8080 并口 (write-only), 新增 `mipi_dbi_gpio_init(dbi, dc, wr, db, wr_up_delay, wr_down_delay)` API, 以及配套 `[PATCH v3 2/3] drm/tiny: Add driver for ili9341 with parallel bus`。链接:
  - https://lkml.indiana.edu/2011.2/04289.html (v1, 2020-12)
  - https://lkml.rescloud.iu.edu/hypermail/linux/kernel/2012.0/03960.html (v3)
  - https://lkml.rescloud.iu.edu/hypermail/linux/kernel/2012.0/03962.html (ili9341 并口驱动)
  - 0day-ci 构建记录显示分支名 `drm-mipi-dbi-Type-B-bus-support-drm-tiny-MRB2801`, 从未进 drm-misc。
- notro (tinydrm/mipi-dbi 作者) 本人在 https://github.com/notro/tinydrm/issues/12 明确说 "tinydrm does not support parallel interface today", 曾设想一个 `struct dpi_device` 抽象, 未实现。
- 另有一个 2020 年方向: Paul Cercueil 的 "DSI/DBI over various buses" 系列 (LWN: https://lwn.net/Articles/822425), 用 DSI 框架挂 SPI/8080 DBI 面板 (测试对象正是 ILI9331 DBI/8080 面板), 同样未合入。
- **结论**: 想走 DRM mipi_dbi 路线, 需要把 Durnev 的 Type B GPIO 补丁 backport 到 7.0-rc7 (补丁不大: 一个 gpio 后端 + struct mipi_dbi 加 wr/db/wr_up_delay/wr_down_delay 成员), 然后写一个类似 panel-mipi-dbi 的 panel 驱动, 把从 lk.bin 提取的三套 init 序列硬编码/转成 DCS 命令流。

### 2) MTK DBI 控制器的 mainline 后端 — 未找到任何人写过
- mainline `drivers/gpu/drm/mediatek/` 只有 mtk_dsi / mtk_dpi / mtk_dp / mtk_hdmi 等, 无 DBI/MCU 8080 后端 (本地目录列表确认)。
- 注意 mtk_dpi.c 是 DPI (RGB 并口时序信号), 不是 MCU/8080 命令总线, 对 S6 这种带内置 GRAM 的 MCU 屏无用。
- postmarketOS 的 MT6572 mainline 成就 (https://wiki.postmarketos.org/wiki/MediaTek_MT6572, Display 列标 Y: "DRM works") 走的是 **DSI**: mt6572-mainline fork (https://github.com/mt6572-mainline) 的树里 `mt6572.dtsi` 有 `dsi@1400c000 (compatible "mediatek,mt6572-dsi")` + ovl/rdma/color/mutex 节点, Lenovo A369i 板级 dts 用 `&dsi { panel@0 }` 挂 DSI 面板 (本地 `/home/<user>/claude/6572/fork_linux/arch/arm/boot/dts/mediatek/mt6572-lenovo-a369i.dts:77` 确认)。**没有任何设备树节点或驱动对应 MCU/DBI 8080 接口**。
- vendor 侧参考: ALPS 的 LCM 框架里 DBI 是标准接口类型 (如 android-mtk-3.18 `drivers/misc/mediatek/ext_disp/.../extd_lcm.c` 里 "Type: DBI"), MT6572/MT6580 vendor 树有 `disp_dbic.c` 这类驱动; 但**搜不到任何把它移植到 mainline 的先例** (GitHub/XDA/postmarketOS 均无)。
- 本地 `mt6572-s6.dts` 注释已正确写明: S6 是 MCU 并口屏, 不是 DSI, simple-framebuffer 永远无效 (MCU 屏 GRAM 在屏内, 需每次刷新主动 burst 写, LK 退出后无人维持显存)。

### 3) 社区怎么点 MCU 并口屏 / 移植 vendor disp 的先例 — 基本没有, 主流是 GPIO 位敲
- postmarketOS/mt65xx 项目: 只找到 DSI 路线 (MT6572 DRM works via mtk_dsi; MT6580 display 标 P, 也是 DSI)。**未找到任何社区项目移植 vendor disp/dbi 驱动到 mainline 的先例**。
- MCU 屏的社区惯例是 fbtft (见下) 或用户态自写。msm8916-mainline 的 "panel driver generator" (https://github.com/msm8916-mainline/linux-mdss-dsi-panel-driver-generator) 只针对 DSI 面板。
- Zephyr RTOS 反而走在了前面: `zephyr,mipi-dbi-bitbang` 支持 `MIPI_DBI_MODE_8080_BUS_8_BIT` (例: https://github.com/zephyrproject-rtos/zephyr/issues/108055 HX8353E over 8080; https://github.com/zephyrproject-rtos/zephyr/issues/99540 ili9488 over 8080), 可作时序参考: WR 拉低→置数据→WR 拉高 (上升沿锁存), 8 位数据线逐位输出。

### 4) fbtft/fbdev + GPIO 位敲是常用做法 — 是, 且本树自带
- `/home/<user>/claude/6572/fork_linux/drivers/staging/fbtft/` 在 7.0-rc7 树中**仍然存在** (staging, 含 fb_ili9325.c/fb_st7789v.c 等 20+ 控制器驱动)。
- `fbtft-io.c:130 fbtft_write_gpio8_wr()` / `:180 fbtft_write_gpio16_wr()` 就是 8080 风格: `gpiod_set_value(par->gpio.wr,...)` + 8/16 根数据 GPIO 位敲, `EXPORT_SYMBOL` 可复用。
- `fbtft-core.c` 通过 devicetree 请求 "reset"/"dc"/"rd"/"wr"/"cs"/"db-0..15" GPIO (`devm_gpiod_get_index_optional`), 即 fbtft 原生支持 DT 描述 8080 并口屏。
- **性能警告**: 位敲每字节约 10-18 次 gpiod_set_value, MT6572 双核 A7 上实测量级通常 0.3-1 MB/s; 全屏 240x320 RGB565 = 153600 B/帧 → 约 2-6 fps。对听力机静态/低帧率 UI 可用, 不适合动画。
- fbtft 属 staging 且维护意愿低 (Greg KH staging 长期目标移除; 新方向是 drm/tiny), 但对 "点亮一块屏" 的短期目标它是最省事的。
- "fbdev/fbcon + 自写 char dev 位敲" 的做法在嵌入式小众屏上存在 (tinydrm issue #12 里被提及为 fallback), 但**不是主流**; 主流顺序是: fbtft (staging, 现成) > drm tiny + mipi-dbi Type B 补丁 (干净, 需 backport) > 全自写。

### 可操作路线排序 (针对 S6)
1. **最快点亮**: 写一个 fbtft 风格板级驱动 (platform device + DT GPIO 描述), 复用 `fbtft_write_gpio8_wr`, 把从 lk.bin 提取的 gc9307/nv3029s/st7789v init 序列作为 `init_display` 硬编码。fbtft 有 `fbtft_device` 机制可用名字注册。需要先把 pinctrl 把 8+4 根 DBI 脚配成 GPIO (mt6572 mainline pinctrl 已有, postmarketOS 标 Y)。
2. **最干净**: backport Durnev 的 mipi-dbi Type B 补丁 (3 个文件量级), 写 `panel-mipi-dbi-8080` 式驱动, 以后可上游。
3. **不建议**: 移植 vendor `disp_dbic.c` (依赖整个 vendor dispsys/mmsys 时钟与寄存器文档, mainline 无任何对应基础, 无先例); simplefb 对 MCU 屏物理上不可能 (mt6572-s6.dts 注释已论证)。

### 关键寄存器/数字
- MT6572 DISP 子系统基址 (vendor DSI 路线已用): mmsys/ovl 0x14007000, rdma 0x14008000, dsi 0x1400c000 (本地 mt6572.dtsi:581-632)。MCU/DBI 控制器在 vendor 树中的基址本次联网搜索**未找到公开数字** (需从 vendor lk/kernel 的 disp_dbic 源码提取, 未找到公开链接)。若走 GPIO 位敲路线则不需要该寄存器。
- 8080 时序参数 (来自 Durnev 补丁): `wr_up_delay` (置数据→WR 拉高) 与 `wr_down_delay` (WR 拉高→下次拉低) 两个 ns 级旋钮; GC9307/ST7789V WR 低脉宽典型 ≥15ns, 10ns 级延迟在 GPIO 位敲下自然满足。

### 来源链接
- mipi_dbi 现状文档: https://docs.kernel.org/gpu/drm-kms-helpers.html
- Type B GPIO 补丁 v1/v3: https://lkml.indiana.edu/2011.2/04289.html , https://lkml.rescloud.iu.edu/hypermail/linux/kernel/2012.0/03960.html , https://lkml.rescloud.iu.edu/hypermail/linux/kernel/2012.0/03962.html
- notro 关于 8080 支持的表态: https://github.com/notro/tinydrm/issues/12
- Paul Cercueil DSI/DBI 总线抽象: https://lwn.net/Articles/822425
- postmarketOS MT6572 (Display=Y via DRM/DSI): https://wiki.postmarketos.org/wiki/MediaTek_MT6572 , https://wiki.postmarketos.org/wiki/Category:MediaTek
- mt6572-mainline 项目: https://github.com/mt6572-mainline
- Zephyr 8080 DBI bitbang 实例: https://github.com/zephyrproject-rtos/zephyr/issues/108055 , https://github.com/zephyrproject-rtos/zephyr/issues/99540
- fbtft staging 历史: https://github.com/raspberrypi/linux/issues/767

======================================================================
## TOPIC: backlight
VERDICT: BLS 就是 dtsi 里 0x1400A000 那个节点; mainline pwm-mtk-disp.c (mediatek,mt2701-disp-pwm) 寄存器布局与 MT6572 vendor BLS 逐一吻合可直接用 (duty 在 +0xA0 10-bit, EN bit16 @+0x00, DEBUG=0xB0 bit[1:0]=3 直通写), 但 .config 里 PWM_MTK_DISP=n 需开; 更优先的是 vendor 参考板实际用的 MT6323 PMIC ISINK (leds-mt6323 + led-backlight); MT6572 没有 0x10006000 独立 LED 控制器, 备选是 0x11008000 AP PWM (mediatek,mt7623-pwm / pwm-mediatek.c, Prestigio MT6572 板实证)。

## MT6572 (S6) 背光驱动调研结论

### 1) BLS 硬件基址 — 确认: 就是 dtsi 里的 `bls: pwm@1400a000`

- 物理基址 **0x1400A000** (4KB)。vendor 代码用虚拟映射 `DDP_REG_BASE_DISP_BLS = 0xF400A000` (虚拟=物理+0xE0000000), 见 `/home/<user>/claude/6572/wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/dispsys/ddp_reg.h:12`。
- S6 原厂 `lk.bin` (246KB, `/home/<user>/claude/6572/mt6572-2/extracted/lk.bin`) strings 实测含完整 `[DSS](0A0)DISP_REG_BLS_PWM_DUTY=0x%x` / `[DSS](0A8)DISP_REG_BLS_PWM_CON` / `[DSS](0B0)DISP_REG_BLS_DEBUG` 等 dump 格式串 + `[DDP] disp_bls_init` / `disp_bls_set_backlight: %d` — **S6 LK 确实在用 0x1400A000 这套 BLS 寄存器**, 与 ddp_reg.h 逐一对应。
- fork 树 mt6572.dtsi:597 的节点 (compatible `mediatek,mt2701-disp-pwm`, clocks = `CLK_MM_DISP_MDP_BLS_26M`(=15) + `CLK_MM_DISP_BLS`(=5), 在 `drivers/clk/mediatek/clk-mt6572-mm.c:38,48` 都是真实 gate), 来自 Max Shevchenko 的 MT6572 mainline 系列 (https://www.opennet.ru/kernel/ml/6.18.html 提到其 MT6572 patch series)。
- 注意: fork `.config` 当前 `CONFIG_PWM_MTK_DISP=n` —— **节点有但驱动没编**。

### 2) BLS 占空比寄存器 (从 vendor `ddp_reg.h` + `ddp_bls.c` 提取, 偏移相对 0x1400A000)

| 寄存器 | 偏移 | 作用 |
|---|---|---|
| BLS_EN | +0x00 | bit0 = BLS 功能使能; **bit16 = 模块使能**; 写 `0x00010001` = 开 |
| BLS_SRC_SIZE | +0x18 | `(h<<16)|w`, 即 0x014000F0 (320x240) |
| **BLS_PWM_DUTY** | **+0xA0** | **10-bit 0..1023, 占空比 = duty/1024** (vendor: `mapped = level*1023/255`, 全亮写 0x3FF) |
| BLS_PWM_CON | +0xA8 | 时钟/分频; vendor 写 0 |
| BLS_PWM_MANUAL | +0xAC | manual/period 模式参数 |
| **BLS_DEBUG** | **+0xB0** | **bit[1:0]=0x3 = 关 double-buffer 直通写** (vendor 改 duty 前后都先写 0x3, 改完回 0) |

- vendor 完整初始化 (`disp_bls_init`/`disp_bls_config_full`): `DEBUG=0x3 → PWM_DUTY=0x3FF → PWM_CON=0 → SRC_SIZE=(h<<16)|w → GAMMA/INTEN → BLS_EN=0x00010001 → DEBUG=0x0`。
- 调亮度 (`disp_bls_set_backlight`): `DEBUG=0x3 → PWM_DUTY=mapped → DEBUG=0x0`。时钟前提: MMSYS 的 `MT_CG_MDP_BLS_26M_SW_CG` 必须开着。

### 3) mainline `pwm-mtk-disp.c` 可用于 MT6572 — 寄存器布局逐一吻合

`/home/<user>/claude/6572/fork_linux/drivers/pwm/pwm-mtk-disp.c` 的 `mt2701_pwm_data` 与 MT6572 vendor BLS 完全对齐:
- `enable_mask = BIT(16)` @ DISP_PWM_EN(0x00) ↔ vendor BLS_EN bit16 ✅
- `con0 = 0xA8` (+`con0_sel=0x2` 选 manual 模式) ↔ vendor BLS_PWM_CON(0xA8) ✅
- `con1 = 0xAC` (12-bit period | 13-bit high_width) ↔ vendor BLS_PWM_MANUAL(0xAC) ✅
- `bls_debug = 0xB0, mask=0x3` ↔ vendor BLS_DEBUG(0xB0) 关 double-buffer ✅

用法 (参考 `mt2701-evb.dts:56-65`, fork 树内):
```
backlight { compatible = "pwm-backlight"; pwms = <&bls 0 100000>;
            brightness-levels = <0 ... 255>; default-brightness-level = <9>; };
&bls { status = "okay"; };
```
频率核算: main clock = MDP_BLS_26M (26MHz), period 12-bit: 全范围约 25kHz 无闪烁 (`period=1039, div=0 → 1040/26MHz ≈ 40us`)。
**需要开**: `CONFIG_PWM_MTK_DISP=y` (+ `BACKLIGHT_PWM` 已 y)。binding: `Documentation/devicetree/bindings/pwm/mediatek,pwm-disp.yaml`。上游 4 块 MT6572 板子 (lenovo-a369i / jty-d101 / energy-phone-colors) 都是 `&bls { status="okay"; }`。

### 4) 备选方案与"0x10006000 LED 控制器"澄清

**a) 首选备选 = MT6323 PMIC ISINK (vendor 参考板实际用的就是这个)**
- drsami 树两块 MT6572 参考板 (`mediatek/custom/cci72_we_{lca,jb3}/kernel/leds/mt65xx/cust_leds.c`) 的 lcd-backlight 都是 `MT65XX_LED_MODE_PMIC + MT65XX_LED_PMIC_LCD_ISINK`。vendor `leds.c:1458+` 的 MT6572 实现: `upmu_set_isink_ch0_step(0x5)` (24mA) + ck_sel=1MHz + ISINK_PWM_MODE + `rg_isink0_double_en(1)`, duty 经 108 级映射表。
- mainline 驱动现成: `drivers/leds/leds-mt6323.c` (compatible `mediatek,mt6323-led`, 4 ISINK 通道; 寄存器 `MT6323_ISINK0_CON0..3 = 0x0330..0x0336` 与 vendor `upmu_hw.h` 一致, 走 pmic_wrap regmap)。mt6323.dtsi 节点 `mt6323_leds` 默认 disabled → 在 S6 dts `&mt6323_leds { status="okay"; led@0 {...}; }` + `CONFIG_LEDS_MT6323=y` (依赖 `MFD_MT6397`+`LEDS_CLASS`, Prestigio MT6572 板已示范, `mt6572-prestigio-pap5500-duo.dts:211`)。要做成 /sys/class/backlight 再加 `led-backlight` (`drivers/video/backlight/led_bl.c`)。
- S6 `vmlinux_s6.bin` strings 同时含 `backlight control through BLS!!1` / `through LCM!!1` / `[LEDS]LK: ISINK Current = %d` — **S6 具体走哪条需上板确认** (vendor 参考配置指向 PMIC ISINK; LK 的 `backlight_set_pwm:duty` 串说明 AP-PWM 路径也编进了)。建议先试 ISINK, 不亮再试 BLS/AP-PWM。

**b) 通用 AP PWM 块 @ 物理 0x11008000 (5 通道)** — Prestigio MT6572 板真实用它做背光: `pwms = <&pwm 1 31250>` + `bl_pwm_pins` pinctrl。mainline 驱动 `drivers/pwm/pwm-mediatek.c` (`mediatek,mt7623-pwm`, `mt7623_pwm_data`: num_pwms=5, chanreg_base=0x10, width=0x40), dtsi 里 `pwm: pwm@11008000` 已有; 需 `CONFIG_PWM_MEDIATEK=y` (当前 =n)。

**c) "mt65xx LED 控制器 @0x10006000" — MT6572 上不存在**: `mt_reg_base.h` 无此定义 (只有 PWM_BASE 0xF1008000→物理 0x11008000, GPIO_BASE 0xF0005000); MT6572 的 LED/背光硬件全在 MT6323 PMIC 里 (S6 LK `[LEDS]LK: PMIC Type: %d` 串印证)。`mt65xx_led` 只是 vendor 的驱动抽象层 (`mediatek/kernel/drivers/leds/leds.c`), 把请求路由到 PMIC ISINK / BLS / AP PWM 三选一。老 MT6575/6577 才有独立 LED 块, 且 mainline 无其驱动 — 此路在 MT6572 不可用也不必走。

### 来源链接
- vendor MT6572 LK ddp_bls.c 镜像: http://git.huayusoft.com/tomsu/AP7200_MDK-kernel/blob/master/mediatek/platform/mt6572/lk/ddp_bls.c
- vendor leds_drv.c (android-mtk-3.18): https://android.googlesource.com/kernel/mediatek/+/android-mtk-3.18/drivers/misc/mediatek/leds/leds_drv.c
- LEDS_MT6323 Kconfig/compatible: https://cateee.net/lkddb/web-lkddb/LEDS_MT6323.html
- MT6572 mainline patch series (Max Shevchenko): https://www.opennet.ru/kernel/ml/6.18.html
- postmarketOS MT6572 wiki (搜到但内容被网络策略挡住, 仅确认页面存在): https://wiki.postmarketos.org/wiki/MediaTek_MT6572
- 本地关键文件: `/home/<user>/claude/6572/wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/dispsys/{ddp_bls.c,ddp_reg.h}`、`.../mediatek/kernel/drivers/leds/leds.c`、`.../mediatek/custom/cci72_we_lca/{kernel/leds/mt65xx,lk}/cust_leds.c`、`/home/<user>/claude/6572/fork_linux/drivers/pwm/pwm-mtk-disp.c`、`/home/<user>/claude/6572/fork_linux/arch/arm/boot/dts/mediatek/mt6572.dtsi:597`、`/home/<user>/claude/6572/mt6572-2/extracted/lk.bin` (strings 交叉验证)

### 落地清单 (S6 dts/config)
1. `CONFIG_PWM_MTK_DISP=y` (BLS 路) 或 `CONFIG_PWM_MEDIATEK=y` (AP PWM 路) 或 `CONFIG_LEDS_MT6323=y`+`led-backlight` (PMIC ISINK 路, 优先)。
2. BLS 路 dts: `&bls { status="okay"; };` + `backlight { compatible="pwm-backlight"; pwms=<&bls 0 100000>; ... }`。
3. PMIC 路 dts: `&mt6323_leds { status="okay"; led@0 { reg=<0>; color=<LED_COLOR_ID_WHITE>; function="lcd_backlight"; default-state="on"; }; };`。
4. 背光输出脚: 上游 4 块 MT6572 板都没给 &bls 配 pinctrl (MT6572 DISP_PWM 走固定脚), 但 MCU 面板的背光脚位需按 S6 LK 的 GPIO/PMIC 配置实测确认 (Z1/S6 若是 PMIC ISINK 直驱则无引脚问题)。

======================================================================
## TOPIC: vendor-lcm-src
VERDICT: 三个目标 LCM 驱动 (gc9307/nv3029s/st7789v_mcu_6572) 在 GitHub/Gitee/Sourcegraph/全网均无公开源码副本; 唯一的 KK1.MP7 MT6572 BSP 泄漏 (rock12/ALPS.KK1.MP7.V1_BIRD72_CWET_A_KK_KERNEL) 只含官方 LCM 库, 无这三个驱动。重建须用已提取的 lk.bin init 序列 + nt35510_mcu_6572/st7789h2_dbi 公开骨架。

# S6 LCM 驱动源码公开副本调研结果

## 一句话结论：三个目标驱动名 (gc9307_mcu_6572 / nv3029s_mcu_6572 / st7789v_mcu_6572) 在公开互联网上均未找到任何源码副本。必须走"从已提取的 lk.bin init 序列 + 公开骨架模板手工重建"路线。

## 搜索覆盖 (多角度交叉验证)
- GitHub repo search API: gc9307 / nv3029s / st7789v / gc9307_mcu / mcu_6572 / nv3029 / ALPS.KK1 / BIRD72 / VANZO72 / gc9307+lcm / st7789v+lcm / alps+i3 等十余组查询
- 全部 11 个公开 MT6572 KK kernel 仓库的完整 git tree (5-6.5万文件/个) 逐一 grep: drsami/mt6572_kernel, MTKKernel/android_kernel_mt6572, rex-xxx/KK_kernel_iq4404_MT6572, kuronosan/KK-Kernel-MT6572, ChiefzReloaded/MTK_mt6572_kernel_master, lehoangnb, eagleeyetom, safwan1469, rock12/ALPS.KK1.MP7.V1_BIRD72_CWET_A_KK_KERNEL, icprog/ALPS.KK1.MP6.V1_VANZO72_WE_72_KK_KERNEL, Intelinsidecom/kernel_ckt72_w_kk-mt6572
- LinerSRT/LCM_mediatek (234 个 LCM 驱动的最大公开合集) 全目录扫描
- Sourcegraph 代码搜索 (含 fork:yes archived:yes; 注: 其后端当时部分降级)
- grep.app (被 Vercel checkpoint 拦截, 无法直查)
- Gitee 搜索 (gc9307/nv3029/mt6572 kernel): 零结果
- Tavily/WebSearch/firecrawl web+developer 搜索, postmarketOS wiki

## 逐项结果

### 1) gc9307_mcu_6572 / gc9307_mcu_6572_s6_24_hsd — 未找到
全 GitHub 只有 4 个含 "gc9307" 的仓库, 全部无关:
- zzzz0317/kmod-fb-tft-gc9307 (OpenWRT SPI fb driver)
- IvanLi-CN/gc9307-async-rs (Rust SPI)
- osptek/1.47-tft-172x320-spi-gc9307 (SPI 模组资料)
- nu11plex/android_z16s_m7s_gy_1_8_gc9307_240x284_dump — **名字含 gc9307 但仓库为空** (0 KB, 2024-11-17 建, 从未推送任何内容), 描述 "fake 9.0 (7.1 api 25)", 无源码
"nv3029"/"nv3029s" 在 GitHub 仓库搜索为**零结果**。该 IC 几乎无公开痕迹。

### 2) nv3029s_mcu_6572 — 未找到 (同 1)

### 3) st7789v_mcu_6572 — 未找到, 但有高价值模板
没有任何 `st7789v_mcu_6572`。最接近的可复用代码:
- **LinerSRT/LCM_mediatek/st7789h2_dbi/st7789h2_dbi.c** (606 行) — ST7789H2 走 DBI (MCU 8080 并口) 的完整 MTK LCM 驱动, 已抓取到 /tmp/lcm_ref/。关键数字: ID 回读 `LCM_ID0=0x85, LCM_ID1=0x85, LCM_ID2=0x52` (即 0x04 命令回读 0x85 0x85 0x52)。ST7789V 同家族, 此驱动是最直接的移植模板。
- **nt35510_mcu_6572/nt35510_mcu_6572.c** (800 行) — 存在于所有 11 个公开 MT6572 KK kernel 中, 路径 `mediatek/custom/common/kernel/lcm/nt35510_mcu_6572/`。这是 MT6572 MCU-8080 LCM 驱动的官方骨架范本 (LCM_UTIL_FUNCS/lcm_util.send_cmd/BUILD_LK 双模式)。已抓取到 /tmp/lcm_ref/nt35510_mcu_6572.c。
- 所有 MT6572 BSP 中仅有的两个 `*_mcu_6572` 驱动就是这两个 (nt35510, hx8369_mcu_6572), 没有第三个。

### 4) ALPS KK1 MP7 MT6572 BSP 泄漏 — 存在但无用
- **rock12/ALPS.KK1.MP7.V1_BIRD72_CWET_A_KK_KERNEL** (唯一公开的真 KK1.MP7 MT6572 BSP kernel, BIRD72 项目, 54963 文件, 145 个 LCM)
- icprog/ALPS.KK1.MP6.V1_VANZO72_WE_72_KK_KERNEL (KK1.MP6)
- Intelinsidecom/kernel_ckt72_w_kk-mt6572 (CKT72 KK 项目)
关键: 它们共享同一套 MTK 官方 LCM 库 (mediatek/custom/common/kernel/lcm/), 全库 145 个驱动里 **0 个** 含 gc9307/nv3029/st7789v。这三个驱动是深圳方案商在 BIRD/VANZO/CKT 项目里自加的, 从未进官方库, 也未随这些 GPL 泄露公开。
- 固件下载站 (service-gsm.net, firmware.center 等) 有海量 ALPS.KK1.MP7.V1 整机固件(含 lk.bin), 但那是二进制不是源码。

### 5) 其他有价值的参考 (已确认链接)
- **postmarketOS MTK LCM Decoder wiki**: https://wiki.postmarketos.org/wiki/MTK_LCM_Decoder — lk.bin 中 init 表的精确布局: `{cmd(1-4B), count/延时(1-4B), 定长参数数组(通常64或128B)}`, 结尾 `REGFLAG_END_OF_TABLE`(单字节+全0), `REGFLAG_DELAY`(延时值在 count 字段)。这验证了我们从 lk.bin 提取序列的方法, 也是转写成新驱动 .c 的对照格式。
- **Steven's Blog "Reconstructing MTK LCM drivers"**: https://www.stevenhoneyman.co.uk/2015/02/reconstructing-mtk-lcm-drivers.html — 官方无源码时的标准重建法: 用同类驱动骨架 + 从 lk.bin 抠出 init 序列填入。
- **ikaris375/lk-analyzer** (GitHub) — MTK LK bootloader 逆向工具, 可恢复显示驱动信息。
- **GC9307 应用笔记**: https://www.scribd.com/document/384823595/GC9307-Application-Notes-Ver01 — 含 2.4"/2.8"/1.54" 各厂面板 (HSD/IVO/TM/CTC/BOE) 的官方 init 代码; "s6_24_hsd" 命名里的 24=2.4寸, hsd=HannStar, 正好对应该笔记覆盖的面板类型 (可作交叉校验, 但非 MTK 驱动源码)。

## 操作建议
既然三份原厂 init 序列已从 lk.bin 提取完毕, 重建路线完全可行:
1. 以 `nt35510_mcu_6572.c` 为骨架 (MT6572 MCU-8080 接口/时序参数写法权威范本)
2. 若确认为 ST7789V: 直接拿 `st7789h2_dbi.c` 换 init 表, ID 回读应为 0x85,0x85,0x52 (0x04 命令)
3. GC9307: 用 GC9307 应用笔记里 2.4" HSD 面板的官方 init 与提取序列交叉验证
4. mainline 7.0-rc7 侧没有 MTK DBI/8080 并口框架, 点屏实际要走 GPIO 位操 (bit-bang) 或移植一个小型 8080 总线驱动; init 序列本身是 IC 级命令, 与宿主接口无关, 提取结果可直接复用

======================================================================
## TOPIC: dbi-writeonly
VERDICT: MT6572 的 DBI/MCU 8080 并口硬件和厂商驱动层都支持读回(本地厂商源码证实: LCD_ReadIF 读 PCMD0 @0x14012000+0xF00, lcm_read_data 钩子, S6 lk.bin 内含同款读时序代码串), 但读回前提是面板 RD 引脚布线且面板响应; 三款候选中只有 ST7789V 有社区验证的可靠读 ID (0x04 命令, 连读两次, 期望 0x858552), NV3029S 指令兼容但 ID 值未公开, GC9307 无可靠读 ID; 最简判定法是抓一次 S6 原厂启动串口日志看 `lcm=`/`[LCM Auto Detect]` 行, 其次拆屏看 FPC 丝印, 兜底逐套试(建议先试带项目定制后缀的 gc9307_mcu_6572_s6_24_hsd)。

# S6 MCU 并口屏调研结果(联网 3+ 角度交叉验证 + 本地厂商源码实证)

## 1) MTK DBI/MCU 8080 并口总线是否支持读回?— 支持(硬件+驱动层都有读), 但有引脚前提

**本地厂商源码铁证** (`/home/<user>/claude/6572/wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/video/`):
- `lcd_drv.c:1437` 有现成 `LCD_ReadIF(LCD_IF_ID id, LCD_IF_A0_MODE a0, UINT32 *value, bits)` — 对 **并行口** (LCD_IF_PARALLEL_0/1/2) 和串口 (SCMD) 都支持, 实现就是从 `LCD_REG->PCMD0/1/2` 寄存器 INREG8/16/32 读出 (lcd_drv.c:1418 `_LCD_ReadIF`)。
- 读时序参数硬件级存在: `LCD_Config()` 带 `readSetup/readHold/readLatency` (映射到 C2RS/C2RH/RLT 寄存器字段, lcd_drv.c:812-814); `lcm_drv.h:314-316` LCM_PARAMS.dbi 有 `read_setup/read_hold/read_latency` 字段。
- 寄存器布局: `lcd_reg.h:271` PCMD0 偏移 **0xF00**, SCMD0 偏移 **0xF80**; DBI 控制器基址 `DISP_DBI_BASE = 0xF4012000` (虚拟, `mt_reg_base.h:22`, 物理地址 = **0x14012000**, MT6572 惯用 0xF0000000 静态映射)。
- 接线完整: `disp_drv.c:242 lcm_read_data()` → `LCD_ReadIF(ctrl_if, LCD_IF_A0_HIGH,...)`, 在 `LCM_UTIL_FUNCS.read_data` 钩子里提供给 LCM 驱动 (disp_drv.c:376)。
- LCM 框架层: `lcm_drv.h:529` `unsigned int (*read_data)(void)`; 多个 MCU 屏驱动用它做 `lcm_compare_id()`, 例如 `nt35582_mcu.c:363`: `send_ctrl_cmd(0x1180); id = read_data_cmd(); ...`。同目录 `nt35510_mcu_6572/ili9481/hx8369_mcu_6572` 等 9+ 个 MCU 驱动都用 `lcm_util.read_data()` 读 ID。

**S6 自己的 lk.bin 实证** (`/home/<user>/claude/6572/mt6572-2/lk_s6_orig.bin` strings):
- 含 `readSetup <= 16U` / `readHold <= 16U` / `readLatency <= 64U` ASSERT 串(与厂商源码 lcd_drv.c LCD_Config 逐字相同) + `read_data0, %x,%x,%x,%x` printf + `DISP_DetectDevice` + 3 处 `[LCM Auto Detect]` 串。**读回路径已编进 S6 原厂 LK。**
- LK 自动探测日志格式: `[LCM Auto Detect] [%d] - [%s]` / `we have %d lcm drivers built in`。

**前提/坑**:
- 8080 读需要面板 **RD/LRDB 引脚实际布线到 SoC**。MTK 引脚定义 (CSDN mt6575 LCD 调试文): LPA0=D/CX, LPCE=CSX, LWRB=WRX, **LRDB=RDX** — 硬件信号存在; 但低成本 8080 模组常只接写 (Arduino 社区通病: "Your TFT is write-only", MISO/RD 没接)。
- 即使总线能读, 还取决于面板是否响应读(见 §2)。

## 2) 三款面板的"读 ID"可靠性

**ST7789V — 有可靠读 ID**:
- 官方 spec (Sitronix, V0.1, 308 页): `9.1.3 RDDID (04h): Read Display ID`, 返回 24bit。表格里 ID1/ID2/ID3 分 3 字节读出。
- 社区实测值: `reg(0x04) = 0x858552` (即 ID1=0x85, ID2=0x85, ID3=0x52), 且 **必须连读两次** ("ST7789 needs twice", 第一次读可能无效)。0xDA/0xDB/0xDC 也可分读 ID1/ID2/ID3。
- 注意: Arduino 社区这些读回测试走的是 SPI (3-wire bidirectional SDA 或 4-wire MISO); 8080 并口模式下读回需要 RD 引脚 + 正确 read 时序。

**NV3029S — 指令集兼容读, 但 ID 值未公开确认**:
- datasheet 标题即 "NV3029S Compatible ST7789V" (NewVision, 240RGBx320, 8/9/16/18-bit 并口 + 3/4-wire SPI), RDDID(04h) 按 ST7789V 协议存在。
- 我们提取的 nv3029s.init 序列用的是 ST7789V 系指令 (0xb2 porch/0xb7/0xbb/0x35 TE), 佐证兼容。
- **未找到** NV3029S 读回的具体 ID 字节值 (网上无公开实测)。

**GC9307 — 无可靠读 ID**:
- GalaxyCore 系 (GC9307/GC9A01A 等) datasheet 有 0xFE/0xEF 解锁页, 但社区共识是 GC 系不实现标准 0x04 RDDID 返回可用 ID (TFT_eSPI/MCUFRIEND 对 GC 屏读 ID 普遍失败/不推荐依赖)。
- datasheet 可获得: GC9307 DS V1.00 (194 页) — https://www.scribd.com/document/384823605/GC9307-DS-V1-00 ; 规格页 https://www.displaysino.com/icDetails/GC9307.html 。datasheet 内如有 read 命令表可自行核, 但社区结论是别依赖。

## 3) 不读 ID 如何判断板上焊的是哪款 — 按可靠度排序

**A. 最省事: 抓原厂启动日志(0 风险)**。MTK LK 会把选中的屏名以 `lcm=` 加进 kernel cmdline (MT6582 已验证行为, 见 LIEBERBIBER LK 逆向文), 且 LK 自己打 `[LCM Auto Detect]` 日志。**用 C3 桥串口抓一次 S6 原厂固件启动, log 里就有答案**(LK UART 输出 + kernel "Kernel command line:" 行)。注意原厂 `printk.disable_uart` 类开关是否存在需看实际抓取。

**B. LK 自动探测逻辑参考**: S6 LK 内建 3 个驱动 (`gc9307_mcu_6572_s6_24_hsd` @0x37404 / `nv3029s_mcu_6572` @0x37420 / `st7789v_mcu_6572` @0x37434, 名字偏移已在 lk.bin 定位), 开机跑 compare_id 逐个试 (MTK LCM 移植文档: "compare_id 必须保证可以正确读取, 通过 resume 打印信息查看 id")。但注意: 若某驱动 compare_id 是纯写探测 (GC9307 无读 ID 只能这样), 判据弱。

**C. 硬件读 ID 探测 (mainline/LK 自写代码)**: 写 `0x04` 命令后读 3 字节 → 0x858552 = ST7789V; NV3029S 若兼容应返回相近值; GC9307 预期读回 0xFF/乱值。寄存器路径: ioremap 0x14012000, 先经 LCD_Config 设 read 时序 (C2RS/C2RH/RLT), 然后写命令到 PCMD0(偏移 0xF00, A0_LOW=命令), 再从 PCMD0(A0_HIGH) 读数据。若硬件 RD 未接 → 读回浮空 (全 1/全 0)。

**D. 拆机看丝印**: 屏幕 FPC 排丝印通常印面板型号 (如 HSD/JBT 前缀) 和 IC 型号; COG/COF 上 IC 激光打码 (打磨屏除外)。对 3 候选可直接目检。

**E. 逐套 init 试** (兜底): 三套序列差异大、互不易烧 (错误命令多数被忽略): GC9307 特征 = 0xFE+0xEF 双解锁 + 0xc3-0xc8 + 0xf0/f1/f2/f3 gamma; NV3029S 特征 = 0xFD 0x06,0x07 解锁 + 0x60-0x69 电源系; ST7789V 特征 = 无解锁、0xB2/0xB7/0xBB 标准命令。上电按任一序列初始化后填纯色查显示正常与否即可分辨。**先试 `gc9307_mcu_6572_s6_24_hsd`** — 驱动名带 `s6_24_hsd` 项目定制后缀 (其余两个是通用名), 按 MTK 习惯, 带项目后缀的是主选料号。

**提取序列已就位**: `/home/<user>/claude/6572/.tmp_s6kit_check/seqs_structured.txt` (三套完整含 reset 时序), `decoded_seqs.txt` 为反汇编原始。

## 来源链接
- ST7789V spec (RDDID 04h): https://dl.sipeed.com/MAIX/HDK/Chip_DS/ST7789V_SPEC_V0.1.pdf
- ST7789V 读 ID 实测 0x858552 + 连读两次: https://forum.arduino.cc/t/need-help-with-esp32-and-1-54-3-wire-9-bit-display/559876 ; 诊断 sketch: https://github.com/prenticedavid/ST7789_kbv/blob/master/examples/ST7789_readreg_diag/ST7789_readreg_diag.ino
- 读 ID 枚举法 (0x04 → 934100/858552/0047): https://industrialmonitordirect.com/it/blogs/knowledgebase/wiring-28-320x240-tft-lcd-to-arduino-spi-driver-code
- NV3029S 兼容 ST7789V datasheet: https://www.displaysino.com/icDetails/NV3029S-Datasheet-ST7789V.html ; NV3029 V1.9: https://www.scribd.com/document/384823602/NV3029datasheet-V1-9 ; NewVision datasheet 站: https://www.crystalfontz.com/controllers/NewVision
- GC9307 datasheet: https://www.scribd.com/document/384823605/GC9307-DS-V1-00 ; 规格: https://www.displaysino.com/icDetails/GC9307.html
- MTK parallel DBI 信号 (LPA0/LPCE/LWRB/LRDB 读引脚) + lcd_drv 文件位置: https://blog.csdn.net/weixin_29807369/article/details/117778638
- MTK LCM 移植 (compare_id 读 ID): https://blog.csdn.net/u010783226/article/details/115918526 ; LK display 框架: https://blog.csdn.net/qq_38012639/article/details/122686522
- LK 附加 `lcm=` cmdline 到 kernel: http://www.lieberbiber.de/2015/07/05/mediatek-details-little-kernel
- MTK LCM 解码器 (从 lk.bin 提取序列): https://wiki.postmarketos.org/wiki/MTK_LCM_Decoder
- "write-only" 模组实例 (RD/MISO 未接): https://forum.arduino.cc/t/2-4-tft-lcd-display-white-flash/545211 ; https://github.com/ropg/spi_lcd_read
- mainline mipi-dbi 仅 SPI 后端 (无 8080): 与本地 S6_FTS_LCM_ROUTE_20260829.md 结论一致, 交叉印证