# 原内核 Android 9 S1H

S1G 实机 USB 日志证明 SurfaceFlinger 已完成 EGL/OpenGL 初始化；SystemServer 因 Z1StockBattery 在系统 provider 安装前读 Settings 而循环重启，sys.boot_completed 尚未置位。

S1H 修复仅延后 provider 依赖的放电诊断到 PHASE_SYSTEM_SERVICES_READY，保留早期真实电池采样、BatteryStats、低电与过热保护。独立 Java 检查稳定复现旧异常，新代码 12 项通过。services.jar、对应 .prof、art/odex/vdex 全部由本次成功构建生成，离线写入保留 uid/gid/mode/SELinux label。原内核不支持的两个远程音频 HAL restart 钩子已注释，保留本地 audioserver。

镜像 e2fsck、init/ELF/JAR 闭包、24 份源码补丁校验通过。boot.img 与已刷 S1F/S1G 的 e9b4e412... 完全相同；仍使用真实原厂 3.4.67 内核。

## 手动刷写

SP Flash Tool 使用之前同一份已成功使用的 scatter，Download Only，只勾选 ANDROID，选择本目录 system.img。保留现有 boot 与 userdata。刷完后通知我准备采集，再插线开机。

## 验证状态

这是待实机验证的修复候选，尚未证明进入桌面。仍使用 SwiftShader 软件渲染和 permissive SELinux，原内核缺失 hwbinder 使部分硬件服务不可用；不能宣称已可流畅日常使用。

S1G 原始回滚镜像已无损归档在 bridge_backups/products/products_stock_s1g_final_20261004_012842/system.img.xz，解压 SHA256 e35ec82ed8b559dfb5a2769113a3b29518f100a4aeb0e91a3a2cf171898bf838 已核对。

实机证据：mainline_recon/z1_android9_acm_20261004_025506_326692.log，live lines 4784–4808 首次电池线程 fatal，后续约每 16 秒重复相同异常。
