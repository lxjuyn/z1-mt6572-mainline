# A9 system 镜像的镜像级改造（2026-10-18，本机可做的那部分）

## 1. 为什么这件事不需要 AOSP 源码树

stock 线那一整套"绕开 3.4.67 缺能力"的代偿，不是散落在二进制里不可触碰，而是
**由镜像内的门控开关驱动的**：`/z1_stock_kernel` 标记文件 + `ro.z1.stock_kernel` 属性
（`ServiceManagement.cpp` 先走本地 factory、`hwservicemanager`/`vndservicemanager` 被
`disabled`、configstore 用本地 passthrough 工厂）。开关一关，这些分支就落回 AOSP 默认路径——
而 mainline 线要的就是默认路径。

所以**在 ext4 上就地改这几个文件**就能撤代偿，不必重编。可行性不是推测：stock 分支
`tools/z1_stock_system_profile.py` 已经用同一手法（debugfs 原地重写单个已分配数据块，
保住 inode 与 SELinux xattr，不碰块设备）改过真实 A9 镜像。

本机没有 `simg2img`，所以工具内置了 sparse→raw 只读转换器（chunk 类型
RAW=0xCAC1 / FILL=0xCAC2 / DONT_CARE=0xCAC3 / CRC32=0xCAC4）。

## 2. `tools/z1/z1_a9_mainline_system.py` 做的五步（每步都读回校验）

1. 删 `*/z1_stock_kernel`（**由遍历结果找真实路径**，不猜）；
2. 去掉 `hwservicemanager`/`vndservicemanager` 的 rc 里的 `disabled` 行 → 恢复真 HIDL 服务；
3. 投递本仓 mainline device 层（`ueventd.z1.rc`/`init.z1.rc`/`fstab.z1`/`manifest.xml`/
   `init.z1.network.rc`/`z1_usb_setup.sh`）到 `/system/etc/`；
4. 从 `build.prop` 里移除 `ro.z1.stock_kernel=1`；
5. `e2fsck -fn` 复检 + 尺寸不得超过 android 分区 943,718,400 B；输出 `ADAPT_REPORT.json`
   （逐条前后 sha256），直接接 `z1_a9_flashpkg.sh --boot … --system …`。

关键一条：**光删标记文件不够**。属性若由 `build.prop` 写死，`ro.z1.stock_kernel` 仍是 1，
门控照走 passthrough 分支——这一步是工具自己以 WARN 报出来后我补的（原先只警告不处理）。

## 3. 实测证据

- **真实 Android 镜像**：`1/system.img`（原厂 4.4，943,718,400 B ext4）遍历到 1450 条路径，
  并正确定位 `/build.prop` 在镜像根而非 `/system/` —— 证明"靠猜路径"必然漏，也证明
  本机的 debugfs 工具链能读真实 Android 镜像。
- **A9 形状夹具**（`mke2fs -O ^metadata_csum,^64bit -b 4096`，含标记文件、
  带 2 行 `disabled` 的单块 rc、含 `ro.z1.stock_kernel=1` 的 build.prop）跑完整改造：
  标记已删 / `disabled` 归零 / build.prop 只剩其它属性 / 6 份 device 层文件投递并读回一致 /
  `e2fsck rc=0` / 9 条动作 0 条备注。
- **端到端**：改造后的 system + P4 D1 boot → `z1_a9_flashpkg.sh` 出完整包，
  只放行 `bootimg,android`，强制关闭 9 个原本 `is_download:true` 的分区
  （preloader/mbr/ebr1/uboot/recovery/sec_ro/logo/cache/usrdata）。

## 4. 本机不能替它保证的事（别当成已完成）

- 手里**没有真实 A9 system.img**（在持 LOS16 源树的那台机器上），上面全部验证跑在夹具 +
  原厂 4.4 真镜像上：证明**手法与工具链可用**，不证明那份具体镜像改造后能起。
- 不改任何 `.so`/`.jar`：S1 系列打在源码里的补丁仍在二进制里，只是分支被门控关掉。
  如果那台机器的 system 是用**已撤销这些补丁**的方式重建的，本工具的第 1/2/4 步就是空操作
  （工具会如实报"没找到标记"）。
- SELinux 仍靠 dts 的 `androidboot.selinux=permissive` 首轮收 denial；`metadata_csum` 开启的
  ext4 会被工具**直接拒绝**原地改块（校验和会坏）。
- 未上板。

## 5. 一旦拿到 A9 system.img 的完整流程

```sh
python3 tools/z1/z1_a9_ramdisk_accept.py <generic ramdisk>          # 验收 ramdisk
A9_RAMDISK=… tools/z1/z1_a9_p2.sh pack 1b out/boot_mainline_a9p5.img # 出 boot（预算/闸门/载荷核验）
python3 tools/z1/z1_a9_mainline_system.py --system <system.img> --out out/a9sys   # 镜像级改造
tools/z1/z1_a9_flashpkg.sh --boot out/boot_mainline_a9p5.img --system out/a9sys/system_mainline.img
#   ^ 装配阶段现在会自动跑 z1_a9_system_accept.py --expect-adapted，不过就拒绝出包
```

## 6. P9 新增：system 镜像内容级验收闸（`tools/z1/z1_a9_system_accept.py`）

装包原先只查"容量 + sparse/ext4 magic"，也就是**把原厂 4.4 或某台 arm64 机型的 A9 灌进去
照样出包** —— 那两种刷上去都表现为 boot 到 init 就挂，而串口里最难读懂的正是 ABI/版本不匹配。
所以把判据搬到离线阶段，逐条打印 OK/WARN/FAIL，`SYS_ACCEPT=ACCEPT|REJECT`，退出码 0/3。

硬判据（FAIL，任一即拒绝出包）：

| 判据 | 为什么是硬的 |
|---|---|
| `ro.build.version.sdk=28` | 不是 A9 就别刷 mainline A9 交付物 |
| `ro.product.cpu.abi/abilist` 含 `armeabi-v7a` | 本树是 `ARCH=arm` 的 32 位内核，arm64-only 镜像跑不了 |
| `/system/bin/init`、`servicemanager` 的 **ELF `e_machine` 必须是 EM_ARM(0x28)** | 属性会骗人，ELF 头不会；两条不一致时以 ELF 为准 |
| 8 项 A9 支柱可执行文件齐备 | `init/sh/servicemanager/hwservicemanager/lmkd/netd/ueventd/app_process32`，缺任一 init→SystemServer 链必断 |
| 无 `/z1_stock_kernel` 标记且 build.prop 无 `ro.z1.stock_kernel=1` | stock 代偿门未撤 → HIDL 永远走本地 passthrough |
| hwservicemanager/vndservicemanager 的 rc 里无 `disabled` | 同上，mainline 要真 HIDL 服务 |
| 尺寸 ≤ android 分区 943,718,400 B | 超了刷机时被截断或拒收 |
| `e2fsck` 报**结构性损坏**征兆（corrupt/inconsistent/missing/illegal/superblock…） | 真坏了不能刷 |
| `--expect-adapted`：mainline device 层已投递 | 装配阶段专用，防"忘了跑 adapter" |

软判据（WARN，不挡出包）：`metadata_csum`（能刷，但本机不可原地改块）、fs_state、路径数 <300
（可能给错分区，但 `build.prop`/bins 那两条本身就会判死，所以不重复硬失败）、`ro.hardware`/
`ro.product.device` 非 z1（靠 dts 的 `androidboot.hardware=z1` + `init.z1.rc` 兜，但要留意它按
原机型找 vendor HAL）、vintf manifest / sepolicy 文件缺失、`e2fsck` 仅记账类瑕疵。

**e2fsck 这条是实测逼出来的**：原厂 `1/system.img` 的 `e2fsck -fn` 返 rc=4，内容只是
"Inode 位图末尾的填充值未设置"—— 打包器不是 e2fsprogs，纯记账瑕疵，照样能刷。第一版把它
当 FAIL，就是一次**误拒**；改成按报文关键词分级后才对。

## 7. 实测证据（四条，全部本机跑过）

```
CASE1 真实原厂 4.4 镜像        REJECT  FAIL sdk(=19) FAIL bins(hwservicemanager/lmkd/ueventd/app_process32)
CASE2 A9 arm32 干净形状夹具     ACCEPT  FAIL=0 WARN=1(路径数)
CASE3 A9 但 stock 门控+arm64   REJECT  FAIL=6：abi_prop/stock_prop/stock_marker/elf_init/elf_hwservicemanager/hidl_rc
CASE4 未投 device 层 + --expect-adapted  REJECT  FAIL dev_layer
端到端 正路：夹具 -> adapter(投 6 份 device 层) -> 验收 ACCEPT -> 出包 rc=0，
        包里带 SYS_ACCEPT.json/log 与哈希；负路：同一命令换真 4.4 -> 预检拒出包 rc=3。
```

复现：`bash tools/z1/z1_a9_system_accept_selftest.sh`（夹具用 `mke2fs -d` 落目录，
arm64 负夹具靠把 ELF 头偏移 18 的 `e_machine` 改成 0xB7，其余字节不动）。
`/tmp` 夹具不入库；判据全部来自只读遍历，工具不写镜像。

## 8. 给持 A9 system.img 那台机器的交付规格

按上面硬判据准备即可，最容易被踩的两条：**必须是 arm32（armeabi-v7a）产物**，
**不要把 stock 线的门控形态原样交过来**（要么在源机撤干净，要么交过来由本机 adapter 撤）。
交接后本机一条命令自证是否合格：

```sh
python3 tools/z1/z1_a9_system_accept.py <system.img>          # 只读，不改镜像
```

顺带记一条取料事实（2026-10-18 复测）：`blob.lineageos.org` 是**真能下文件**的 LOS 主机
（`/downloads/` 有 autoindex，`twrp/`、`heimdall/` 等，实测 200 且 Content-Length 真实），
但**不放 ROM**；`update.lineageos.org` 现在连 DNS 都解析不了；`download.lineageos.org`
对**任意路径**都回同一份 723 B 的 Vue 壳（`index.json`、`/m7/` 都一样），所以它的 200
不代表可达。结论仍是：本机弄不到 A9 arm32 的 system 原料，卡点在出网面不在"32 位 A9 不存在"。

## 9. P9 之后：只要 system，不要 ramdisk

交棒链（`A9_HANDOFF_20261018.md`）已把 first-stage 的必要职责搬进本机自己的 bring-up ramdisk，
所以**上面 §8 那份"交 system.img + A9 generic ramdisk"的清单缩成一条：只交 system.img**。
boot.img 用本机 P9 出片（`out/boot_mainline_a9p9_D1_1b.img` / `..._D4_4b.img`），
刷完由 `[a9hand] RESULT_STEP_handoff_probe=GO` 决定是否进 A9；判定不过原样落回调试 shell。

