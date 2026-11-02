# FM 移植笔记（Z1 · MT6572 CONSYS FM / MT6627N RF）

2026-11-02，FM 移植员工作流产物。模块名 `mtk_fm_drv.ko`，字符设备 `/dev/fm`。

## 1. 硬件定案（纠正任务简报的前提）

任务简报假设 Z1 = 外挂 MT6620 combo（SDIO WLAN + UART STP）。**证据否定了这个假设**：

- `mainline_recon/MT6572_CONNSYS_AUDIT_20260717.md:5-10`：Z1 用 **MT6572 SoC 内置 CONNSYS**，
  原厂 `mediatek/config/cci72_we_jb3/ProjectConfig.mk`：`MTK_COMBO_CHIP=MT6572_CONSYS`、
  **`FM=MT6627_FM`**；"external MT6620 combo chip" 不存在；`wlan_mt6620.ko`/`mtk_hif_sdio.ko`/
  STP-UART 只是编译参考，已被 Z1 淘汰。
- 上游 `gabin8/mt6572-connsys-modules` README（Architecture 节）：MT6572 的 WiFi/BT/GPS/FM
  MAC/基带在 die 内 CONSYS，**外挂 MT6627N 只是 RF 收发器**，"there is no SDIO/UART combo
  bus"。同硅片机器：Prestigio PAP5500 DUO、Lenovo A369i。
- Z1 现行 R4/R5 radio 链（`HANDOFF_20261028.md:166,350`）用的就是该上游的 soc 系模块
  （mtk_btif_drv / mtk_stp_wmt_soc / mtk_stp_bt_soc / mtk_wmt_wifi_soc / wlan_gen2），
  且 `bluetooth@1.0-service` 注册成功 = BTIF+WMT 控制面板上已验证。

因此：**FM 芯片库用 MT6627，不是 MT6620**。本目录 vendor 的就是上游 fmradio/（chips/mt6627）。

## 2. 传输通道定案（任务问题 1）

FM 的命令/寄存器/RDS **全部走 WMT/STP 控制面，over BTIF**；无独立 FM 总线、无 SDIO function：

- 芯片无关层 `core/fm_link.c`（上游与 KK 版同构）：命令 `fm_cmd_tx()` →
  `mtk_wcn_stp_send_data(buf, len, FM_TASK_INDX)`；事件/RDS `fm_event_parser()` →
  `mtk_wcn_stp_receive_data(..., FM_TASK_INDX)`；RX 到达回调
  `core/fm_eint.c: fm_request_eint()` → `mtk_wcn_stp_register_event_cb(FM_TASK_INDX, parser)`。
  FM_TASK_INDX=1（上游 `conn_soc/include/stp_exp.h:40`；与 KK 世代编号一致，已 diff 核对）。
- 寄存器读写 `core/fm_cmd.c`：`fm_reg_read/write`(FSPI)、`fm_top_reg_read/write`(CSPI)、
  `fm_host_reg_read/write`（如 legacy_wmt.c 里切 SPI 时钟用的 0x81026004）全部封成 STP
  命令包经 `fm_cmd_tx` 进 CONSYS 固件，由固件代访 FM DSP/RF 寄存器。
- 电源：`mtk_wcn_wmt_func_on/off(WMTDRV_TYPE_FM)`（WMT FUNC_CTRL，含 WMT patch 下载路径，
  由已部署的 mtk_stp_wmt_soc.ko + mtk_stp_launcher 负责）；整芯复位回调
  `mtk_wcn_wmt_msgcb_reg(WMTDRV_TYPE_FM, ...)`（plat/legacy_wmt.c:77-83）。
- 物理承载：BTIF 0x1100E000 + APDMA VFF 0x11000380/0x11000400（dts `btif@1100e000`，
  mt6572-z1.dts 已有），WDT/wakeup IRQ 122/124。

### 音频出口

CONSYS **数字输出**：WMT audio-interface 命令把芯片置 FM-I2S 模式，CONSYS 以 master 32kHz
驱动 AFE 的 I2S 输入，AFE 经 ASRC 重采样到 44.1kHz 进下行链路（扬声器/耳机），AWB memif
可录音。本树 AFE 侧已就绪（上游 fm.md 的 kernel prerequisite 已满足）：

- `sound/soc/mediatek/mt6572/mt6572-afe-pcm.c:6-7,95,171,613-621,665`：
  `FM I2S In` → ASRC → `FM Playback` switch（`FM Playback Switch`）→ DL1 下行；
  `FM Playback Volume`（fm_gain）；AWB capture（card0 dev1 "AWB Capture"，44.1k S16 stereo）。
- `mt6572-mt6323.c:61-62`：AWB Capture DAI 无 codec。
- 非模拟→PMIC AUX 路线（那是 MT6620 外挂时代 `FM_AUD_ANALOG` 选项，KK
  `mt6620_fm_config.c:179/185` 两种 aud_path 的出处；本硅片音频出 CONSYS 只有数字 I2S）。

## 3. 依赖清单（任务问题 2）与 7.0 对应

上游模块是 MediaTek conn_soc 家族的前向移植（经 4.14 BPI 路线 + linux-next 7.2-rc4 验证），
3.4 API 已在源内替换完毕，无需逐个再适配：

| 3.4 时代 API | 上游模块现状（7.0-rc7 适用性） |
|---|---|
| `get_fs/set_fs(KERNEL_DS)` + filp_open 读 patch（KK fm_patch.c 即此病） | 已换 `request_firmware()`（core/fm_patch.c:35，kthread 内），7.0 原生 |
| `class_create(THIS_MODULE, name)` 6.4 改签名 | 已是单参 `class_create(FM_NAME)`（core/fm_module.c:1451） |
| WMT/STP 导出符号（mtk_wcn_stp_send_data/receive_data/register_event_cb/register_tx_event_cb/is_ready/is_rxqueue_empty/parser_data、mtk_wcn_wmt_func_on/off、msgcb_reg/unreg、hwver_get、therm_ctrl、dsns_ctrl、chipid_query、ic_info_get、assert） | 本树已部署 `out/z1_wmt_emi_stage/mtk_stp_wmt_soc.ko` 全部导出（nm 实测 113 个 mtk_wcn* T 符号，逐一核对 YES）；本树根 Module.symvers 存在；CONFIG_MODVERSIONS=n → insmod 纯按名解析，无 CRC 风险 |
| `mach/mt_gpio.h` / GPIO EINT（KK fmradio 时代） | mt6627_fm_lib.c:35 已注释该 include；fm_eint.c 只注册 STP 回调，无真实 GPIO |
| `mtk_wcn_cmb_stub_audio_ctrl`（KK 名） | 上游 plat/legacy_wmt.c 不再调用它；音频路径改由 WMT audio-interface 命令+AFE 开关完成，stub 依赖消失 |
| workqueue/hrtimer/proc | kthread + request_firmware + `proc_create("/proc/fm")`（fm_module.c），7.0 原生 |

外编注意（项目既有约定）：`KBUILD_MODPOST_WARN=1` 绕过 modpost 跨模块符号告警；
`M=` 外编不读根 .config autoconf，本模块源内无 `#if CONFIG_*` 撞墙点（上游已消化）。

## 4. 移植实施（本目录构成）

- `core/ inc/ plat/ chips/mt6627/ NOTICE`：**逐字节原样** vendor 自
  `github.com/gabin8/mt6572-connsys-modules` branch main（api.github.com tarball，
  2026-11-02 取）。上游 fmradio 的 Kbuild 用 `-I$(src)/../conn_soc/...` 兄弟目录——
  单目录 M= 移植不可达，故把五个头文件目录（headers only，无 .c）原样重挂到
  `conn_soc_inc/{conn_soc/include,conn_soc/linux/include,conn_soc/core/include,
  compat_inc,detect_inc}`，仅改 Kbuild 的 -I 前缀（见 Kbuild 头注释）。
- `Kconfig`：新符号 `MTK_CONNSYS_FM`（tristate）。**不接入父级 Kconfig**——本目录不被任何
  父 Makefile/Kconfig 引用，全量内核构建不会碰它；将来要内建时 source 本文件即可。
- 未做任何公共内核源码改动（git status 仅本 untracked 目录）。

## 5. 固件

FM DSP patch/coeff：`mt6627_fm_v{1,2,3,5}_{patch,coeff}.bin` 已从 Z1 原厂系统提取
（`out/firmware_backup/firmware/mt6627/`，原路径 `/system/etc/firmware/mt6627/`）。
上板需部署到 `/lib/firmware/`（request_firmware 的默认搜索路径；上游 fm.md 用
`/lib/firmware/mt6627_fm_v*_{patch,coeff}.bin`）。版本匹配：驱动按
`mtk_wcn_wmt_ic_info_get(WMTCHIN_HWVER)` 选 v<ver>，Z1 现有 v1/v2/v3/v5 覆盖。

## 6. 上板验证序列（见任务结果 onBoardTest）

前提：connsys 控制面已起（Z1 radio 链：btif → mtk_stp_wmt_soc → mtk_stp_launcher），
`/lib/firmware/mt6627_fm_*.bin` 就位，耳机插入（线即天线）。关闭 /dev/fm 即下电。

## 7. 未确认项 / 遗留

- 上游 fmradio 与 Z1 现部署的 mtk_stp_wmt_soc.ko（Oct 26-28 构建）之间的 STP 契约：符号名
  与 FM_TASK_INDX 已逐一核对一致（见 §3），但**未板上实测**——上游 README 的 FM "working"
  是在同硅片 PAP5500 DUO + 自家构建栈上验证的，Z1 尚未跑过 /dev/fm。
- FM 音频听感依赖 AFE FM 路径 + codec 下行路由（本树已具备代码；Z1 上未实测）。
- RDS 解析（fm_rds_parser.c）已随模块入内，fmctl `b` 可验；无需额外内核件。
- 应用层（fmctl/fm-up.sh/fm-record.sh 在上游 tools/，GPL）本轮未打包入 ramdisk/system。
