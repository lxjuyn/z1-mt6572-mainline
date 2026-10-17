# iw (nl80211) 交叉编译报告 — 2026-08-14

## 结论一句话
`iw 5.19` (libnl-3.7.0) 已交叉编译成 **ARM 32-bit EABI5 静态链接** 二进制，用于 Z1 上板测 WiFi scan（wlan_gen2 驱动只支持 nl80211，iwlist/WEXT 报 "Interface doesn't support scanning"）。qemu 冒烟测试通过（ARM 代码可执行、libnl 静态链接符号全解析）；qemu 环境下 netlink socket 被拒（EPROTONOSUPPORT）是本机 qemu/netlink 限制，非二进制缺陷（原生 x86 netlink 测试正常）。

## 产物位置
- 归档目录: `/home/<user>/claude/6572/bridge_backups/products/products_iw_nl80211_20260814_144324/`
- **上板用二进制**: `.../iw_arm_static_stripped` (764400 字节, 已 strip)
- 调试版(未 strip, 1.3MB): `.../iw_arm_static_debug`
- SHA256 (stripped): `38b5be3ccb07ccd67b8031fda729b53ebbeee8205c50bb37548b4617de935ceb`

## 目录内容
```
products_iw_nl80211_20260814_144324/
├── iw_arm_static_stripped       # iw 5.19 静态 ARM, 上板用 (764400 B)
├── iw_arm_static_debug          # 未 strip 版 (1366564 B)
├── iw-src/iw-5.19.tar.xz        # iw 源码 (kernel.org)
├── libnl-src/libnl-3.7.0.tar.gz # libnl 源码 (github thom311 release)
├── libnl-arm/                   # libnl 交叉编译产物
│   ├── lib/*.a                  # libnl-3.a libnl-genl-3.a libnl-route-3.a libnl-nf-3.a libnl-idiag-3.a libnl-xfrm-3.a
│   ├── include/libnl3/netlink/  # 头文件 (含 genl/, route/, netfilter/ 等)
│   └── lib/pkgconfig/*.pc
└── logs/                        # libnl configure/make/install + iw make 日志
```

## 编译命令

### 1. libnl-3.7.0 (静态库)
```
cd /tmp && tar xf libnl-3.7.0.tar.gz && cd libnl-3.7.0
./configure --host=arm-linux-gnueabihf --prefix=/tmp/libnl-arm \
    --disable-shared --enable-static --enable-cli=no --disable-python
make -j4 && make install
```
产出: `/tmp/libnl-arm/lib/libnl-{3,genl-3,route-3,nf-3,idiag-3,xfrm-3}.a` + 头文件装到 `/tmp/libnl-arm/include/libnl3/`（注意 libnl3 头在子目录 `libnl3/` 下）。

### 2. iw 5.19 (静态)
```
cd /tmp && tar xf iw-5.19.tar.xz && cd iw-5.19
make CC=arm-linux-gnueabihf-gcc NO_PKG_CONFIG=1 \
  CFLAGS="-O2 -I/tmp/libnl-arm/include/libnl3 -DCONFIG_LIBNL30" \
  LIBS="-L/tmp/libnl-arm/lib -lnl-genl-3 -lnl-3" \
  LDFLAGS="-static" \
  iw
arm-linux-gnueabihf-strip iw
```
**关键点**:
- iw-5.19 Makefile **没有** `NL3INC/NL3LIB` 变量（那是新版 iw-6.x 的），旧版靠 pkg-config 探测。用 `NO_PKG_CONFIG=1` 跳过探测，手动喂 `CFLAGS/LIBS`。
- 必须 `-DCONFIG_LIBNL30`：iw.c/iw.h 在未定义 CONFIG_LIBNL20/30 时会走 libnl-1.x 兼容代码（`nl_sock`→`nl_handle` 等），会编译错/链接错。
- `CFLAGS` 的 `-I` 指向 `include/libnl3` 子目录，不是 `include` 本身。
- 链接警告（`getaddrinfo`/`getprotobynumber` in statically linked apps）是 glibc 已知无害警告，iw scan 路径不调用。

## 静态验证
```
$ file iw_arm_static_stripped
ELF 32-bit LSB executable, ARM, EABI5 version 1 (GNU/Linux), statically linked, ... stripped
$ readelf -d iw_arm_static_stripped | grep NEEDED   # 无输出 = 无动态依赖
$ arm-linux-gnueabihf-nm iw_arm_static_debug | grep handle_scan   # 含 scan 命令符号
```
- 类型: ELF 32-bit ARM EABI5, **statically linked**, stripped
- 动态依赖: **无** (readelf NEEDED 为空)
- 大小: 764400 B (746 KiB) stripped / 1366564 B (1.3 MB) debug，符合预期
- 符号: 含 `handle_scan`、`NL80211_CMD_TRIGGER_SCAN`/`ABORT_SCAN` 等 scan 命令入口

## 上板用法 (Z1, busybox ramdisk)
```
# 把 iw 拷进 ramdisk/rootfs 后 (chmod +x)：
iw dev wlan0 scan          # 触发 + 打印 scan 结果 (BSSID/SSID/信道/信号)
iw dev                    # 列出无线接口
iw dev wlan0 info         # 接口信息
iw list                   # phy 能力 (wlan_gen2 驱动的 NL80211_BAND_* 支持情况)
iw dev wlan0 connect "SSID"   # 若驱动支持 station connect
```
- wlan0 接口名以驱动实际注册名为准（`iw dev` 可见）。
- 静态 glibc 二进制无动态库依赖，busybox 环境直接可跑；glibc NSS 警告（getaddrinfo 等）在纯扫描路径不触发。
- 如果 `iw` 报 `command failed: No such device (-19)`，说明 wlan0 未 up，先 `ip link set wlan0 up`（或 `ifconfig wlan0 up`，busybox 有）。

## 备注
- iw 版本: 5.19 (iw_version[] = "5.19")；libnl 版本: 3.7.0。
- 源码来自 kernel.org 官方 release (iw-5.19.tar.xz) + github thom311/libnl release (libnl-3.7.0.tar.gz)。
- 未做任何驱动/内核改动，未碰硬件，未刷写。
- qemu-arm-static 冒烟: `./iw version` 到 nl80211_init 后报 "Failed to connect to generic netlink"，经 strace 确认是 `socket(AF_NETLINK, ..., NETLINK_GENERIC)` 在本机 qemu 环境返回 EPROTONOSUPPORT（qemu/沙箱 netlink 限制；原生 x86 同名测试 socket/bind 全通）。二进制本身执行正常。
