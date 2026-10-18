# binder 协议版本：本树返回 8，A9 用户态默认也期望 8（2026-10-18 查实）

## 为什么这件事值得单独查

A9 的 `libbinder`（`frameworks/native/libs/binder/ProcessState.cpp::open_driver`）打开 `/dev/binder`
后会 `ioctl(BINDER_VERSION)`，把内核返回值与它**编译期**的 `BINDER_CURRENT_PROTOCOL_VERSION` 比对；
不等就 `close(fd)` 并把 fd 置 `-1`。后果不是"某功能不可用"，而是 servicemanager/zygote 起不来，
串口现场长得像"内核 binder 坏了"。所以这个值必须提前定死，不能等上板赌。

## 两侧的值（各自都有实证，不是记忆）

**内核侧 = 8**，来自本树构建产物本身：
- `include/uapi/linux/android/binder.h`：`#ifdef BINDER_IPC_32BIT` → 7，`#else` → 8；
- 全树 grep `BINDER_IPC_32BIT`：除了这个头文件里的两处使用，**没有任何地方定义它**
  （没有 `CONFIG_ANDROID_BINDER_IPC_32BIT`，Makefile/Kconfig/源码里都没有）；
- `make ARCH=arm ... drivers/android/binder.i` 里 `BINDER_VERSION` 分支展开成
  `__typeof__(*(&ver->protocol_version)) __pu_val = ((8));`；
- `objdump -d drivers/android/binder.o` 对应处是 `mov r0, #8`。

**用户态侧**：Pie 的 bionic uapi 头 `libc/kernel/uapi/linux/android/binder.h`
（取自 `aosp-mirror/platform_bionic` tag `android-9.0.0_r35`，经 GitHub contents API，
`android.googlesource.com` 与 `raw.githubusercontent.com` 本机均不可达）里是**同一个条件式**：
```
 95: #ifdef BINDER_IPC_32BIT
 96: #define BINDER_CURRENT_PROTOCOL_VERSION 7
 98: #define BINDER_CURRENT_PROTOCOL_VERSION 8
```
`BINDER_IPC_32BIT` 在用户态编译里同样不会被定义（它是当年 kernel 侧
`CONFIG_ANDROID_BINDER_IPC_32BIT` 的 Oreo-MR1 32 位 ABI 开关，现代内核已无此配置），
所以 A9 用户态期望的也是 **8** —— 与本树一致。

**结论**：不存在 7/8 不匹配风险。但这条只靠推理不够，已把它变成**上板一次即自证**的判据。

## 已落地的改动

1. `tools/z1/z1_a9_probe.c` 的 `probe_binder()`：原来只打印 `protocol_version`，
   现在若读到的值 ≠ 8，**额外出** `RESULT_STEP_binder_proto_expected_8=FAIL(got=N)`，
   不等下一轮刷机才反应过来。
2. 新增 `tools/z1/z1_a9_probe_build.sh`（可复现构建，之前是手敲命令，属交付缺口）。
   本机 armhf 工具链没有静态 libc，所以走 `-nostdlib -nodefaultlibs -lgcc` 裸 syscall 路线；
   uapi 头来自 `make headers_install` 的 `usr/include`。
3. **构建脚本内置入口硬校验**，这是本轮顺带抓到的一个真隐患：源码在 `__arm__` 下入口是
   `_start`（不是 `main`），我一开始按 `-Wl,-e,main` 传，链接器只给一句
   `警告: 无法找到项目符号 main; 缺省为 00010098` 就把入口摆到段首地址 —— 二进制照样
   `file` 出 "statically linked"，但一执行跳进别的函数，表现为**探针毫无输出**，
   极易被误诊成内核问题。现在脚本会比较 `readelf -h` 的入口与 `nm` 的 `_start`
   （含 Thumb 位 ±1），对不上直接 `exit 4`。
4. P6 出片：`out/boot_mainline_a9p6_D1_1b.img`（`0b235ed3…`）与 `_D4_4b.img`（`098076ba…`），
   ramdisk body 1,276,124 → `linux,initrd-end=0x842378dc`（头字段互证），
   `RESULT: VALID` + `VERIFY=OK` + `PAYLOAD-OK`，`DEVCHAIN_FAIL=0`。

## 仍未消除的限制

- 本机没有 `qemu-arm`，也没有 armhf 静态 libc 之外的执行手段，**探针二进制从未真正执行过**
  —— 入口校验、静态性、字符串存在性都是静态证据。要拿到运行时证据只能上板。
- A9 用户态侧的"期望值 8"依赖 `aosp-mirror` 的 Pie tag 头文件；若那台构建机的 bionic
  被改过或用了别的 kernel-headers，请在本机 ramdisk 验收之后、出片之前再读一次
  它的 `libc/kernel/uapi/linux/android/binder.h`。
