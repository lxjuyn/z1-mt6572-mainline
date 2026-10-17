# WiFi 静态工具编译报告 (iwconfig/iwlist/iwpriv) — 2026-08-14

状态: 完成。三个 WEXT 工具已交叉编译为 ARM 静态可执行文件，qemu-arm 冒烟测试通过。

## 目的
Z1 ramdisk 是 busybox，无 iwlist/iwconfig/iw（实测 `iwlist: not found`）。WiFi 驱动 wlan_gen2 已确认支持 WEXT（CONFIG_CFG80211_WEXT=y）。本次为下次刷入 ramdisk 测 WiFi scan 准备静态工具。只编译，未碰硬件/内核源码/未提交 git。

## 产物路径
归档目录（含 bin/ + 完整源码 + 编译日志 + 冒烟测试日志）:
```
/home/<user>/claude/6572/bridge_backups/products/products_wifi_tools_20260814_131306/
├── bin/
│   ├── iwconfig   (505 KB, 517368 B)
│   ├── iwlist     (513 KB, 525616 B)
│   └── iwpriv     (501 KB, 513264 B)
├── wireless_tools_src/   (63 文件: 全部 .c/.h/Makefile/man/COPYING, 含无线头)
├── build_arm_static.log  (交叉编译日志, exit=0, 仅 gethostbyname/getnetbyname 警告)
└── qemu_smoketest.log    (qemu-arm 运行验证日志)
```

## 二进制类型验证 (file)
```
iwconfig: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV), statically linked, stripped
iwlist:   ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV), statically linked, stripped
iwpriv:   ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV), statically linked, stripped
```
`readelf -d` 确认三者均无 NEEDED 条目 = 完全静态，不依赖 ramdisk 任何共享库。BuildID/日期: 见 bin/ 下 `sha256sum`（build log 与 report 内可复算）。

## SHA256
```
40f9659de7c1353b17eb8b5cfbdc21b9e2fa89cbecf1ff1bb706c3e00f92f304  iwconfig
288c59e953db132f64666a36a128139bccf92feb7b83fa63bd8c22cc7bd8c432  iwlist
3b2de3b010baaa8b4d017709866ed02122b525fb5d47fe5a708d8e6a5636ae94  iwpriv
```

## 源码来源
- 仓库: github.com/HewlettPackard/wireless-tools (git clone --depth 1, commit c107434 "Update README.md")
- 版本: wireless-tools 30-pre9 树, iwlib.h 内部报 WT_VERSION=29 / WE_VERSION=21
- 本地包 wireless-tools 30~pre9-16.1ubuntu2 已装 (amd64 二进制, 非源码; apt-get source 无 deb-src 源不可用)
- 本地 /usr/src 无源码; GitHub release tarball 404 (tag 不对), 用 git clone 拿到

## 编译命令
```
cd <source>/wireless_tools
make CC=arm-linux-gnueabihf-gcc BUILD_STATIC=y BUILD_STRIPPING=y LDFLAGS=-static iwconfig iwlist iwpriv
```
说明:
- `CC=arm-linux-gnueabihf-gcc` 交叉编译 (host 是 x86_64, Makefile 默认 CC=gcc)
- `BUILD_STATIC=y` 让工具链静态 libiw.a 而非 libiw.so
- `LDFLAGS=-static` 全静态链接 libc.a/libm.a (交叉工具链 /usr/arm-linux-gnueabihf/lib/{libc.a,libm.a} 均存在)
- `BUILD_STRIPPING=y` 减体积 (~0.5MB/个)
- 编译器: arm-linux-gnueabihf-gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1)
- 唯一警告: gethostbyname/getnetbyname 静态链接告警 (仅影响 DNS 解析 AP 名, scan 不用, 无碍)

## 运行验证 (qemu-arm-static)
```
$ qemu-arm-static ./iwconfig
lo        no wireless extensions.
enp7s0    no wireless extensions.
br-004839989c46  no wireless extensions.

$ qemu-arm-static ./iwlist --help
Usage: iwlist [interface] scanning [essid NNN] [last]
              [interface] frequency
              ...
```
二进制能在 qemu-arm 下正常启动并输出正确行为 (iwconfig 枚举接口, iwlist 打印 usage)。

## 上板使用提示 (供下次刷 ramdisk 时用)
- Z1 上用法: `iwconfig wlan0` / `iwlist wlan0 scanning` / `iwpriv wlan0` — WEXT 接口, 对应 cfg80211 WEXT compat (CONFIG_CFG80211_WEXT=y)。
- 建议一并放进 ramdisk: 三个工具 (~1.5MB) 直接拷入。
- 若 scan 无输出: 先 `iwconfig` 确认 wlan0 存在与 mode (managed)，再 `iwlist wlan0 scan`。
- 备选 (未编): iwgetid/iwspy/iwevent 同树源码在手, 同命令可随时加编; nl80211 的 `iw` 需 libnl-3 静态库 (系统无 arm 版), 未做。

## 归档内容
- bin/: 3 个 stripped 静态 ARM 可执行
- wireless_tools_src/: git clone 的完整源码 (iwconfig.c/iwlist.c/iwpriv.c/iwlib.c/iwlib.h/wireless.*.h/Makefile/man 等 63 项)
- build_arm_static.log: 全量编译日志 (exit=0)
- qemu_smoketest.log: 冒烟测试输出

未提交 git (按任务要求, 主会话统一提交)。
