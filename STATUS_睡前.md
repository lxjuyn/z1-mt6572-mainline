# 佳伯 Z1 移植 — 睡前状态(2026-07-07 02:4x)

## 已完成(本会话)
- ✅ mtkclient 链路验证:adb reboot 触发后 mtk 一把抓住 preloader 窗口,读出完整 GPT
  (存档: out/mtk_probe_1783363131.log)
- ✅ GPT 分区表确认(刷写点):
    uboot    @0x2920000  0x60000  (384KB) ← 刷 lk_Z1.bin
    bootimg  @0x2980000  0x600000 (6MB)   ← 刷 boot_linux.img
    recovery @0x2f80000  0x600000 (6MB)   ← 刷 twrp-3.1.1-0-Z1.img
- ✅ boot_linux.img 核对(路线③产物确认):
    Linux 3.4.67 (hongyao@hongyao-PowerEdge-R630, gcc 4.7) #1 SMP PREEMPT 2022-08-23
    cmdline: console=tty0 console=ttyMT1,921600n1 rdinit=/init androidboot.selinux=permissive
    ramdisk: 1069377 B (自带 initramfs, USB ACM 切串口)
    → 同源 3.4.67 hongyao 内核, 可用
- ✅ 代理守护修复:桌面系统代理改回 none,proxy-guardian.py 两个劫持函数掏空 no-op
    mihomo 仍在跑 + 自动选节点 + 开机自启, 但不再劫持桌面联网.
    备份: ~/proxy-guardian.py.bak.1783361051
- ✅ 工具脚本: tools/flash_with_retry.sh (带重试+回滚防变砖, bootloader强制要求回滚镜像)

## 卡点(为什么没继续刷)
- SP Flash Tool v5.1916 在本系统启动 segfault (Qiinit报 support_dl_cert 后崩), GUI 用不了
  → 按你指示 fallback 到 mtkclient 刷写路径
- mtkclient 上次写 bootimg 在 50% 半写(Data ack failed), 现已回滚 OK
  → 384KB lk_Z1.bin 小目标重试成功率高, 但 uboot=bootloader 变砖风险大
- 第一次写 uboot 分区是变砖权益点, 你睡前我不能盲刷

## 你醒来一句话就能推进:
  "刷 uboot"  → 我跑: tools/flash_with_retry.sh uboot out/lk_Z1.bin <需回滚镜像>
  (注意: uboot 没有天然"现役原版", 回滚镜像要确认来源; 或先用 twrp 这种有 live_recovery 兜底的)

  更稳的醒后第一步其实是先刷 recovery(有 live_recovery 完整兜底, 半写也救得回):
  "刷 recovery" → tools/flash_with_retry.sh recovery out/twrp-3.1.1-0-Z1.img out/live_recovery.img

## 备份齐全(救机用)
  out/live_boot.img      6MB  现役原版 boot
  out/live_recovery.img  6MB  现役原版 recovery
  out/MT6572_Android_scatter.txt  分区表
  firmware_backup/       厂商完整备份
