# CONNSYS 缺口补齐：cfg80211.ko + mtk_stp_launcher 编译报告 (2026-08-14)

> 板子关机，纯编译产出。vermagic 固定 `7.0.0-rc7-z1+`（CONFIG_LOCALVERSION="-z1" + AUTO=n, git a00567cbe）。
> 任务: 补 wlan_gen2 insmod 依赖的 cfg80211.ko + MTK WMT 用户态 launcher。

## 产物 1: cfg80211.ko

| 项 | 值 |
|---|---|
| 产物路径 | `/home/<user>/claude/6572/fork_linux/net/wireless/cfg80211.ko` |
| 大小 | 491872 B |
| vermagic | **`7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8`** ✅ 完全匹配 |
| 编译命令 | `cd /home/<user>/claude/6572/fork_linux && make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- M=net/wireless KBUILD_MODPOST_WARN=1 -j4 modules` |
| .config 前置 | `CONFIG_CFG80211=m` (行1042), `CONFIG_WIRELESS=y` (行1039), `CONFIG_LOCALVERSION="-z1"` (行41), `CONFIG_LOCALVERSION_AUTO` 未开 — 均原样未动 |
| 退出码 | 0（编译前 5 个 connsys .ko 同款 `KBUILD_MODPOST_WARN=1`） |

modinfo 摘要: `depends:` 空（无子模块依赖）；`firmware: regulatory.db`；`alias: net-pf-16-proto-16-family-nl80211`。
完整 modinfo 已存归档 `cfg80211_modinfo.txt`。

### modpost "undefined symbol" 警告（正常，非生成障碍）
`iwe_stream_add_value/event/point`、`wireless_send_event`、`wireless_nlevent_flush` — 外编 M= 不读整树 Module.symvers，insmod 时由内核符号表解析（与 CLAUDE.md §4 路A 其他 7 个 .ko 同款情形）。其中 `wireless_send_event`/`iwe_stream_*` 由 `CONFIG_WIRELESS_EXT` 内核段提供（已确认主 .config `CONFIG_CFG80211_WEXT=y`），非 cfg80211 自身缺失。

## 产物 2: mtk_stp_launcher

| 项 | 值 |
|---|---|
| 产物路径 | `/tmp/connsys_build/tools/launcher/mtk_stp_launcher` |
| 大小 | 522212 B |
| 类型 | `ELF 32-bit LSB executable, ARM, EABI5, statically linked`（`file` 确认，`readelf -d` 无 dynamic section）✅ |
| 编译命令 | `cd /tmp/connsys_build/tools/launcher && arm-linux-gnueabihf-gcc -static -O2 -I . -o mtk_stp_launcher stp_uart_launcher.c` |
| 编译器 | `/usr/bin/arm-linux-gnueabihf-gcc` (Ubuntu 13.3.0) |
| 退出码 | 0（仅 4 类无害警告：pthread_setname_np 隐式声明 / read 返回值忽略 / ALOGI %s 可能 null — 均不影响静态链接与功能） |
| 头文件 | `wmt_ioctl.h`(本目录) + 本地桩 `sys/endian.h` / `utils/Log.h` / `cutils/properties.h`（-I . 全部覆盖，无需额外 -I） |

## 归档

目录: `/home/<user>/claude/6572/bridge_backups/products/products_connsys_missing_20260814_015818/`

```
cfg80211.ko            491872 B  (vermagic 7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8)
mtk_stp_launcher       522212 B  (ELF32 ARM static)
cfg80211_modinfo.txt   完整 modinfo 输出（含 vermagic 记录）
cfg80211_build.log     编译日志（LD [M] cfg80211.ko 收尾）
```

## 备注 / 风险点

1. **modinfo 显示 vermagic 尾部有尾随空格**（`7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8 `），与模块加载校验无关（vermagic 匹配按字符串前缀+内部空格归一，已与 `MODULE_INFO(vermagic, ...)` 标准格式一致；另外 5 个 connsys .ko 同构）。如需更保险，可另跑 `strings cfg80211.ko | grep ^vermagic` 双确认，但 modinfo 输出已是权威。
2. **cfg80211 缺 `regulatory.db` 固件**：modinfo `firmware: regulatory.db`。CFG80211_REQUIRE_SIGNED_REGDB 未开、`CONFIG_CFG80211_CRDA_SUPPORT=y`（走用户态 crda/regdb），上板运行时若内核请求 `regulatory.db` 而 rootfs 没有，会 fallback 到 builtin 或告警但不影响 wlan_gen2 insmod（wlan 驱动本身不依赖 regulatory.db 才能加载）。
3. **未做任何源码/配置改动**：fork_linux 工作树唯一 pre-existing 改动是 `scripts/z1_modprobe_connsys.sh`（主会话此前遗留，本次未触碰）。`.config` 未改。未 git commit。
4. **launcher 的 pthread_setname_np 隐式声明**：链接静态成功（glibc 符号存在），运行期该调用可能因 `_GNU_SOURCE` 未定义而行为与预期略有差异，但仅影响线程名设置，不影响 patch 下发主流程。如需消除可在编译命令加 `-D_GNU_SOURCE`（本轮按 README 原命令，未加）。

## 上板衔接（只列事实，不操作）

- insmod 顺序: `cfg80211.ko` → `wlan_gen2.ko`（后者依赖前者的 nl80211 符号）。
- WMT 启动前: 跑 `mtk_stp_launcher` 喂 patch（源码 README 说明 fallback 前缀 `ROMv<hwver>_patch`，需 `/system/etc/firmware/` 下有 `mt6572_82_patch_e1_{0,1}_hdr.bin` 复制为 `ROMv1_patch_{0,1}_hdr.bin` 才能把 Wi-Fi RAM code 拉起来）。
- 5 个 z1vermagic .ko 归档: `bridge_backups/products/products_connsys_modules_z1vermagic_20260814_015009/`。
