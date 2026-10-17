# MT6572 CONNSYS Launcher 时序与 func-on 竞态分析

- 日期: 2026-08-14
- 板: Z1 (MT6572, MT6627N RF) — 关机状态, 纯只读源码分析
- 源码:
  - userspace launcher: `/tmp/connsys_build/tools/launcher/stp_uart_launcher.c` (57KB)
  - kernel WMT/STP (out-of-tree, 实际加载): `/tmp/connsys_build/conn_soc/` (core/ + linux/)
  - bring-up 脚本: `/tmp/connsys_build/tools/{connsys-up.sh,bt-up.sh,wifi-up.sh,S99bt}`
  - 对照 (fork_linux 内的老拷贝, 非 BTIF 版): `fork_linux/drivers/misc/mediatek/connectivity/common/` — 注意该拷贝的 `wmt_lib_set_hif` **没有** BTIF 分支, 不能用于本结论; 本报告以内核实际加载的 conn_soc 为准

---

## 0. 一句话结论

`mtk_stp_launcher` 是**常驻 daemon**(非跑完就退)。`g_stp_if_type` 和 `wmtHifConf.hifType` 是**内核静态变量**, 一旦 launcher 的 `WMT_IOCTL_SET_STP_MODE` ioctl 落地就**持久有效**(与 launcher 进程死活无关, 只要 `mtk_stp_wmt_soc.ko` 还加载着)。但内核**没有任何保底**: 若 launcher 从未跑过 SET_STP_MODE, `g_stp_if_type = STP_MAX_IF_TX`(STP 全部 TX 返回 -1), `wmtInfoBit` 无 `WMT_OP_HIF_BIT`, 任何 func-on 在 `wmt_core_stp_init()` 处直接 `"no hif info!"` 失败。开机竞态窗口 = **模块加载完成(可发 func-on)到 launcher 完成 SET_STP_MODE 之间**; 现行 `connsys-up.sh` 用 `sleep 2` 硬扛, 不保证。

---

## 1. launcher 主流程 (main 入口到退出)

文件 `/tmp/connsys_build/tools/launcher/stp_uart_launcher.c`:

### 1.1 启动阶段 (main, L1390)

| 步骤 | 位置 | 说明 |
|---|---|---|
| 打开 `/dev/stpwmt` | L1405-1413 | `open(CUST_COMBO_WMT_DEV, O_RDWR|O_NOCTTY)` (L58: `"/dev/stpwmt"`)。失败则 `usleep(300000)` 每 300ms 无限重试直到成功 |
| 查 chip id | L1416-1428 | `query_chip_id()` (L1163): 先读 Android 属性 `persist.mtk.wcn.combo.chipid`; mainline 无 Android 属性系统 → 落到 `ioctl(gWmtFd, WMT_IOCTL_WMT_QUERY_CHIPID)` (L1190)。失败每 300ms 重试直到 chipId != -1 |
| 判断 SOC / combo 流程 | L1430-1439 | chipId=0x6572 ∈ SOC 名单 (L1436) → `gStpMode = STP_BTIF_FULL`(3) 默认, 走 **SOC flow** |
| 参数解析 | L1442-1463 | `getopt("m:d:b:c:p:n:?")`。`-m 3` → `gStpMode=3`(覆盖默认); `-p` → `gPatchFolder` |

### 1.2 关键 ioctl (SOC flow, L1464-1467) —— 竞态核心点

```
L1465  ioctl(gWmtFd, WMT_IOCTL_SET_PATCH_NAME, gPatchName);      /* 默认空串 */
L1467  ioctl(gWmtFd, WMT_IOCTL_SET_STP_MODE,
              ((gFmMode & 0x0F) << 4) | (gStpMode & 0x0F));       /* gFmMode=2 (comm I/F) */
       /* = (2<<4)|3 = 0x23 */
```

- **这一条 ioctl 就是设置 `g_stp_if_type` + `wmtHifConf.hifType` 的唯一时机**(见 §3)。
- 返回值**未检查**(SOC flow), 即内核拒绝也照走。
- 注意 SOC flow **不走** combo flow 的 `setHifInfo()`/`get_wmt_cfg()`/`WMT_IOCTL_WMT_CFG_NAME`/`WMT_IOCTL_PORT_NAME`(那些是 L1530-1638 combo 分支)。SOC 分支直接 `SET_PATCH_NAME → SET_STP_MODE` 就完事。

### 1.3 pwr-on 线程 (L1338 `launcher_pwr_on_chip`)

- L1641 `ioctl(WMT_IOCTL_SET_LAUNCHER_KILL, 0)`(通知内核 launcher 活着)。
- L1643 `pthread_create(pwr_on_chip)`:
  - L1350 循环最多 20 次、每次间隔 1s: `ioctl(WMT_IOCTL_LPBK_POWER_CTRL, 1)` — 这是 `func_on(LPBK)` → 内核 `opfunc_pwr_on` → **整颗 CONNSYS 上电 + WMT firmware patch 下载**(补丁下载发生在这次上电的 `sw_init` 流程里)。
  - L1362-1373 设 `service.wcn.driver.ready="yes"`(Android 属性, mainline 上无实际作用)。
  - L1375-1376 线程 detach 后**退出**(只有这个线程退, 主线程还在)。

### 1.4 主线程常驻循环 (L1676-1735) —— 常驻 daemon 本体

```
L1676  while (!__io_canceled) {
L1683      poll(fds, fd_num=1, 2000);          /* 只 poll /dev/stpwmt */
L1704      if (fds[0].revents & POLLIN) {
               read(gWmtFd, gCmdStr, ...);     /* 读内核下发的命令串 */
               err = handle_cmd(&sStpParaConfig, gCmdStr, len);
               write(gWmtFd, "ok" / "resp_x", ...);  /* 回响应 */
           }
       }
```

- 退出条件只有 `__io_canceled`:
  - `SIGTERM/SIGINT` → `sig_term` (L339-344): `__io_canceled=1` + `ioctl(WMT_IOCTL_SET_LAUNCHER_KILL, 1)`。
  - `SIGHUP` → `sig_hup` (L334) 仅打印, 不退。
- **结论: launcher 是常驻 daemon, 跑完 SET_STP_MODE + pwr-on 后不退出**, 长期 poll `/dev/stpwmt` 服务内核命令, 直到被 SIGTERM 杀掉。

### 1.5 内核命令表 (L213-227 `cmd_hdr_table`)

```
"baud_115200_0" .. "baud_4000000_0"  → cmd_hdr_baud_*   (UART combo 流程才用)
"open_stp" / "close_stp" / "rst_stp" → cmd_hdr_stp_*    (UART combo 流程才用)
"srh_patch"                           → cmd_hdr_sch_patch (L498, CUST_MULTI_PATCH=1 版)
```

- **`cmd_hdr_sch_patch` 何时扫 `/system/etc/firmware/ROMv1_patch*`**: 只有内核经 char dev 发来命令串 `"srh_patch"` 时才执行。内核侧由 `wmt_ctrl_patch_search()` (conn_soc/core/wmt_ctrl.c:489, `WMT_CTRL_PATCH_SEARCH`) 触发, 而它只被 `mtk_wcn_soc_patch_info_prepare()` (conn_soc/core/wmt_ic_soc.c:2009, 调用点 L2014 / L2306) 调用 —— 即 **WMT func-on 的 firmware patch 下载流程中**。launcher 启动阶段**不会**主动扫 patch。
- `cmd_hdr_sch_patch` 逻辑 (L498-720): chipId 0x6572 → `chipName="ROMv1"` (L605-607) → 扫 patch 目录(默认 `/etc/firmware`, 实际 `-p /system/etc/firmware/`)下 `ROMv1_patch*` 文件, 读 offset 22 的 patch 头 2 字节与 `ioctl(WMT_IOCTL_GET_CHIP_INFO, 2)` 的 fwVersion 比 `(patchVer^fwVersion)&0xff`, 匹配则通过 ioctl 向内核登记元数据:
  - `WMT_IOCTL_SET_PATCH_NAME`(完整路径)
  - `WMT_IOCTL_SET_PATCH_NUM`(patchInfo[0]>>4, L666-668)
  - `WMT_IOCTL_SET_PATCH_INFO`(dowloadSeq + addRess + patchName, L677/689)
- **读 srh_patch 请求 / 回写 patch 元数据的交互通道** (内核 ↔ launcher, conn_soc):
  - 内核: `wmt_ctrl_ul_cmd()` (core/wmt_ctrl.c:372): 置 `WMT_STAT_CMD` 位, 拷命令到 `gDevWmt.cCmd`, 触发 `cmdReq` 事件, 在 `cmdResp` 信号上等**最多 2s** (L405 `timeoutValue=2000`)。
  - launcher 侧: `WMT_poll` (linux/pri/wmt_dev.c:1218) 见 `WMT_STAT_CMD` 置位 → POLLIN; `WMT_read` (L1179) → `wmt_lib_get_cmd()` (core/wmt_lib.c:462) 返回 `cCmd`。
  - launcher 回写: `write(gWmtFd, "ok"|"resp_x")` → `WMT_write` (wmt_dev.c:1139): 收到 `"ok"` → `wmt_lib_trigger_cmd_signal(0)`, 否则 `-1` (L1161-1170)。
  - 超时: `wmt_ctrl_ul_cmd` 返回 -2 "wait signal timeout"。**launcher 死了 / 没在 poll 就会 2s 超时**, 补丁下载失败 → 芯片跑裸 ROM。

---

## 2. 常驻性 / g_stp_if_type 持久性 / 内核保底(任务关键问)

### 2.1 launcher 是常驻 daemon 吗?

**是。** 见 §1.4: 主线程在 `poll(/dev/stpwmt)` 上无限循环, 只有 SIGTERM/SIGINT 才退出。SET_STP_MODE + pwr-on 只是启动期的前置动作, 不是一次性执行完退出。

### 2.2 执行完 SET_STP_MODE + patch 登记就退出, g_stp_if_type 是否持久?

**持久, 与 launcher 进程无关。** 两个都是内核态全局:

- `g_stp_if_type`: conn_soc/linux/pri/stp_exp.c:56 `static ENUM_STP_TX_IF_TYPE g_stp_if_type = STP_MAX_IF_TX;` — 模块加载期初始化为 `STP_MAX_IF_TX`, 只被 `mtk_wcn_stp_set_if_tx_type()` (L163, 由 `wmt_lib_set_hif` 调用) 改写。**launcher 进程退出不清零**。
- `wmtHifConf.hifType` + `wmtInfoBit`: 内核 `gDevWmt.rWmtHifConf` (core/wmt_lib.c) 与 `gMtkWmtCtx.wmtInfoBit/wmtHifConf` (core/wmt_core.c), 同样内核全局, launcher 死后仍在。

→ 只要 SET_STP_MODE 落地过,**之后 launcher 死不死, 内核的 if_type/hifType 都有效**。

### 2.3 launcher 退出/未跑, 内核有无保底?

**没有, 两条硬门槛:**

1. **STP TX 层**: `g_stp_if_type` 默认 `STP_MAX_IF_TX` → `mtk_wcn_sys_if_tx()` (stp_exp.c:84-101) 所有分支都不匹配, 直接 `return -1` → 任何 STP 数据包都发不出去。
2. **func-on 层**: `wmt_core_stp_init()` (core/wmt_core.c:647-651):
   ```
   if (!(pctx->wmtInfoBit & WMT_OP_HIF_BIT)) {
       WMT_ERR_FUNC("WMT-CORE: no hif info!\n");
       osal_assert(0);
       return -1;
   }
   ```
   `WMT_OP_HIF_BIT` 只有 `opfunc_hif_conf()` (wmt_core.c:873) 置位, 而 `opfunc_hif_conf` 只被 `WMT_IOCTL_SET_STP_MODE` 路径(wmt_dev.c:1896-1901 排队 `WMT_OPID_HIF_CONF` op)触发。**没有别的入口**(conn_soc/wmt_exp.c:100-109 明确注释: FUNC_ON/FUNC_OFF op 故意**不**带 HIF_BIT, 防止覆盖 launcher 写的合法 hifType=BTIF)。

→ **launcher 未跑 SET_STP_MODE → 任何 func-on(BT/WiFi/LPBK)都在 wmt_core_stp_init 失败**, 加上 `opfunc_pwr_on` 重试预算只有 `WMT_PWRON_RTY_DFT=2` (core/include/wmt_lib.h:66), 不会给 launcher 长时间等待窗口。

> 注: 本 forward-port 的 `osal_assert` (linux/include/osal.h:342-346) 只是打印非致命, 所以竞态表现为 **func-on 失败**, 不是内核 panic。

---

## 3. 内核 ioctl 到 if_type/hifType 的完整链 (conn_soc)

1. launcher `ioctl(WMT_IOCTL_SET_STP_MODE, 0x23)` → `WMT_unlocked_ioctl` (linux/pri/wmt_dev.c:1875)。
2. `wmt_lib_set_hif(0x23)` (core/wmt_lib.c:393):
   - `val = 0x23 & 0xF = 3` = `STP_BTIF_FULL` (linux/pri/include/wmt_dev.h:22) → `pHif->hifType = WMT_HIF_BTIF`(2) (core/include/wmt_core.h:103) + `mtk_wcn_stp_set_if_tx_type(STP_BTIF_IF_TX)` → `g_stp_if_type=2` (include/stp_exp.h:87)。
3. 排队 `WMT_OPID_HIF_CONF` op, `u4InfoBit = WMT_OP_HIF_BIT` (wmt_dev.c:1896-1901)。
4. `opfunc_hif_conf` (core/wmt_core.c:860-880): `gMtkWmtCtx.wmtInfoBit |= WMT_OP_HIF_BIT`, 把 `wmtHifConf` memcpy 进内核全局。
5. 此后任意 func-on → `opfunc_func_on` → (WMT 未上电则) `opfunc_pwr_on` → `wmt_core_stp_init` 检查 HIF_BIT ✓ → `MTKSTP_BTIF_MAND_MODE` (wmt_core.c:669-673) → `sw_init` → **patch 下载**(此时内核发 `"srh_patch"` 给 launcher)。

配套 (对照, 非本机): fork_linux 老拷贝的 `wmt_lib_set_hif` **无 BTIF 分支**, `-m 3` 会走 `else { invalid stp mode }` 返回 -1。若哪天把 fork_linux 那份当内核源用, 竞态会变成 "SET_STP_MODE 永远无效"。本机实际加载的是 conn_soc, BTIF 分支在, 无此问题。

---

## 4. bring-up 脚本怎么启动 launcher

`/tmp/connsys_build/tools/connsys-up.sh`(设备上 `/root/connsys/connsys-up.sh`):

```
L6-8   insmod mtk_btif_drv.ko → mtk_stp_wmt_soc.ko → mtk_stp_bt_soc.ko
L11-12 mknod /dev/stpwmt c 190 0;  mknod /dev/stpbt c 192 0
L14-17 若未跑: ./mtk_stp_launcher -m 3 -p /system/etc/firmware/ >/tmp/launcher.log 2>&1 & ; sleep 2
L22    echo '0 0' > /proc/driver/wmt_dbg     (PSM off)
L25-26 ./stpbt-hci-test                       (开 /dev/stpbt → func_on(BT) → HCI_Reset)
```

- **参数**: `-m 3`(BTIF mode, `STP_BTIF_FULL`) + `-p /system/etc/firmware/`(patch 目录, 给 `srh_patch` 用)。**无 `-d/-b/-c/-n`**, 因为 SOC 流程不需要。
- **顺序**: launcher 后台启动 → `sleep 2` → BT func-on。这是现行唯一的时序同步手段。
- 启动链条: `/etc/init.d/S99bt` → `/root/connsys/bt-up.sh &`(boot 后台) → `connsys-up.sh` → 若 `HCI_TEST_RC!=0` 则 `bt-up.sh` 打印 "radio degraded, REBOOT" 退出。
- `wifi-up.sh` 随后: `insmod mtk_wmt_wifi_soc.ko` → `mknod /dev/wmtWifi c 155 0` → `insmod cfg80211.ko wlan_gen2.ko` → `echo 1 > /dev/wmtWifi`(func_on(WIFI)) → 等 `wlan0`。

---

## 5. 竞态窗口与正确顺序

### 5.1 依赖链(倒序)

BT/WiFi func-on 依赖 ① `g_stp_if_type` 正确(BTIF) ② `wmtInfoBit|WMT_OP_HIF_BIT`(HIF 已配) ③ WMT 已上电 + STP ready(launcher pwr-on 线程完成补丁下载) ④ launcher 仍在 poll(`srh_patch` 靠它, 2s 超时)。

- `/dev/stpbt` open (`stp_chrdev_bt.c:736`) → `func_on(BT)`, 且要 `mtk_wcn_stp_is_ready()`(L744), 否则 `-ENODEV`。
- `/dev/wmtWifi` 写 `1` → func_on(WIFI)。
- ①+② 由 launcher 的 **SET_STP_MODE** ioctl 一次性提供; ③+④ 由 launcher 的 **pwr-on 线程**(LPBK func-on, 内含补丁下载, 触发 `srh_patch` → 又依赖 ④ launcher 在线)提供。

### 5.2 竞态窗口

```
t0   insmod 完成, /dev/stpwmt 存在 (此时任何进程都能发 func-on ioctl)
t1   launcher open /dev/stpwmt (可能 300ms×N 重试)
t2   launcher query_chip_id 成功 (可能 300ms×N 重试)
t3   launcher SET_PATCH_NAME + SET_STP_MODE 落地  ← ①+② 满足
t4   launcher pwr-on 线程: LPBK func-on + patch 下载完成  ← ③+④ 满足
```

- **竞态窗口 = [t0, t3)**: 窗口内到达的 BT/WiFi func-on 在 `wmt_core_stp_init` 撞 `"no hif info!"` 失败。`opfunc_pwr_on` 只有 2 次重试, 每次重试间隔≈一次 stp_deinit+pwr_off 的时间, 不是长窗口。
- **[t3, t4) 还有第二个竞态**: SET_STP_MODE 已落地但 WMT 尚未上电/STP 未 ready 时, `/dev/stpbt` open 会因 `!mtk_wcn_stp_is_ready()` 返回 -ENODEV("STP is not ready"); 且此时若 WMT 正在被 launcher 自己上电, 外部 func-on 会撞 `opfunc_pwr_on` 的 `"already powered on"` 分支(wmt_core.c:917-922)。
- **触发源**: 开机自启链 `S99bt → bt-up.sh → connsys-up.sh` 里 `sleep 2` 后立即 `stpbt-hci-test`;若模块加载慢/`query_chip_id` 多轮重试, `t3` 可 >2s, `sleep 2` 兜不住。`wifi-up.sh` 若在 launcher 完成前并行 `echo 1 > /dev/wmtWifi` 同样撞窗。
- 影响: `HCI_TEST_RC!=0` → `bt-up.sh` 判 "radio degraded" 退出要求 reboot(README: 失败不可重试)。

### 5.3 正确顺序(launcher 生命周期全景)

1. insmod 链 `btif → wmt_soc → bt_soc`(顺序固定, 依赖)。
2. `mknod /dev/stpwmt c 190 0`(major 190)。
3. **launcher `-m 3 -p <patchdir>`**:
   - open `/dev/stpwmt` → query chip id(SOC flow) → `SET_PATCH_NAME` → **`SET_STP_MODE 0x23`**(置 hifType=BTIF + g_stp_if_type=BTIF + wmtInfoBit|HIF_BIT)→ `SET_LAUNCHER_KILL 0`。
   - pwr-on 线程: `LPBK_POWER_CTRL 1` → 内核上电 + `sw_init` 补丁下载 → 内核发 `"srh_patch"` → launcher 扫 `/system/etc/firmware/ROMv1_patch*` 并 ioctl 回登记 `SET_PATCH_NAME/NUM/INFO`。
   - 主线程进入常驻 poll 循环。
4. **等到 ③ 完成后再允许任何 BT/WiFi func-on**: 判定点应是 dmesg 出现 `set STP_IF_TX to BTIF` + WMT 上电完成(pwr-on ioctl 返回 0 / STP ready), 而非固定 `sleep 2`。
5. 之后 BT(func-on → HCI)/WiFi(`echo 1 > /dev/wmtWifi`)可安全进行; launcher 保持常驻供后续 `srh_patch`/rst 命令使用, 直至系统关机。

### 5.4 建议的同步手段(只读分析, 未实施)

- 最稳: 把 `connsys-up.sh` 的 `sleep 2` 换成**事件等待** — 轮询 `dmesg | grep "set STP_IF_TX to BTIF"`(g_stp_if_type 落地)再加 WMT ready 标志;或让 launcher 在 SET_STP_MODE 成功后写一个就绪文件/退出码, 脚本等它。
- 内核侧兜底(若改源码): 在 `wmt_core_stp_init` 的 `no hif info` 分支把 `WMT_PWRON_RTY_DFT` 窗口拉长/增加等待, 或提供默认 BTIF fallback;但改动面大, 且会掩盖 launcher 真没跑的问题。
- 注意 `srh_patch` 2s 超时: 若 launcher 常驻但卡在别处, 补丁登记会超时失败 → 芯片裸 ROM(BT 可能仍通, Wi-Fi RAM code 必死, 见 README)。

---

## 6. 关键文件与行号索引

| 主题 | 文件 | 行 |
|---|---|---|
| launcher main | tools/launcher/stp_uart_launcher.c | L1390 |
| SOC flow + SET_STP_MODE ioctl | 同上 | L1436-1467 |
| pwr-on 线程 (LPBK func-on) | 同上 | L1338-1379 |
| 常驻 poll 循环 | 同上 | L1676-1735 |
| cmd_hdr_table / srh_patch | 同上 | L213-227 / L498-720 |
| sig_term (退出) | 同上 | L339-344 |
| wmt_lib_set_hif (hifType+if_type) | conn_soc/core/wmt_lib.c | L393-419 |
| g_stp_if_type 静态变量 | conn_soc/linux/pri/stp_exp.c | L56 |
| STP TX 门控 | 同上 | L84-101 |
| SET_STP_MODE ioctl 处理 + HIF_CONF op | conn_soc/linux/pri/wmt_dev.c | L1875-1909 |
| "no hif info" 门槛 | conn_soc/core/wmt_core.c | L647-651 |
| opfunc_hif_conf 置 HIF_BIT | conn_soc/core/wmt_core.c | L860-880 |
| WMT_PWRON_RTY_DFT=2 | conn_soc/core/include/wmt_lib.h | L66 |
| srh_patch 触发源 | conn_soc/core/wmt_ic_soc.c | L2009-2018, L2306 |
| ul_cmd 2s 超时 | conn_soc/core/wmt_ctrl.c | L372-425 |
| /dev/stpbt open → func_on(BT)+is_ready | conn_soc/linux/pub/stp_chrdev_bt.c | L731-758 |
| BTIF 枚举 | conn_soc/{linux/pri/include/wmt_dev.h, core/include/wmt_core.h, include/stp_exp.h} | L22 / L103 / L85-89 |
| launcher 启动脚本 | tools/connsys-up.sh | L14-17 |
| 开机自启链 | tools/S99bt + bt-up.sh | — |
