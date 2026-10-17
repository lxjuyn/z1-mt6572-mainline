# 最近进度 (恢复点) — 2026-07-11

## 当前任务
/goal: 完成 wifi 驱动的完整编译和其他驱动的编译

## WiFi 驱动
✅ wlan_mt6620.ko 已编通 (2.17MB). 备份在 `bridge_backups/products/products_common_4light_142850/`.

## 其他驱动 (connectivity/common) — 6 个 obj-m 模块
### 已编出 .ko (4 个, 已备份)
- mtk_stp_uart.ko (46K) — 改 tty_ldisc API
- mtk_stp_bt.ko (53K) — 删 set_fs/get_fs
- mtk_stp_gps.ko (46K) — 改 GPS_event_cb static→named
- mtk_wmt_wifi.ko (35K) — 干净
- 备份: `bridge_backups/products/products_common_4light_142850/`

### 重模块 (mtk_stp_wmt + mtk_hif_sdio) — 正在收尾
桩头补好了让 wmt_plat_alps/mtk_wcn_cmb_hw/stp_exp 等编过, 平台 GPIO/EINT 是 no-op 桩.

已编出的重模块 .o (18 个, 源树里): wmt_core/wmt_ctrl/wmt_func/wmt_ic_6620/wmt_lib/wmt_conf/wmt_dev/wmt_exp/wmt_tm(空桩)/osal/wmt_plat_alps/wmt_plat_stub/mtk_wcn_cmb_hw/stp_exp/btm_core + 4 轻 .o

## 历史卡点（旧 runtime 的 workflow 曾处理；不可在当前 Codex runtime 直接续跑）
- stp_dbg.c: genl API (GENL_ID_GENERATE/genl_register_ops/snd_pid) + do_gettimeofday
- 探测组: hif_sdio.c / hif_sdio_chrdev.c / mtk_wcn_cmb_stub_alps.c 未知错
- timer 类: stp_core.c / wmt_conf.c / wmt_ctrl.c (可能还有)

## 历史 workflow 记录（非当前可执行控制面）
旧 Claude Code runtime 曾启动 workflow（run ID: `wf_34e4368a-367`，task `wv14kr4th`）并行修剩余文件。
- 历史脚本: `/home/<user>/claude/6572/mainline_recon/common_fix_workflow.mjs`
- **不要在当前 Codex runtime 书写、复制或假装执行 `Workflow(...)`。**当前 runtime 未暴露该工具；应使用 `spawn_agent`、主会话计划、状态检查和重派完成等价编排。

## Makefile
common/Makefile 已恢复 obj-m (mtk_stp_wmt + mtk_hif_sdio 都开), 6628 那行已注释 (Z1 是 MT6620 不用 6628).

## 桩头 (已补, 在 stub_includes/)
- asm/system.h (含 barrier.h)
- cust_gpio_usage.h (所有 GPIO_COMBO_* 常量=0)
- cust_eint.h (CUST_EINT_*=0)
- mach/mt_gpio.h (mt_set/get_gpio_* no-op inline + CHIP_E1 + get_chip_eco_ver)
- mach/eint.h (mt65xx_eint_* no-op inline)
- mach/mtk_rtc.h (rtc_gpio_* no-op inline)

## 规则 (已写入 CLAUDE.md §0)
- 强制用 agent team（并行 fan-out + discover→fix→verify）；workflow 仅在 runtime 明确提供时才可用。
- 产物也要备份

## 编译命令
cd /home/<user>/claude/6572/fork_linux
timeout 590 make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
  M=drivers/misc/mediatek/connectivity/common \
  KBUILD_MODPOST_WARN=1 modules
