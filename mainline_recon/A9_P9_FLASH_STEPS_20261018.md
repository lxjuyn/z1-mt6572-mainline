# A9 P9 上板步骤（2026-10-18）—— 交棒链第一轮该刷什么、该读什么

## 0. 本轮出片物（都在工作区 `out/`，已进 git 台账 `A9_P9_MANIFEST.txt`）

| 文件 | 大小 | sha256 | 用途 |
|---|---|---|---|
| `out/boot_mainline_a9p9_D1_1b.img` | 5,656,576 | `d7b428c1fccdd9c09d8b416503524b9372ef396e8fa02ec9929374d90c04625a` | **推荐**：eMMC 仍 1-bit，与已验证启动链同源 |
| `out/boot_mainline_a9p9_D4_4b.img` | 5,656,576 | `e96718d6772b20ad50eab3291cc11d0bc2adb5969428fdf82427871a074e41e2` | 只在 D1 起来之后再试（`bus-width=<4>` + DAT1-3 pinmux） |
| `out/ramdisk_a9p9.gz` | 1,281,977 | `2ef3b09ace72201c71e7e789a240c18ecee8d2dcef37d4b91aeb47b004d0f18c` | 载荷母本（body 1,281,465 → `initrd-end=0x84238db9`） |
| `out/A9_flashpkg_p9_D1/`、`out/A9_flashpkg_p9_D4/` | — | 各自 `HASHES.txt` | 现成刷机包（boot.img + scatter，只放行 `bootimg`） |

刷写失败的回滚件仍是原厂 `1/boot.img`（sha 前缀 `2dd41702`，零修改）。

## 1. 刷 boot-only（这一轮就该做的动作）

```sh
cd /home/lxj/claude/6572
tools/flash_with_retry.sh bootimg out/A9_flashpkg_p9_D1/boot.img 1/boot.img 8   # 第3参=失败自动回滚原厂
Z1_CAPTURE_BAUD=921600 ./z1_capture_mainline.sh 600      # CH340 直连必须带这个变量；经 C3 桥则去掉
```
纪律照旧：**先起 mtkclient 再按开机键**；下载线直插主板后置 USB，**别经 hub**；正常开机时拔掉下载线。

## 2. 串口里必须出现的顺序（缺一段就停在哪一段）

`[init] welcome` → `[pstore]` → **`[a9dev]`**（运行时节点 YES/MISSING）→ `[a9probe] RESULT_STEP_*`
→ `[a9cfg]`（`/proc/config.gz` 自证）→ **`[a9hand]`** → ACM/getty。

`[a9dev]` 那段本轮才搬到无条件执行：原先它写在 `[ -e /proc/config.gz ]` 的 else 里，
IKCONFIG_PROC 一生效就永不打印（也就是"证①过了、证②没地方出"）。

`[a9hand]` 在**没刷 A9 system** 的板子上应当是：

```
[a9hand] 交棒判定开始
[a9hand] android 分区节点=mmcblk0pN (943718400 B)          # 认分区靠 scatter 字节数，不猜设备号
[a9hand] RESULT_STEP_handoff_probe=FAIL(sdk=19 不是 28，不交棒)   # 分区里还是原厂 4.4
[a9hand] 判定未过，继续走调试 shell（下面起是 ACM/getty）
```
看到这三行就说明：分区识别、只读挂载、build.prop 判据、以及"不交棒就不动 shell"的兜底全对。
若 `SKIP(没有尺寸=…)`：分区没被认出来 → 把 `mmcblk0p*` 的 `/sys/class/block/*/size` 一并抓回来对几何。

## 3. 手里有真实 A9 system.img 之后（一次跑完，别再手工分步）

```sh
cd /home/lxj/claude/6572
tools/z1/z1_a9_ship.sh verify <分片目录> out/system_a9.img           # 逐片 sha + 整件 sha，坏一片就不出镜像
python3 tools/z1/z1_a9_system_accept.py out/system_a9.img           # 内容级验收（sdk/abi/ELF/支柱/门控）
tools/z1/z1_a9_finalize.sh --ramdisk out/ramdisk_a9p9.gz \
        --system out/system_a9.img --variant 1b --dir out/A9_final_a9p10   # 改造 + 装配 + 预检一条链
```
`finalize` 内部四步依次为：ramdisk 内容级验收 → 出 boot.img（initrd-end 现算 + 预算闸门 + 载荷逐字节核验）
→ `z1_a9_mainline_system.py`（撤 stock 门控 + 投 6 份 mainline device 层）→ 装配（再跑一次
`--expect-adapted` 验收闸），任一步非零就停并打印停在哪一步。

**这条链本轮已经用 A9 arm32 夹具跑通过一次**（不是推演）：`rc=0 FINALIZE=OK`，
四步全过，出包 `boot.img + system.img + scatter(放行 bootimg,android) + SYS_ACCEPT.json/log`，
且产出的 boot.img sha 与 §0 表里的 `d7b428c1…` **逐字节一致** —— 说明交料口一旦有真 system.img，
这一条命令就是全片装配。夹具本身不是交付物。

刷全片：SP Flash Tool 用 `out/A9_final_*/pkg/MT6572_Z1_A9_scatter.txt`，**Download Only**，
勾选项只应有 `bootimg` 与 `android` 两行；`userdata` 那行必须由 scatter 生成器置为不下载。

## 4. 交棒成功时串口会多出这四行（这就是本轮要拿的板上真值）

```
[a9hand] RESULT_STEP_vendor=TMPFS(...)      或 UNMOUNTED(...) / BIND(...)
[a9hand] ro.boot.hardware 等价名=z1(自 /proc/cmdline)
[a9hand] RESULT_STEP_fstab=PLACED|OK|MISS|FAIL
[a9hand] RESULT_STEP_selinux=LOADED(...)|NOPOLICY|FAIL|SKIP
[a9hand] RESULT_STEP_handoff=EXEC(/system/bin/init second_stage)
```
之后是 A9 init 自己的日志。四条待证事项里最容易先死的是 `selinux`（镜像里没有
`precompiled_sepolicy` 就无从载入）与 `fstab`（缺了挂不上 /data）—— 这两行是本步的靶子。
交棒后若 A9 挂了：`panic=10` 会重启，**下一轮开机 `[pstore]` 会把上一轮 console-ramoops 吐回来**，
所以别断电，直接再抓一次串口就能看到崩溃尾巴。

## 5. 本轮没做的事（别当成已完成）

- 没上板：所有结论都是主机离线证据（夹具与真实原厂 4.4 镜像）。
- 手里没有真实 A9 system.img：§3 那条链是用 A9 arm32 **夹具**跑通的，只证明通道与判据能接上，
  夹具不是交付物。取料边界见 `A9_SYSTEM_ADAPT_20261018.md` §8/§10 与 `A9_HANDOFF_20261018.md` §7。
- 不改任何 `.so/.jar`：S1 系列打在源码里的补丁仍在二进制里，只是被门控关掉。
