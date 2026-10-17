# Z1 (MT6572) CONNSYS 固件时序深挖 — ROM Patch + RAM Code 下载时序与 func-on 前置条件

> 深挖日期: 2026-08-14 | 板子关机, 纯只读源码分析
> 源码根: `/tmp/connsys_build/` (mainline 移植 fork; 部分文件已带 2026-08-14 的稳定性修复注释)
> 结论一句话: **hw_check 读 chip id(0x80000008) 必须靠 CONN MCU ROM 的 STP 引擎 + WMT 命令处理器活着应答; launcher 的 `WMT_IOCTL_SET_STP_MODE`(→ hifType=BTIF + g_stp_if_type=BTIF + HIF_CONF op) 必须在任何 func-on 之前到达 wmt 内核线程; ROM patch 由内核 `sw_init` 主动拉取(patch_info_prepare → "srh_patch" → launcher 扫 /etc/firmware 回灌 SET_PATCH_NUM/SET_PATCH_INFO), 不是 launcher 主动推。**

---

## 0. 三实体与命令面

| 实体 | 文件 | 角色 |
|---|---|---|
| 内核 wmt 核心 | `conn_soc/core/wmt_core.c` + `wmt_ctrl.c` + `wmt_lib.c` | 执行 func-on op / STP 打开 / hw_check / sw_init / patch 下载 |
| 内核 STP 传输 | `conn_soc/core/stp_core.c` + `linux/pri/stp_exp.c` + `linux/pri/stp_btif.c` + `btif/mtk_btif_exp.c` | 把 WMT 命令帧成 STP 包, 经 BTIF 硬件(UART)发到 CONNSYS; 收 CONN MCU 应答 |
| launcher | `tools/launcher/stp_uart_launcher.c` | 开机异步跑; SET_STP_MODE 配置 HIF; LPBK func-on 上电; 响应 "srh_patch" 提供 patch 文件信息 |
| CONN MCU ROM | (芯片内) | ROM 启动代码内含 STP 引擎 + WMT 命令处理器; 出厂阶段应答 QUERY_STP/SET_STP/SET_REG/patch 下载 |

所有内核→芯片的 WMT 命令都是 `{0x01, len_hi, len_lo, 0x00, ...}` 载荷, 再被 STP 帧封装(BTIF 路径帧头 `0x80|seq|ack + type/len + len + checksum + crc16`)。**芯片侧执行命令的是 CONN MCU ROM, 不是裸硬件** —— 这是理解 hw_check 依赖关系的关键。

---

## 1. `mtk_wcn_soc_sw_init()` 完整 ROM patch 下载序列 (wmt_ic_soc.c:980)

调用点: `wmt_core_stp_init()` (wmt_core.c:699) 在 `hw_check` 成功之后调 `p_ic_ops->sw_init()`。所有步骤都走 `wmt_core_init_script()` → `wmt_core_tx/rx()` → STP。

### 1.0 前置(在 sw_init 之前, 属于 wmt_core_stp_init, wmt_core.c:636)
1. `WMT_CTRL_STP_OPEN` (wmt_core.c:655) → `wmt_ctrl_stp_open` (wmt_ctrl.c:456): `mtk_wcn_stp_open_btif()`(注册 stp_btif_if_tx=mtk_wcn_consys_stp_btif_tx, stp_btif.c:88) + 注册 STP rx 解析器 + 注册 WMT task 回调 + 置 `WMT_STAT_STP_OPEN`。
2. `WMT_CTRL_STP_CONF` EN=0 / RDY=0 (wmt_core.c:661-666)。
3. `if hifType==WMT_HIF_BTIF`: `WMT_CTRL_STP_CONF` MODE=`MTKSTP_BTIF_MAND_MODE`(bit3) (wmt_core.c:669-673) → `mtk_wcn_stp_set_mode()` → `f_mode = BTIF_MAND`。**没有这一步 f_mode=0, 后面 `mtk_wcn_stp_send_data()` 三条模式分支全不命中直接 return 0, hw_check 的 Tx 必然失败。**
4. `WMT_CTRL_STP_CONF` EN=1 (wmt_core.c:675-677) → `mtk_wcn_stp_enable(1)` + 置 `WMT_STAT_STP_EN`。
5. `wmt_core_hw_check()` (wmt_core.c:684) — 见 §3。
6. `sw_init()`。

### 1.1 BTIF 初始化三连 (只在 hifType==BTIF 时执行, wmt_ic_soc.c:1014-1050)
| 步骤 | init_script | CMD | EVT | 含义 |
|---|---|---|---|---|
| 1 | `init_table_1_2` (L1017) | `WMT_QUERY_STP_CMD` `{01 04 01 00 04}` | `WMT_QUERY_STP_EVT_DEFAULT` `{02 04 06 00 00 04 11 00 00 00}` | "query stp default" — 探测 ROM STP 默认选项 (TEST-ONLY)。**这是对 CONN MCU ROM STP 引擎的第一发探测** |
| 2 | `init_table_4` (L1024) | `WMT_SET_STP_CMD` `{01 04 05 00 03 DF 0E 68 01}` | `WMT_SET_STP_EVT` | "set stp" — 命令 ROM 把芯片侧 STP 切到 BTIF full-set 配置(payload `03 DF 0E 68 01`) |
| 3 | STP_CONF MODE=BTIF_FULL + EN=1 (L1031-1040) | — | — | 主机侧 f_mode 升到 BTIF_FULL |
| 4 | `osal_sleep_ms(10)` (L1043) | — | — | "enough for chip do mechanism switch. at least 2ms needed" |
| 5 | `init_table_5` (L1045) | `WMT_QUERY_STP_CMD` | `WMT_QUERY_STP_EVT` `{02 04 06 00 00 04 DF 0E 68 01}` | "query stp" — 确认芯片已报 BTIF full-set 配置(evt 里带 `DF 0E 68 01`) |

### 1.2 ROM patch 下载 (CFG_WMT_MULTI_PATCH 路径, L1058-1108)
```
6.1 mtk_wcn_soc_patch_info_prepare() (L1060) ──► WMT_CTRL_PATCH_SEARCH
       └► wmt_ctrl_patch_search() (wmt_ctrl.c:489) ──► wmt_ctrl_ul_cmd("srh_patch")     ★ 阻塞等 launcher 应答(2s)
       └► launcher cmd_hdr_sch_patch() 回灌 patch 信息(见 §2) ──► 内核 pDev.patchNum / pWmtPatchInfo[]
6.2 WMT_CTRL_GET_PATCH_NUM (L1069) ──► patch_num = pDev->patchNum
       (日志 "patch total num = [%d]" — 0 说明 launcher 没扫到任何 ROMv1_patch 文件)
6.3 (CFG_WMT_PATCH_DL_OPTM) set_mcuclk_table_1/3 提高 CONN MCU 时钟
6.4 for patch_index in 0..patch_num-1:
       ├─ mtk_wcn_soc_patch_dwn(index) (L1086, 定义 L2020):
       │    a. WMT_CTRL_GET_PATCH_INFO (L2051): name + launcher 给的 addRess[4]
       │    b. WMT_CTRL_GET_PATCH (L2059): firmware loader 读 patch 文件到内存
       │    c. 发 WMT_PATCH_ADDRESS_CMD (L2144, {01 08 10 00 ...}, 含 patch RAM 基址,
       │       0x6752/0x8127/0x7623/0x6571/0x0326/0x0321/0x0335/0x0337/0x8163/0x6580 → [8]=0x40)
       │    d. 发 WMT_PATCH_P_ADDRESS_CMD (L2170, 把 launcher addRess[4] 拷到 [12..15])
       │    e. 分片循环 (L2196-2260): 每片 WMT_PATCH_CMD 头 {01 01 00 00 flag}(flag=FRAG_1ST/MID/LAST)
       │        + 片体(默认 DEFAULT_PATCH_FRAG_SIZE), 每片等 WMT_PATCH_EVT
       │    f. WMT_CTRL_FREE_PATCH (L2270)
       └─ wmt_core_init_script(init_table_3) (L1091): WMT_RESET_CMD {01 07 01 00 04}
            └─ "wmt reset" — 复位 CONN MCU 让刚下的 patch 生效 (每个 patch 后一次)
6.5 (CFG_WMT_PATCH_DL_OPTM) set_mcuclk_table_2/4 降回 MCU 时钟
```

### 1.3 之后 (L1125-1369)
- (CFG_WMT_READ_EFUSE_VCN33) 读 eFuse 调 PMIC VCN33 电压
- (CFG_WMT_FILTER_MODE_SETTING) wifi/lte coex 脚本
- PALDO on → `calibration_table` RF 校准 (L1180) → PALDO off
- `wmt_stp_init_coex()` (L1196)
- (CFG_WMT_CRYSTAL_TIMING_SET) 晶振微调
- osc_type / merge_pcm 脚本
- `init_table_5_1` FM strap (L1233) — 用 `pWmtHifConf->au4StrapConf[0]`(launcher SET_STP_MODE 的 fm 位设置)
- `WMT_CTRL_SET_STP_DBG_INFO` (L1348)
- (CFG_WMT_PS_SUPPORT) `wmt_lib_ps_enable()`
- return 0 → 回到 `wmt_core_stp_init` 置 STP_RDY (wmt_core.c:709) → WMT DRV_STS_FUNC_ON (wmt_core.c:936)

---

## 2. launcher (`stp_uart_launcher.c`) 开机流程

### 2.1 `-m 3` 参数含义
- usage (L845-879): `-m (BT/GPS/FM common interface mode selection)`; `-3: BTIF mode`。
- 枚举 `STP_MODE` (L68-75): `STP_UART_FULL=0x1, STP_UART_MAND=0x2, STP_BTIF_FULL=0x3, STP_SDIO=0x4`。
- **SOC 芯片(含 0x6572)默认就强制 `gStpMode = STP_BTIF_FULL` (L1439), `-m 3` 只是显式确认**。`-p /etc/firmware` 给 patch 目录。

### 2.2 main() 启动顺序 (L1390)
```
1. 循环 open /dev/stpwmt (CUST_COMBO_WMT_DEV) 直到成功            (L1405-1413)
2. query_chip_id(): 先读属性 persist.mtk.wcn.combo.chipid;
   无则 ioctl WMT_IOCTL_WMT_QUERY_CHIPID
       └─► mtk_wcn_wmt_chipid_query()  (Z1 fork: mt6572/plat_stub.c:35)
             └─► return PLATFORM_SOC_CHIP = 0x6572  (硬编码 SoC id, 不读 CONNSYS)
   └─► 0x6572 ∈ SOC 集合 → 走 SOC chip flow                                 (L1436-1468)
3. SOC flow: gStpMode = STP_BTIF_FULL; getopt 解析 -m/-p                      (L1442-1463)
4. ioctl WMT_IOCTL_SET_PATCH_NAME gPatchName(=空)                            (L1465)
5. ★ ioctl WMT_IOCTL_SET_STP_MODE ((gFmMode&0x0F)<<4 | (gStpMode&0x0F))
       gFmMode=2(comm I/F), gStpMode=3(BTIF_FULL) → 实参 = 0x23              (L1467)
       └─► 内核 wmt_dev.c:1875 → wmt_lib_set_hif(0x23) (见 §4)
             └─► hifType=WMT_HIF_BTIF + g_stp_if_type=STP_BTIF_IF_TX + au4StrapConf[0]=WMT_FM_COMM
             └─► 入队 WMT_OPID_HIF_CONF op (wmt_dev.c:1890-1907, pSignal->timeoutValue=0 不等结果)
   ★★★ 这是在 main() 里同步做的 —— 严格先于下面的 pwr_on 线程 ★★★
6. ioctl WMT_IOCTL_SET_LAUNCHER_KILL 0                                       (L1641)
7. pthread_create(launcher_pwr_on_chip)                                       (L1643)
       └─► 线程: ioctl WMT_IOCTL_LPBK_POWER_CTRL 1
             └─► mtk_wcn_wmt_func_on(WMTDRV_TYPE_LPBK) → WMT_OPID_FUNC_ON
             └─► wmtd 线程 FIFO: 先处理第5步的 HIF_CONF op, 再处理本 FUNC_ON op
             └─► opfunc_func_on → WMT 未 on → opfunc_pwr_on:
                   wmt_plat_pwr_ctrl(FUNC_ON) → wmt_core_stp_init() → hw_check → sw_init(ROM patch)
                   → STP_RDY → WMT FUNC_ON      (失败重试 20×1s, L1349-1360)
       └─► 成功后设 service.wcn.driver.ready=yes                            (L1362-1373)
8. 主循环 poll /dev/stpwmt (L1676-1734): 收内核 ul_cmd 字符串("srh_patch")
       → handle_cmd() → cmd_hdr_sch_patch() → 回写 "ok" / "resp_%d"
```

### 2.3 cmd_hdr_sch_patch() 何时被触发 + 如何与内核握手 (L498, MULTI_PATCH 版)
- **触发时机不是 launcher 自己定时, 而是内核 `sw_init` 里 `patch_info_prepare()` 主动发来的**。链路:
  ```
  内核 mtk_wcn_soc_patch_info_prepare() (wmt_ic_soc.c:2009)
    └─► wmt_ctrl(WMT_CTRL_PATCH_SEARCH) → wmt_ctrl_patch_search (wmt_ctrl.c:489)
    └─► wmt_ctrl_ul_cmd(pDev, "srh_patch") (wmt_ctrl.c:372)
         ├─ 写 gDevWmt.cCmd="srh_patch", 置 WMT_STAT_CMD, 触发 cmdReq 事件
         └─ 阻塞等 cmdResp 信号 (2s 超时)
    launcher poll /dev/stpwmt 得 POLLIN → WMT_read() (wmt_dev.c:1797)
    └─► wmt_lib_get_cmd() 取走 "srh_patch" → handle_cmd → cmd_hdr_sch_patch
  ```
- **cmd_hdr_sch_patch 内部**:
  1. `chipId = ioctl(gWmtFd, WMT_IOCTL_GET_CHIP_INFO, 0)` → `wmt_lib_get_icinfo(WMTCHIN_CHIPID)` = `gDevWmt.chip_id` —— 注意这是 **hw_check 之后**才被 `WMT_CTRL_HWIDVER_SET` 填好的(wmt_ctrl.c:863, ver_check 里 L1629-1635 写入)。因此 **launcher 必须在 func-on 完成 hw_check 之后才扫固件**。
  2. 0x6572 → 走 SOC 分支, `chipName = "ROMv1"` 再拼 `"_patch"` (L605-616) → 前缀 `ROMv1_patch`。
  3. `fwVersion = ioctl(WMT_IOCTL_GET_CHIP_INFO, 2)` → `WMTCHIN_FWVER` = `gDevWmt.fw_ver`(ver_check 读 GEN_FVR 0x80000004 的 ROM 版本)。
  4. `opendir(pPatchPath, 默认 /etc/firmware)`; 对每个 `ROMv1_patch*` 文件:
     - open → lseek(22) → 读 2 字节 `patchVer`; `if ((patchVer^fwVersion)&0xFF==0)` (ROM 主版本匹配才收)
     - 读 4 字节 `patchInfo[0..3]`: `gPatchNum = (patchInfo[0]&0xF0)>>4`; `gDwonSeq = patchInfo[0]&0x0F`
     - 第一个匹配: `ioctl(WMT_IOCTL_SET_PATCH_NUM, gPatchNum)` → 内核 kcalloc patchInfo 数组 (wmt_dev.c:2103-2113)
     - 每个匹配: `ioctl(WMT_IOCTL_SET_PATCH_INFO, &gStpPatchInfo)` (patchName + addRess[4] + dowloadSeq) → 内核存 `pPatchInfo+seq-1`; 收齐 patchNum 个 → `wmt_lib_set_patch_info()` (wmt_dev.c:2115-2147)
  5. 回写 "ok" → 内核 `WMT_write` → `wmt_lib_trigger_cmd_signal(0)` → `wmt_ctrl_ul_cmd` 返回 0 → sw_init 继续。
- 之后内核自取: `WMT_CTRL_GET_PATCH_NUM`(patch_num) → 每个 patch `GET_PATCH_INFO`(拿名字+地址) → `GET_PATCH`(firmware loader 读文件) → 分片下发。

---

## 3. 关键: hw_check 依赖 CONN MCU ROM STP 引擎吗?

**是, 绝对依赖。** 证据链:

1. **读法**: `wmt_core_hw_check()` (wmt_core.c:781-795) 用 `wmt_core_reg_rw_raw(0, GEN_HCR, &chipid, GEN_HCR_MASK)` 读 `GEN_HCR = GEN_CONFG_BASE(0x80000000)+0x8 = 0x80000008`。实现 (wmt_core.c:543-589): 构造 20 字节 `WMT_SET_REG_CMD`(op=2 读, addr=0x80000008) → `wmt_core_tx()` 经 STP 发给芯片 → **然后 `wmt_core_rx()` 阻塞等 `WMT_SET_REG_RD_EVT` 应答**(wmt_ctrl.c:247-276 等信号循环, 超时即 "get hwcode fail (-2)")。
2. **0x80000008 是 CONNSYS 内部寄存器空间**, 属于 CONN MCU 视界, 主机不能直读。必须由 CONN MCU ROM 固件执行这个 WMT SET_REG 读并回 EVT。
3. **ver_check 同样靠 ROM**: hw_check 里还调 `p_ops->ic_ver_check()` = `mtk_wcn_soc_ver_check()` (wmt_ic_soc.c:1593), 再读 `GEN_HVR`(0x80000000) 和 `GEN_FVR`(0x80000004), 同样是 STP 请求-应答。
4. **sw_init 的 BTIF 三连本身就是为确认 ROM STP 引擎活着**: `init_table_1_2` QUERY_STP-DEFAULT 是第一条命令, 若 CONN MCU ROM 没起来, 这条就挂 (return -2)。
5. **host 侧没有 ROM 引擎时命令根本发不出去**: f_mode=0 时 `mtk_wcn_stp_send_data()` (stp_core.c:2522/2558) 无分支命中, 返回 0, Tx 即失败 —— 见下。

### launcher 需要先于 func-on 发 SET_STP_MODE 吗? —— 需要, 三条 host 侧门禁

| host 侧门禁 | 谁满足 | 缺了会怎样 |
|---|---|---|
| `wmtInfoBit & WMT_OP_HIF_BIT` | **只有 launcher 的 SET_STP_MODE → HIF_CONF op → opfunc_hif_conf (wmt_core.c:862-880)** | `wmt_core_stp_init` L647-651 直接 `osal_assert(0)` + return -1 "no hif info!" |
| `hifType == WMT_HIF_BTIF` → `f_mode = MTKSTP_BTIF_MAND_MODE` | 同上 (wmt_core.c:669-673) | f_mode=0 → `mtk_wcn_stp_send_data` 无分支命中 return 0 → hw_check Tx REG_CMD fail |
| `g_stp_if_type ∈ {UART,SDIO,BTIF}` → `mtk_wcn_sys_if_tx` 有指向 | 原版: launcher SET_STP_MODE (`mtk_wcn_stp_set_if_tx_type(STP_BTIF_IF_TX)`); **本 fork 已改为默认 BTIF (stp_exp.c:74)** | 原版缺省 STP_MAX_IF_TX → mtk_wcn_sys_if_tx return -1 |

**内核源码注释就是铁证** (wmt_exp.c:100-110, 描述已修 bug):
> "FUNC_ON/FUNC_OFF op must NOT carry WMT_OP_HIF_BIT... clobbering the legal hifType=BTIF(2) that the launcher already wrote via the WMT_IOCTL_SET_STP_MODE -> HIF_CONF op. That made wmt_core_stp_init() skip MTKSTP_BTIF_MAND_MODE, so f_mode stayed 0 and mtk_wcn_stp_send_data() returned 0 (**hw_check Tx REG_CMD fail**). HIF conf is provided by the launcher's HIF_CONF op, not by func ops."

### func-on 正确前置条件清单
```
P1  CONNSYS 上电: opfunc_pwr_on → wmt_plat_pwr_ctrl(FUNC_ON)          (由 func-on 自己完成)
P2  launcher 已发 WMT_IOCTL_SET_STP_MODE(-m 3) 且内核已处理 HIF_CONF op
    → hifType=WMT_HIF_BTIF; wmtInfoBit 置 HIF bit; g_stp_if_type=BTIF; au4StrapConf[0]=FM_COMM
P3  STP_OPEN 完成 (BTIF 打开, stp_btif_if_tx 注册, rx parser 挂上)
P4  f_mode = BTIF_MAND 被 wmt_core_stp_init 设置
P5  STP_EN=1
P6  (由 P2-P5 保证) hw_check 的 SET_REG 读 0x80000008 能发出且收到 ROM 应答 → chip id = 0x6572
P7  sw_init: ROM STP 引擎应答 QUERY_STP-DEFAULT → SET_STP → 10ms → QUERY_STP
P8  launcher 存活并在 poll /dev/stpwmt(否则 "srh_patch" 无人接, patch_info_prepare 超时)
P9  /etc/firmware 里有 ROMv1_patch*.bin 且 ROM 主版本匹配(否则 patch_num=0, 无 patch 可下)
P10 sw_init 下完 patch → STP_RDY → WMT FUNC_ON → 各 func 自己的 func_on(如 wifi→wlan probe→WIFI RAM code)
```
**坑提示**: 本 fork 只修了 g_stp_if_type 默认值这一条门禁 (P2 的第 3 项)。P2 的前两项(hifType + wmtInfoBit)仍**必须**由 launcher SET_STP_MODE 提供 —— 单靠默认 BTIF 不够。若启动时 `echo 1 > /dev/wmtWifi` 早于 launcher 的 SET_STP_MODE ioctl, func-on 仍会失败。

---

## 4. `wmt_lib_set_hif()` (wmt_lib.c:393-429) + g_stp_if_type 默认值

### 4.1 wmt_lib_set_hif(unsigned long hifconf)
实参 = `((gFmMode&0x0F)<<4) | (gStpMode&0x0F)`; 对 Z1 = `(2<<4)|3 = 0x23`
```
val = hifconf & 0xF:
  1(STP_UART_FULL) → pHif->hifType = WMT_HIF_UART(0);  au4HifConf=val>>8;       set_if_tx_type(STP_UART_IF_TX)
  4(STP_SDIO)      → pHif->hifType = WMT_HIF_SDIO(1);                           set_if_tx_type(STP_SDIO_IF_TX)
  3(STP_BTIF_FULL) → pHif->hifType = WMT_HIF_BTIF(2);                           set_if_tx_type(STP_BTIF_IF_TX)   ★Z1
  其他            → warn + set_if_tx_type(STP_MAX_IF_TX); return -1
val = (hifconf & 0xF0)>>4:
  1(WMT_FM_I2C)  → au4StrapConf[0]=WMT_FM_I2C
  2(WMT_FM_COMM) → au4StrapConf[0]=WMT_FM_COMM                                  ★Z1 (L427 日志 "new hifType: 2, fm:2")
  其他           → return -2
```
枚举: `WMT_HIF_UART=0, WMT_HIF_SDIO=1, WMT_HIF_BTIF=2` (wmt_core.h:101-103); `WMT_FM_I2C=1, WMT_FM_COMM=2` (wmt_core.h:95-96)。STP mode bit: `MTKSTP_UART_FULL=bit0, UART_MAND=bit1, BTIF_FULL=bit2, BTIF_MAND=bit3, SDIO=bit4` (stp_core.h:97-101)。

### 4.2 g_stp_if_type 默认值 (stp_exp.c)
- **原版 MTK**: `static ENUM_STP_TX_IF_TYPE g_stp_if_type = STP_MAX_IF_TX;` — 非法值, 必须靠 launcher SET_STP_MODE 设成 BTIF 才能走通。
- **本 fork (2026-08-14 "CONNSYS stability fix C")**: 已改成 `STP_BTIF_IF_TX` (stp_exp.c:74, 注释: "MT6572 SOC has BTIF as the only HIF. Default g_stp_if_type to BTIF so mtk_wcn_sys_if_tx() dispatches to stp_btif_if_tx even before the launcher's SET_STP_MODE ioctl runs")。
- 但 `stp_btif_if_tx` 本身要 `mtk_wcn_consys_stp_btif_open()` 才注册 (stp_btif.c:88, 由 STP_OPEN 触发), 所以默认值只消除"调度门禁", 不消除"STP_OPEN 门禁"和"f_mode/hifType 门禁"。

---

## 5. 完整时序图

```
时间轴 ──────────────────────────────────────────────────────────────────────────►

【launcher 进程】                          【内核 wmtd 线程】                       【CONN MCU ROM】
───────────────────                        ─────────────────                       ─────────────
open /dev/stpwmt
query_chip_id → 0x6572 (硬编码)
gStpMode=STP_BTIF_FULL
ioctl SET_PATCH_NAME("")
★ ioctl SET_STP_MODE(0x23)  ──────────►  wmt_lib_set_hif: hifType=BTIF,
│                                          g_stp_if_type=BTIF, strap=FM_COMM
│                                          (异步) 入队 HIF_CONF op ──► opfunc_hif_conf:
│                                                                       wmtHifConf=BTIF, wmtInfoBit|HIF_BIT
ioctl SET_LAUNCHER_KILL 0
pthread pwr_on_chip:
ioctl LPBK_POWER_CTRL 1 ─────────────► 入队 FUNC_ON op (FIFO, 在 HIF_CONF 之后)
│                                        opfunc_func_on → WMT 未 on → opfunc_pwr_on
│                                          wmt_plat_pwr_ctrl(FUNC_ON)  [CONNSYS 电源门开]
│                                          └─► wmt_core_stp_init:
│                                              STP_OPEN ─► BTIF 打开, tx/rx 挂好
│                                              EN=0/RDY=0
│                                              MODE=BTIF_MAND ─► f_mode=bit3   [P4]
│                                              EN=1                             [P5]
│                                              └─► hw_check ── SET_REG读0x80000008 ──► ROM STP 引擎收到
│                                                     ◄── RD_EVT(0x6572) ────────── ROM WMT 处理器回
│                                                     ├─ ver_check: 读 0x80000000/0x80000004 (ROM ver)
│                                                     │    └► HWIDVER_SET → gDevWmt.chip_id/fw_ver 填好
│                                                     └─► sw_init (MTK_WCN_MULTI_PATCH):
│                                                          ├─ QUERY_STP-DEFAULT ──► ROM 答(默认配置)  [P7]
│                                                          ├─ SET_STP(BTIF)     ──► ROM 切 full-set
│                                                          ├─ host MODE=BTIF_FULL + EN=1; sleep 10ms
│                                                          ├─ QUERY_STP ──► ROM 报 DF 0E 68 01
│                                                          ├─ patch_info_prepare:
│                                                          │    发 "srh_patch" ──►
│  poll /dev/stpwmt 得 "srh_patch" ◄──────────────┘
│  cmd_hdr_sch_patch:
│    GET_CHIP_INFO→0x6572, GET_CHIP_INFO(fwver)
│    扫 /etc/firmware: ROMv1_patch*.bin
│    ioctl SET_PATCH_NUM / SET_PATCH_INFO ──► 内核存 pPatchInfo[]
│  回写 "ok" ─────────────────────────────► 触发 cmdResp 信号, patch_info_prepare 返回
│                                                          ├─ GET_PATCH_NUM → patch_num
│                                                          ├─ (提 MCU 时钟)
│                                                          ├─ loop each patch:
│                                                          │   ├─ GET_PATCH_INFO(name+addr)
│                                                          │   ├─ GET_PATCH(读文件)
│                                                          │   ├─ PATCH_ADDRESS_CMD ──► ROM
│                                                          │   ├─ PATCH_P_ADDRESS_CMD ──► ROM
│                                                          │   ├─ 分片 PATCH_CMD*N ──► ROM 每片回 EVT
│                                                          │   └─ init_table_3: WMT_RESET ──► ROM 复位生效 [P9]
│                                                          ├─ (降 MCU 时钟) / RF 校准 / coex / FM strap
│                                                          └─ STP_RDY ──► WMT DRV_STS_FUNC_ON
│  设 service.wcn.driver.ready=yes
│
│  (稍后, 单独路径)
echo 1 > /dev/wmtWifi ─────────────────► WIFI FUNC_ON: WMT 已 FUNC_ON → 跳过 pwr_on
│                                         └─► wmt_func_wifi_on → mtk_wcn_wlan_probe()
│                                              └─► mt6620 wlan 驱动自身下载 WIFI_RAM_CODE
│                                                  (经同一 STP/BTIF 通道, WMTDRV_WIFI task)
```

### 关键握手小结
- **patch 信息是"内核拉, launcher 推"**: 内核 `sw_init` → `WMT_CTRL_PATCH_SEARCH` → 文本命令 `srh_patch`(经 /dev/stpwmt 字符设备) → launcher `cmd_hdr_sch_patch` → `SET_PATCH_NUM`/`SET_PATCH_INFO` ioctl 回灌 → 内核 `GET_PATCH_NUM`/`GET_PATCH_INFO`/`GET_PATCH` 消费。launcher 不是主动下载固件。
- **launcher 扫固件依赖 hw_check 结果**: `WMT_IOCTL_GET_CHIP_INFO` 读的 `gDevWmt.chip_id` 要等 ver_check 里 `HWIDVER_SET` 才被填。所以即使 launcher 进程早就活着, patch 扫描也必须等到内核 func-on 走到 sw_init 发 "srh_patch" 之后才发生。
- **ROM patch 只下一次**: launcher 的 LPBK func-on 把 CONNSYS 完全初始化(WMT FUNC_ON)后, 后续任何 func(WIFI/BT)的 func-on 不再重跑 pwr_on/ROM patch(wmt_core.c:1021-1022 检查 WMTDRV_TYPE_WMT 已是 FUNC_ON 则跳过)。

---

## 6. 源码定位表

| 关注点 | 文件:行 |
|---|---|
| sw_init 入口 + BTIF 三连 + patch 循环 | conn_soc/core/wmt_ic_soc.c:980 (L1014-1123) |
| patch_info_prepare | wmt_ic_soc.c:2009 |
| patch_dwn 分片下发 | wmt_ic_soc.c:2020 (L2144-2272) |
| 命令数组 (QUERY_STP/SET_STP/RESET/PATCH_*_ADDRESS) | wmt_ic_soc.c:129-161, 197-198 |
| wmt_core_stp_init (STP_OPEN→hw_check→sw_init) | wmt_core.c:636-714 |
| hw_check (读 0x80000008) | wmt_core.c:781-860 |
| wmt_core_reg_rw_raw (SET_REG 请求-应答) | wmt_core.c:543-589 |
| opfunc_pwr_on / opfunc_func_on | wmt_core.c:882-945 / 990-1056 |
| "srh_patch" ul_cmd 收发 | wmt_ctrl.c:489-502 (patch_search), 372-409 (ul_cmd) |
| SET_PATCH_NUM / SET_PATCH_INFO ioctl | linux/pri/wmt_dev.c:2103 / 2115 |
| SET_STP_MODE ioctl → wmt_lib_set_hif + HIF_CONF op | wmt_dev.c:1875-1909 |
| wmt_lib_set_hif | core/wmt_lib.c:393-429 |
| g_stp_if_type 默认值 (fork 改过) | linux/pri/stp_exp.c:70-74 (原版 STP_MAX_IF_TX → fork STP_BTIF_IF_TX) |
| f_mode 分支 / set_mode | core/stp_core.c:2824-2829, 2522-2621 |
| STP 模式 bit 定义 | core/include/stp_core.h:97-101 |
| launcher main + SOC flow + SET_STP_MODE | tools/launcher/stp_uart_launcher.c:1390, 1436-1467 |
| launcher cmd_hdr_sch_patch | stp_uart_launcher.c:498-720 |
| launcher pwr_on 线程 (LPBK func-on) | stp_uart_launcher.c:1338-1379 |
| chipid_query (Z1 fork 硬编码 0x6572) | conn_soc/mt6572/plat_stub.c:35 |
| wifi func-on → wlan probe (RAM code 入口) | core/wmt_func.c:640-675 |

### 给主会话的要点(避免踩坑)
1. **launcher 必须用 `-m 3`(BTIF)且必须先于任何 func-on 发 SET_STP_MODE**; fork 里 g_stp_if_type 默认已改 BTIF, 但 hifType/f_mode/wmtInfoBit 三条门禁仍只认 launcher 的 HIF_CONF op。
2. **hw_check 失败 ≠ 驱动 bug, 先查 launcher 是否已配 BTIF + CONN MCU ROM 是否应答**; 若 CONNSYS 电源门没真正打开(wmt_plat_pwr_ctrl 桩), 0x80000008 读会超时。
3. **"patch total num = [0]" 日志 = launcher 没扫到 ROMv1_patch*.bin 或 ROM 版本不匹配**, 不是下载失败——先查 /etc/firmware 文件与前缀/偏移 22 的版本字节。
4. **时序上 launcher 扫固件必然晚于 hw_check**(依赖 GET_CHIP_INFO 里 gDevWmt.chip_id), 所以"launcher 还没扫 patch"在开机早期是正常的, 只要 func-on 走到 sw_init 它就会被 "srh_patch" 唤醒。
5. WIFI RAM code 是 wlan 驱动(mt6620)在 WMT FUNC_ON 之后自己下的, 与 ROM patch 不同阶段、同一条 STP/BTIF 通道。
