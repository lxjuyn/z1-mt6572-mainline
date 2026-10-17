# Z1 厂厂商固件料(direct dump from 1/system.img)

提料日期 2026-07-10。源: `mount -o loop,ro 1/system.img /tmp/z1sys`(需sudo, 密码1, 见 memory project-6572-z1-port)。备份到 `out/z1_factory_drv/`。

## 设备身份(从 build.prop, 铁证)
- `ro.product.model=Z1`, `name/device/board=j72_z1`, `manufacturer=alps`
- `ro.product.cpu.abi=armeabi-v7a` / `abi2=armeabi`
- `ro.mediatek.platform=MT6572`, `ro.mediatek.chip_ver=S01`
- `ro.build.version.release=4.4.2`, `sdk=19`, `ro.build.version.incremental=S13_V1.0`
- `ro.mediatek.version.branch=KK1.MP7`, `version.sdk=2`
- `ro.hardware.version=S13_MB_V1.3`
- 已编 kernel: `Linux 3.4.67 #1 SMP PREEMPT (gcc 4.7) hongyao@hongyao-PowerEdge-R630 Aug 23 2022`(见 mainline-z1-boot-test-result memory)

## 模块现状(关键!)
- `/lib/modules/wlan.ko` 只有 **8字节占位** → WiFi/BT/wmt 驱动 **静态编进 kernel **(MTK combo 常见), 系统内**没有可加载的 wlan.ko/wmt.ko/stp.ko/hci.ko**。
- 推论: 要在 3.4.67 Z1 上 insmod 外加 .ko, 必须**自编或 vermagic 完全相同**才能和厂商 builtin 协同 — 否则符号冲突/缺失都起不来。复用厂商 .ko 捷径走不通(根本没有可复用 .ko)。
- 看 mainline 是否能走 out-of-tree wlan.ko 取决于 Z1 kernel EMBEDDED_STA/BT�认的复用程度(可能要重编 kernel 留 hook)。见 [[mainline-build-z1-done]] / 编译 agent W2。

## 固件 blob (/etc/firmware, 完整)
| 文件 | 大小 | 用途 |
|---|---|---|
| WIFI_RAM_CODE_SOC | 160464 | WiFi 主固件(magic MTKW) |
| WIFI_RAM_CODE_E6 | 159264 | E6版本 |
| WIFI_RAM_CODE_MT6630 | 235760 | MT6630兼容 |
| WIFI_RAM_CODE_MT6628 | 157344 | MT6628兼容 |
| WIFI_RAM_CODE | 158784 | 兜底 |
| mt6627/ | (dir) | BT/FM patch子文件 |
| WMT_SOC.cfg | 80 | WiFi/BT共存配置 |
备份到 `out/z1_factory_drv/`(部分)。补全需重挂载拷。

## 用户态工具 (/system/bin, ARM32 ELF EABI5)
- `wmt_loader` 9440B — WMT 加载器, ARM ELF, interpreter `/system/bin/linker`(Android bionic) 用的, stripped. **不能直接 Alpine musl 跑**, 需按 memory 路线 musl 重编或自写。
- `wlan_loader` 5292B — WLAN 加载器
- 备份到 `out/z1_factory_drv/`
- 下一步看 init.rc / rc 文件找 wmt_loader 调用顺序 + args(没在系统init.rc找到, 可能在 lk 或 vendor 分区)。

## WiFi 路线结论
**drsami 实测: `VERSION=3 PATCHLEVEL=4 SUBLEVEL=5` = 3.4.5 (readme base Android 4.2.2)**, **不是 3.4.67**。与 Z1 真机 3.4.67 差 62 sublevel, vermagic 必不一致, 编出的 .ko 在 Z1 insmod 会被 vermagic 拒载。drsami **不是直接可用的 3.4.67 源树**。

两条路经济性更新:
- **路 A (W2, mainline编.ko) — 更靠谱, 优先**: fork_linux 本地齐, 编译 agent acd52126已啃到只差几个桩头(见 W2_BUILD_PROGRESS)。vermagic 是 fork_linux(7.0-rc7)的, 在 Z1 跑 mainline kernel 才 match — 与 [[mainline-z1-boot-test-result]] 卡 early init 配套(mainline Boot通后才轮到 WiFi)。
- **路 B (3.4.67厂商源树) — 源树难找, 后置**: drsami 不对齐(3.4.5), 要真 3.4.67+MT6572 源树得 web 搜 hongyao@PowerEdge 编译机署名 / GitHub MTK BSP。即便找到也得啃 combo 改写, 未必比 A 轻。且厂商已 builtin 没可复用.ko, 即编出也对 Z1 builtin 冲突(需 Z1 kernel 开 module hook)。

**推荐: 先押路 A**。路 B 暂留 web 调研低优先。

## 2026-07-10 19:40 路B调研更新(用户给 https://github.com/TestMT6572)
- **TestMT6572 = MT6572 device+vendor+framework BSP 集合**(LineageOS cm-14.1, Micromax A102/Explay JoyTV 都是 MT6572 设备), 22个 repo。
- **无 kernel 源 repo**(全 repo 无 kernel/mediatek_kernel 类名, manifest 里 device/vendor 全注释掉, kernel 走 prebuilt `device/kernel/{kernel文件}`)。MtkCamera 是另一个 fork-but-src。
- **可用料**: `android_vendor_Micromax_A102/proprietary/{bin,lib,etc,xbin,vendor}` — 厂商 blob/用户态/wpa_supplicant_8/system_bt 都在, 可补 Z1 侧料或交叉验证。`android_device_Micromax_A102/BoardConfig.mk` 确认 `TARGET_BOARD_PLATFORM=mt6572, OLD_KERNEL=true, "mt6572 version is 26"`(→kernel 内部 ABI 26 = Linux 3.4.x 系)。
- **kernel driver 源仍缺**: 路B真内核源还只有 drsami 3.4.5(差 Z1 真 3.4.67 60 sublevel) 或继续 web 找 3.4.67+MT6572 + hongyao@PowerEdge-R630 编译机署名。
- **编译 agent state(2026-07-10 19:30)**: wmt_dev.c 被 agent Edit 工具拆坏多处字符串(`\n`误换真换行), 文件半破无法编译, 已用 drsami 干净原版(ca9fdc4d...)回滚。agent 已搭好 stub_includes 体系(wakelock/earlysuspend/aee/xlog + mach/*) + **已编出 mtk_stp_wmt.mod, wmt_core.o**(部分文件编成功)。connectivity 子树 agent 总改入:wmt_dev.c(已回滚), Makefile(顶层+drv_wlan+common), stub_includes 全套。
</content>
</invoke>

## 2026-07-10 21:00 wlan_mt6620.ko 编译进展 + 真实难度收口
**已完成(我亲手改,非agent)**:
- wmt_dev.c 4 类 mainline API 墙全过: proc_create+struct proc_ops(mainline 5.6+ 老是 file_operations, 改成 proc_ops/*.proc_*); kernel_read 替 fd->f_op->read; 删整套 set_fs/get_ds/cred fsuid 覆盖; do_gettimeofday→ktime_get_real_ts64 + struct timespec64. 备份 wmt_dev.c.20260710_194847_preManualEdit.
- wlan 子树 Makefile: 加 -DCONFIG_NL80211_TESTMODE=1 (外编 M= 子树不读根 .config autoconf.h, 源码 `#if CONFIG_NL80211_TESTMODE` 撞 #error); 加 -isystem $(shell $(CROSS_COMPILE)gcc -print-file-name=include) (mainline 7.0 NOSTDINC_FLAGS 只 -nostdinc 不喂 gcc 自带 lib include, vendor 源 `#include <stddef.h>` 撞 fatal error). 备份 Makefile.20260710_202619/20260710_204905.

**真实难度(wlan.ko 不是"剩1错出.ko"的轻活,是和 thermal 墙同级的大改)**:
- wlan 子树 stddef.h/testmode 墙过之后,下一堵真墙在 os/linux/gl_init.c: **24 条 error, 51 条集中 gl_init.c**, 全是 mainline cfg80211/wireless framework 从 3.4→7.0 ABI 大改:
  1. struct net_device 无 ieee80211_ptr 成员 (mainline 封装改了)
  2. alloc_netdev_mq 4→5 参数
  3. cfg80211_ops 整组回调签名全变 (struct net_device* → struct wireless_dev*, scan/mgmt_tx/TLS/add_key 等, ~15 处 incompatible pointer type)
  4. IEEE80211_BAND_2GHZ/5GHZ → mainline 改 NL80211_BAND_2GHZ (vendor 用旧 IEEE80211_BAND_*)
  5. WIPHY_FLAG_CUSTOM_REGULATORY 删/改, flush_delayed_work_sync 移除, ndo_select_queue 多一参
- **结论**: wlan.ko 完整编出 = 重写一层 cfg80211 适配 (driver 私有 wireless_dev 包装 + 全组 cfg80211_ops 回调阈值适配 + band 枚举映射). 工作量等同 wmt_tm.c thermal 墙. 不是 1 行 ccflags 能解决.

**Task#4a 状态**: 未出 .ko. stddef/testmode 两堵外编墙过了(我这轮加), 但暴露了 cfg80211 ABI 大改的真墙(预计 24+ 错要逐条改). 这是路 A 的真实成本.
