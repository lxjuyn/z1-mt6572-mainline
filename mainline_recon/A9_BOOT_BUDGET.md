# A9 boot 分区预算与体积归因（P1 实测）

## 1. 结论先说

| 项 | 值 |
|---|---|
| boot 分区上限 | 6,291,456 B |
| A9 内核 zImage | **4,772,352 B**（基线 3,920,384 B，**+851,968 B ≈ +832 KB 压缩后**） |
| 追加进 zImage 的 DTB | 19,744 B（A/B）/ 19,808 B（C，bootargs 变长） |
| 探针 ramdisk（v13.7+probe） | body 1,276,023 B（+512 B MTK 头 = 1,276,535） |
| 已出镜像 A/B/C 实际尺寸 | 6,074,368 B → **余量 217,088 B** |

含义：**boot 侧已经没有空间放 A9 的 generic ramdisk**（AOSP first-stage init ramdisk 通常 1–3 MB 压缩后）。
换 A9 ramdisk 前必须先做体积工程，否则会顶破 6 MB。

## 2. 归因方法与它的局限（如实标注）

- 本内核的 `System.map` 只有 `addr type name` 三字段（6.x 起由 kallsyms 派生，**没有尺寸列**），
  所以"按符号大小排序归因"这条路不成立——我第一次尝试得到的全是 0，那是解析错误不是结论。
- 改用可行方法：对两棵树的每个 `.o` 跑 `arm-linux-gnueabihf-size -t`，按目录累加 TEXT 段。
- 因此下表是 **未压缩 TEXT 增量**，与压缩后 +832 KB 不是同一量纲；`security/`、`drivers/block/`
  等父目录会**递归包含**其子目录，故总计已去掉父目录重复计数（原始各行相加为 1626 KB，
  去掉 `security` 的 266 KB 与 `drivers/block` 的 13 KB 后为 **≈1347 KB**）。

| 目录（去重后） | main TEXT | a9p1 TEXT | 增量 |
|---|---|---|---|
| `net/ipv6` | 11 KB | 380 KB | **+369 KB** |
| `security/selinux` | 0 | 182 KB | **+182 KB** |
| `net/xfrm` | 0 | 122 KB | **+122 KB** |
| `drivers/md`（DM_VERITY 等） | 0 | 118 KB | **+118 KB** |
| `fs/fuse` | 0 | 113 KB | **+113 KB** |
| `drivers/android`（binder+ashmem） | 0 | 89 KB | **+89 KB** |
| `net/netfilter` | 0 | 87 KB | **+87 KB** |
| `net/core` | 496 KB | 578 KB | +82 KB |
| `mm`（memcg 等） | 588 KB | 665 KB | +77 KB |
| `security/keys` | 0 | 38 KB | +38 KB |
| `kernel/cgroup` | 52 KB | 93 KB | +41 KB |
| `drivers/block/zram` | 0 | 13 KB | +13 KB |
| `crypto` / `fs/ext4` / `net/unix` | — | — | +7 / +6 / +5 KB |
| `lib` | 581 KB | 578 KB | −3 KB |

**A9 自身能力面合计 ≈ 1.35 MB（未压缩）**，与压缩后 +832 KB 相符。这些符号是 A9 的硬前置，
**不该通过裁它们来腾空间**。

## 3. 腾空间的正确方向（按性价比）

1. **裁掉与本设备无关的既有平台代码**（体积大头在这里，不在 A9 符号）：基线 `.config` 里
   `CONFIG_DVB_*`/`MEDIA_*`/`CONFIG_BT_*`/大量 `PHY_*`/`TPM`/`NFC`/其它 SoC 的 `MACH_*`
   都是从 multi_v7 继承来的。`make localmodconfig` 思路或按 Z1 实需手工裁，预计能回收远超
   1 MB（本轮 `modules_install` 产出 364 个 `.ko` 即说明外挂项之多）。
2. 把**非首启必需**的 A9 项从 `=y` 改 `=m` 随 system 提供：`FS_ENCRYPTION`/`KEYS`、
   `DM_VERITY`（若首轮不校验 system 可后置）、`ZRAM`（若先用 zram-tools 加载）、以及 netfilter
   其余 match。注意 `xt_quota2`/`xt_IDLETIMER` **已经是 `=m`**。
3. ramdisk 侧：探针镜像的 ramdisk 1.27 MB 里含完整 dmesg 回放逻辑；A9 的 first-stage ramdisk
   与调试 ramdisk 应分开——**别把探针塞进 A9 ramdisk**，用 C 变体这类探针镜像单独验内核面。
4. 压缩器：`CONFIG_KERNEL_XZ`（历史实测 8.46 MB→4.11 MB）保持；不要为省空间退回 gzip。

## 4. 交付物：`.ko` 集（给 AOSP 机）

位置 `bridge_backups/products/a9p1_ko/`：
- `xt_quota2.ko` 10,572 B `sha256 7e3bde80014d97a6…`
- `xt_IDLETIMER.ko` 14,552 B `sha256 55db6afabeeb47d1…`
- `modules.dep(.bin)/modules.symbols(.bin)/modules.order/modules.softdep/modules.devname`
- 两者 vermagic 均为 `7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8`，与 zImage 同轮编译。

完整 `modules_install` 树（364 个 `.ko`，10 MB）在 `/tmp/a9mods/lib/modules/7.0.0-rc7-z1+/`，
需要时可整包交付。

## 5. 下一步（预算相关）

- 出 A9 boot.img 之前，先跑一次"裁剪 + 体积复测"，把 kernel+dtb 压回 ≤ 4.3 MB，
  给 A9 ramdisk 留出 ≥ 1.5 MB；裁完必须重新过 `RESULT: VALID` 与符号三证（防止裁掉 A9 必需项）。


## 6. P7 追加实测（2026-10-18）：摘 eBPF 才是这块肉

本文 §3 说"腾空间该裁无关平台代码"，P2 照做省下 217,088 B；但 eBPF(`kernel/bpf` 内建 613 KB TEXT、
其中 verifier 208 KB)一直因为"SECCOMP_FILTER 依赖 BPF_SYSCALL"这句**在 7.0-rc7 已失效**的理由被供着。
P7 摘掉后：zImage 4,612,608 → **4,346,368**(-266,240 B)，
A9 generic ramdisk 上限 1,654,944 → **1,921,196 B**。
教训：**"某个符号不能关"这种断言，要去本树 Kconfig 读依赖，不能引用文档或老内核的记忆。**
