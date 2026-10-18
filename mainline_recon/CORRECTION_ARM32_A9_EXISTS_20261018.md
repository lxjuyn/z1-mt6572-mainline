# 更正：32 位 Android 9 是存在的（此前"拿不到 arm32 A9"的说法作废）

## 错在哪

我在 2026-10-18 的会话里说过"32 位 A9 无法取得"。这句话被我把两件事混成一件：
- **有据可查的事实**：Google 模拟器系统镜像仓库（`sys-img2-1.xml`）里 `armeabi-v7a` 只到
  API 24/25，**没有 android-28 的 arm32 模拟器镜像**；
- **我错误外推的结论**：由此说"拿不到 32 位 Android 9 userspace"。

后者是错的。用户指出后我改用完整数据核实，不再凭印象。

## 核实结果（可复现）

从 `LineageOS/lineage_wiki` 的 `_data/devices/*.yml`（**稀疏克隆全量 764 台设备定义**，
字段名是 `architecture:` 而不是 `arch:`——我第一版正则因此全部落空）统计：

| architecture | 台数 |
|---|---|
| arm64 | 571 |
| **arm（32 位）** | **162** |
| x86 | 1 |
| （30 台写成嵌套 `cpu:` 结构，未归类） | 30 |

这 162 台 arm32 设备按 `current_branch` 分布：

| 分支 | arm32 台数 | 例子 |
|---|---|---|
| **16.0（= Android 9 Pie）** | **7** | `find7`、`ham`、`kccat6`、`lentislte`、`r5` |
| 17.1（Q） | 8 | `m8`、`harpia`、`lux`、`osprey` |
| 18.1（R） | 41 | `klte`、`castor_windy`、`bacon`、`FP2` |

**结论：Android 9 的 32 位（armv7）userspace 是官方存在的**，既有 AOSP `aosp_arm` 这条线，
也有 LineageOS 16.0 的 7 台 arm32 设备；更低端的 Android 9 Go 机型本身就是 32 位 SoC。
先前那句"拿不到"是错的。

## 但对本项目，本机的实际限制在别处（这次是实测出来的）

在这台机器上逐一探测了可达性：

| 目标 | 结果 |
|---|---|
| `api.github.com` | **200**（binder 头文件就是经 contents API 从 aosp-mirror 取回的） |
| `github.com`（git https） | **可用**（`git ls-remote`/`git clone --filter` 均成功） |
| `dl.google.com` | 可响应（但模拟器仓库确实没有 android-28 arm32） |
| `lineageos.org` / `download.lineageos.org` | 200，但页面是 JS 壳（723 B），我试的 3 个 API 路径都 404 |
| `update.lineageos.org`（真正的构建镜像目录） | **000 = 连不上** |
| `sourceforge.net` | **403** |
| `android.googlesource.com` / `raw.githubusercontent.com` | 均不可达 |

也就是说：**能不能弄到 A9 arm32 的 system 镜像，卡的是这台机器的出网面，不是"32 位 A9 不存在"**。
需要 A9 arm32 原料时，可行的办法是换一台能连 LOS 镜像的机器下载（或让用户直接给文件），
本机接手后续的加工/出片 —— 本机的镜像级改造链已经建好并验过。

顺带记下这套工具对"外来 A9 arm32 镜像"的适配点，避免下次重做判断：
arm32 LOS16 官方设备里没有 MTK（`find7/ham/kccat6` 是 Qualcomm），所以它们的 `/vendor` HAL 与
MT6572 完全不匹配，只能取 `/system` 侧内容，显示/HIDL 仍要用 Z1 自己的
`gralloc`(ashmem+`/dev/fb`)、`hwcomposer` 与 `manifest.xml`；`boot.img` 必须用我们自己的
（内核不同），不能连 vendor_boot/dtb 一起搬。这类"跨设备搬 /system"的可行性受 VINTF
矩阵与 SELinux 版本影响，本机无法离线断言，必须上板验。
