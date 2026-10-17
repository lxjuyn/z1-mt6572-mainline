# V13 刷后启动失败 — 专家团结论文档 (2026-08-26)

> 状态: Z1 已关机拔电(省电)。bootloader/preloader 未动, 随时可进下载模式重刷。
> 本文目标: 把 v13 无限重启的排查推进到"只差一个实验"的程度, 交给专家团定案。
>
> ⚠️ **结局更新 (2026-08-26 凌晨)**: 本文已定案关闭。根因 = dts chosen 硬编码的过期值
> `linux,initrd-end = <0x841e2810>` 把 initrd 窗口截短到 908K (payload 实为 1,068,865 B) →
> gzip 中途断粮; 本文"剥头补丁未执行"假设证伪 (LK 载入时已自行剥头, 补丁必然不命中,
> 方案1 单独无效)。v13.1 将 `linux,initrd-end` 改为 `0x84204f41` 后上板验证通过。
> 下文内容仅作历史留档。定案详情/打包命令/上板核对表/新卡点 (mtk-msdc CMD1 硬挂起)
> 见 `mainline_recon/V13_PACKED_20260825.md` 的 v13.1 章节。

---

## 一、已完成事项

### 1. 主机输入法修复 ✅ (与 Z1 无关, 已闭环)
- 症状: fcitx 4.2.9 + 搜狗, 启动后 ~15-20 秒自行退出(退出码 0), 无法切换输入法
- 排除的假凶手: QQ 拉新实例、搜狗 dbus 卡私有总线、会话 dbus / AppArmor / 代理守护
- 真凶: **fcitx-clipboard 模块**, 处理剪贴板事件时用无效 Atom 调 XGetWindowProperty,
  X 服务器报 BadAtom 致命错误 → fcitx 有序自杀
  (铁证: `~/.config/fcitx/log/crash.log` = `fcitx: BadAtom (invalid Atom parameter)`)
- 修复: `~/.config/fcitx/addon/fcitx-clipboard.conf` 用户级覆盖 `Enabled=False`
  (系统级 /usr/share/fcitx/addon/ 未动); 搜狗模块无罪, 已恢复;
  `~/.config/fcitx/config` 的 `SwitchKey` 从 Disabled 改回 `CTRL_SPACE`
- 已验证存活 121 秒+, fcitx-remote 通信正常; 已写入长期记忆

### 2. Z1 v13 打包与刷写 ✅ (刷写成功, 启动失败)
- 背景: v12 漂移作废(其 zImage 编于 08-23 17:19, 漏 4dcaf6c64 WDT 安全网)
- 从 HEAD 86e8a98bb 重编: zImage 3,789,312 B (22:46, #103), dtb 19,744 B
- 打包: `cat zImage dtb > /tmp/zImage_appended_v13` → mkbootimg
  `--base 0x10000000 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 --pagesize 2048 --mtk 1`
  cmdline 逐字节沿用 v12 实解参数:
  `console=tty0 console=ttyMT0,921600n8 root=/dev/ram panic=10 loglevel=7 clk_ignore_unused regulator_ignore_unused oops=panic sysrq_always_enabled rdinit=/init earlycon=mtk8250,0x11005000`
  ramdisk = `out/ramdisk_v11_extracted.gz` (1,069,377 B)
- 产物: `out/boot_mainline_v13.img` (4,884,480 B)
- 刷写: `tools/flash_with_retry.sh bootimg out/boot_mainline_v13.img /home/<user>/claude/6572/1/boot.img 8`
  第 2/8 轮成功, 3.15 MB/s, **严格读回校验前 4,884,480 B 与目标一致**
  (尝试 1 失败根因: USB hub 下挂, error -71; 直插后置 USB 后解决)

### 3. 启动失败排查 — 已完成的取证 ✅
- 抓取: `mainline_recon/z1_boot_mainline_20260826_005657.log` (921600 baud, CH340)
- 现象: 内核起来 → 崩溃 → `panic=10` 重启, 无限循环(日志里至少 19 轮)

---

## 二、关键证据链 (专家团重点)

### 症状序列 (每轮重启一致)
```
[0.107599] Trying to unpack rootfs image as initramfs...
[0.150707] rootfs image is not initramfs (read error); looks like an initrd
[0.155526] Freeing initrd memory: 908K
[0.407999] Run /init as init process
[0.409920] Failed to execute /init (error -26)     ← ETXTBSY, rootfs 空壳的连锁反应
   → /sbin/init → /etc/init → /bin/init → /bin/sh 全失败 → panic → 重启
```

### 已核实的事实
1. **内核确实是新编的**: `Linux version 7.0.0-rc7-z1+ ... #103 SMP Tue Aug 25 22:46:14 CST 2026`
2. **ramdisk 本身没问题**: v11/v12/v13 三者 ramdisk 段逐字节一致 (sha256 前缀 `f46d560c`);
   格式 = 512B MTK ROOTFS 头 (magic `88 16 88 58`, ASCII "ROOTFS" 在偏移 0x08, 长度字段 0x04=1068865)
   + gzip-cpio payload; payload 本地可完整解压: **65 条目, /init 脚本在, bin/busybox 1.5MB 静态在**
   (此前"缺 busybox"的判断是本地解包不完整造成的误判, 已推翻)
3. **内核源码里有剥头补丁**: `352764f26` + `635575a7f`, 位于 `init/initramfs.c` unpack_to_rootfs()
   开头(~525-538 行): 检测 magic + "ROOT"@0x08 → `pr_info("initramfs: detected MTK ROOTFS header, skipping 512 bytes\n"); buf+=512; len-=512;`
   **检测条件与实际头逐字节吻合**。字符串确认在 `vmlinux` 和 `arch/arm/boot/Image` (22:46 同批构建) 中
4. **但运行日志里 0 次命中 "detected MTK ROOTFS"** → 剥头逻辑要么没执行, 要么走了别的代码路径
5. **initrd 尺寸疑点**: `Freeing initrd memory: 908K` (≈929,792 B),
   而 boot.img 的 ramdisk 段是 1,069,377 B (应为 ~1044K)。
   对比 8/15 成功日志: `Freeing initrd memory: 2176K` 与其镜像段 2,226,880 B 吻合。
   → **内核看到的 initrd 比镜像段短 ~139KB**, gzip 流被截断即可解释 "read error"。
   需要专家确认: 这 908K 是 LK 载入截断? DT chosen linux,initrd-end 写错? 还是别的边界?
6. **WDT 安全网日志也未出现** (0 次 "WDT safety net") — 按 4dcaf6c64 应在 mtk_wdt probe
   附近打印。`mtk-wdt 10007000.watchdog: Watchdog enabled` 有出现。
   (未及核实该打印点的代码位置与 loglevel, 留待专家)

### 已知良好基线 (回滚/对比用)
- `out/boot_mainline_z1_panicdbg.img` (6,017,024 B, 8/14 22:26) — 8/15 成功进 userspace 的镜像:
  其 ramdisk 是 **另一个文件**: 2,226,880 B, **xz 压缩**, 103 条目, sha 前缀 `58e3d827`
  (已解到 /tmp/rd_big.cpio; 注意 /tmp 重启会丢, 重解: 取该镜像 ramdisk 段跳过 512 头即得)
- 含义: 历史上真正跑通 userspace 的都是大 ramdisk; 小 ramdisk (v11/12/13 在用的这个)
  **从未被证明成功启动过**

---

## 三、悬而未决的矛盾 (核心问题)

> 剥头补丁条件与实际头完全吻合、代码已链入镜像, 但运行时既无 pr_info 输出、
> initrd 尺寸还短了 139KB。两条线索互相独立, 可能同根因也可能两个问题。

候选假设 (按专家团可自行增删):
- A. 7.0-rc 树里 initrd 的解包路径不经过这份打了补丁的 `unpack_to_rootfs`
  (重构后走了另一函数/另一编译单元) → 补丁形同虚设, 头被当压缩数据解 → read error
- B. LK/preloader 载入 ramdisk 时截断或错位 (908K 疑点), 头虽然在但数据不完整
- C. pr_info 因 console 时序/loglevel 被吞 (可能性最低: loglevel=7, 且同期 info 级日志正常输出)

## 四、未完成 / 待专家团执行

1. **定案根因**: 验证假设 A — 在 vmlinux 里反汇编 `unpack_to_rootfs`, 确认 0x88/0x16
   比较序列存在且被 initrd 路径调用; 或在 7.0-rc 源码里追 initrd → unpack 的调用链
2. **修复并出 v13.1**: 三个候选方案(不互斥, 推荐顺序):
   - 方案1 (最稳): 主机端直接把 512B MTK 头剥掉, 用**裸 gzip-cpio** 打 ramdisk 段,
     完全不依赖内核补丁 → 重打 v13.1
   - 方案2: 换用已知良好的大 ramdisk (panicdbg 镜像里那个 2176K xz 的)
   - 方案3: 修内核补丁 (若假设 A 成立, 把检测挪到真正的 initrd 解包入口)
3. **重刷 + 验证**: 下载模式刷 v13.1 → 电池开机 → `./z1_capture_mainline.sh 600`
   (环境变量 `Z1_CAPTURE_BAUD=921600`) → 验证点:
   - `Run /init` 后出现 shell (不再 error -26)
   - dmesg 出现 `WDT safety net: scheduling disable in 18s if no user-space feed`
4. **文档/记忆回写**: `mainline_recon/V13_PACKED_20260825.md` 补"启动失败"结论;
   `~/.claude/projects/-home-lxj-claude-6572/memory/mainline-z1-v11-boot-status.md`
   更新为"v13 已刷但启动失败, 待 v13.1"
5. 更早的挂起项 (非阻塞): 蓝牙上板验证(栈已备好, 见 BT_STACK_BUILT_20260814.md);
   S6 硬件 UART 测试点定位

## 五、关键命令速查

```bash
# 刷写(8轮重试, 严格读回校验)
tools/flash_with_retry.sh bootimg out/boot_mainline_v13.img /home/<user>/claude/6572/1/boot.img 8
# 抓串口日志(先插 CH340, 再开机)
Z1_CAPTURE_BAUD=921600 ./z1_capture_mainline.sh 600
# 打包(勿用 tools/z1_make_bootimg.sh, 其默认 cmdline 是 ttyS0,115200 与实际不符!)
cat fork_linux/arch/arm/boot/zImage fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb > /tmp/zImage_appended
./build/mtkbootimg/mkbootimg --kernel /tmp/zImage_appended --ramdisk <ramdisk.gz> \
  --base 0x10000000 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
  --pagesize 2048 --mtk 1 --cmdline "console=tty0 console=ttyMT0,921600n8 root=/dev/ram panic=10 loglevel=7 clk_ignore_unused regulator_ignore_unused oops=panic sysrq_always_enabled rdinit=/init earlycon=mtk8250,0x11005000" -o out/boot_v13_1.img
# 内核构建纪律: 绝不设 LOCALVERSION= (vermagic 固定 7.0.0-rc7-z1+)
```

## 六、关键文件清单

| 文件 | 说明 |
|---|---|
| `out/boot_mainline_v13.img` | 当前板上的镜像(4,884,480 B), 启动失败 |
| `out/boot_mainline_z1_panicdbg.img` | 8/15 已知良好基线(6,017,024 B) |
| `out/ramdisk_v11_extracted.gz` | v13 用小 ramdisk(带 512B MTK 头, 1,069,377 B) |
| `mainline_recon/z1_boot_mainline_20260826_005657.log` | 失败启动全量日志 |
| `fork_linux/init/initramfs.c` ~L525-538 | 剥头补丁位置 |
| `1/boot.img` | 原厂回滚镜像(刷写脚本的回退件) |
| `bridge_backups/products/products_v13_20260825_231934/` | v13 产物归档 |
