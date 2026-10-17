# Z1 / MT6572 当前状态与恢复任务（2026-07-17）

> 本文件用于跨会话恢复。历史 Markdown 多数停在 2026-07-13，必须以 2026-07-17 的镜像、日志和实测归档覆盖旧结论。

## 当前实机状态

- 2026-07-17 19:21 后，修正 `/chosen/ranges` 的 simplefb v2 镜像已完成安全刷写及读回前缀校验。
- Z1 已恢复 Linux 7.0.0-rc7 mainline 的 2 MiB layout2m 基线镜像。最近一次硬件访问因电池电量低而停止；当前没有活动刷写等待器，也不能假设 C3/普通 USB 仍在线。充电完成前只做离线工作。
- simplefb 已完成实机 PASS：`fb0` 位于 `0xbfd00000`，大小 `0x28000`，240×320 RGB565，stride 512；用户先实际看到了全屏随机彩色图案；随后又确认 8 条 RGB565 竖直色带规整、顺序和颜色正确。两轮测试后 framebuffer 原内容 MD5 均可一致恢复，fbcon 文字也恢复。
- 本机未焊音量加/减开关，仅留焊盘；因此 KPD 只能确认 driver probe/input0，不能通过正常实体按键验证扫描码。未证实焊盘电气关系前禁止短接。
- 实机证据归档：`bridge_backups/products/products_simplefb_ranges_board_pass_20260717_200031/`；规则色条追加验证：`bridge_backups/products/products_simplefb_colorbars_pass_20260717_201735/`。

## 已上板验证

- Linux 7.0.0-rc7 已在 Z1 启动；旧文档中的“LK jump 后 early reset”已过时。
- USB 基线镜像：`out/boot_mainline_mt6572_real_usbphy_musb.img`
- SHA256：`e7bf5a1a6bd16c25e546425cfd3a5250e49d59a986b171eee44dd7ab8cd14014`
- USB2 PHY + MUSB peripheral + `g_serial` 已完整验证：
  - 内核识别真实 MT6572 USB2 PHY；
  - HS slew calibration 当前超时，安全回退值 4；
  - UDC `musb-hdrc.3.auto` 与 `/dev/ttyGS0` 存在；
  - 主机枚举 `0525:a4a7 Gadget Serial v2.4` 并出现 `/dev/ttyACM1`；
  - Host→Z1 与 Z1→Host 双向数据均 PASS。
- 证据：`bridge_backups/flash_mt6572_real_usbphy_musb_20260717_104022/`。

## simplefb 第一次上板结果：失败但根因明确

- 已测试镜像：`out/boot_mainline_mt6572_simplefb_kpd_auxadc.img`
- SHA256：`0d38b3762ffc44f846ec6a283ddaaabe4854f9c7beb3e07408008942291b2192`
- 启动日志：`mainline_recon/z1_boot_mainline_20260717_161221.log`
- 日志 SHA256：`db38eae569d7375271085aaec2b662a5149d402da29c32e562a99331840c0765`
- Linux 正常启动到 `/init`，KPD、USB PHY/MUSB、g_serial 仍正常 probe；simplefb 报：
  - `No memory resource`
  - `probe with driver simple-framebuffer failed with error -22`
- 根因：simplefb 是 `/chosen` 子节点；`/chosen` 有 `#address-cells`/`#size-cells`，但缺少空 `ranges;`。OF 地址转换因此无法把 `reg` 转为 `IORESOURCE_MEM`。
- 第一版及继承它的旧 Lima 镜像已标为 FAILED/SUPERSEDED，不得继续刷写。

## simplefb 第二版：实机验证通过

- DTS 只新增 `/chosen { ranges; };`，framebuffer 参数未改：
  - 地址 `0xbfd00000`
  - 大小 `0x28000`
  - 240×320
  - stride 512
  - `r5g6b5`
- 使用已上板验证的 USB2/MUSB 原始 zImage 与相同 ramdisk；内核代码未变。
- 镜像：`out/boot_mainline_mt6572_simplefb_ranges_kpd_auxadc.img`
- SHA256：`d28e0203a95b289a73f84ca3db8b5bcb3f15592433fc09bdd<NUM15>`
- 大小：5,429,248 bytes；6 MiB boot 分区余量 862,208 bytes。
- 归档：`bridge_backups/products/products_simplefb_ranges_kpd_auxadc_20260717_163105/`
- Android header、MTK kernel/ramdisk header、独立 DT、append DT、大小和归档 SHA256 均通过。
- `dtbs_check` 未运行成功，原因仅为主机未安装 `dtschema`/`dt-doc-validate`；DTC 编译和 DTB 反编译检查通过。
- 状态：BOARD PASS。刷写、读回校验、simplefb probe、几何、全屏可见写入和内容恢复均已验证；证据归档见 `products_simplefb_ranges_board_pass_20260717_200031/`。

## KPD / AUXADC

- KPD 描述已由错误的 5×3 改为固定 72-slot 的 8×9 逻辑空间，仅保留有证据的 slot 0/9（音量加/减）。
- 启动日志确认 `mt6779-keypad` 成功 probe 为 `input0`。本机未焊音量键，无法正常产生 slot 0/9 实体事件；仅在焊盘电气关系经原理图/连续性测量证实后才考虑注入测试。
- AUXADC unit-address 已由 `adc@110013000` 修为 `adc@11003000`，`reg` 未改；功能尚未验证。
- DTB 归档：`bridge_backups/products/products_kpd_auxadc_dts_20260717_121433/`。

## GPU / Lima

- GPU IP：ARM Mali-400MP1（Utgard）；主线驱动为 Lima，不是 Panfrost。
- fresh Lima 镜像 `out/boot_mainline_mt6572_lima_simplefb_fresh.img` 已安全刷写并完成读回校验；SHA256 `5a0c7358ec7ac2b4963373f873ba9190abf0bfee0caebedae304dde8fbee9641`。
- 上板 probe PASS：GP 1.1、PP0 1.1、32 KiB L2、bus/mod clock 416 MHz；`[drm] Initialized lima 1.1.0`，并出现 `/dev/dri/card0`、`/dev/dri/renderD128`。runtime PM 可在 active/suspended 间正常转换。
- Lima UAPI PASS：GPU_ID/NUM_PP/GP_VERSION/PP_VERSION、context create/free、GEM create/info/close 均成功。
- Mesa 26.1.1 EGL surfaceless 实际命令提交 PASS：`GL_RENDERER=Mali400`；离屏 RGB 三角形 `glDrawArrays + glFinish + glReadPixels` 成功，GP/PP IRQ 同步递增，GPMMU/PPMMU IRQ 为 0，无 fault/hang/timeout。
- 240×320 RGB565 回写 simplefb PASS；静态三角形可见。随后完成 6000 帧实时动画：用户确认彩色三角形持续旋转、缩放和移动；最终 GP IRQ=6001、PP0 IRQ=6000，`ANIMATION=PASS`、退出码 0，runtime PM 回到 suspended。
- 动画前后 framebuffer SHA256 均为 `9cdcbc67161c76ff62c27bb63858a4a4e346664742635bdfcf64519f6df29ebb`，证明 Linux 文字画面已字节一致恢复。
- 证据归档：`bridge_backups/products/products_lima_board_probe_uapi_pass_20260717_222618/` 与 `bridge_backups/products/products_lima_realtime_animation_pass_20260717_000824/`。
- 当前显示路径仍是 GPU 离屏渲染 → CPU `glReadPixels`/RGB565 转换 → simplefb；这证明 3D 执行，不等于原生 DRM/KMS 零拷贝扫描输出。下一显示 gate 是 MMSYS/SMI read-only，再分离推进 OVL/RDMA、DSI/ST7785M。

## 显示

- 面板：`st7785m_qvga_dsi_vdo_hsd`，240×320，DSI video mode。
- LK 已初始化 DSI/OVL；最终 OVL layer 2 scanout 证据为 `0xbfd00000`、pitch 512、vendor format 1=RGB565。
- `512*320=0x28000`，且 LK layer 3 从 `0xbfd28000` 开始，验证了 surface 边界。
- DTS 12 MiB framebuffer reserved-memory 为 `0xbf400000..0xbfffffff`，覆盖目标 surface。
- 原生 DRM/DSI 后置：需要 MT6572 DDP data、IOMMU/SMI、DSI graph、ST7785M panel 时序；不得与 simplefb 首次成功同批修改。
- MMSYS/SMI 隔离 gate 0 已上板通过：外部只读审计模块以 `read_mask=0` 仅解析 DT 资源，确认 MMSYS `0x14000000`、SMI AO `0x1000e000`、SMI EXT `0x14011000`、LARB0 `0x14010000`、M4U `0x10203000`，每段均为 `0x1000`；未执行 `of_iomap`、MMIO 读写、时钟/reset/PM/IRQ/IOMMU 操作。
- 模块 `insmod`/`rmmod` 均成功；卸载后 simplefb、Lima render node、USB gadget 正常，Lima runtime PM 为 suspended，无 Oops/panic/external abort/GPU fault。证据见 `mainline_recon/mt6572_mm_ro_probe_20260717/`。
- 非零 read mask 尚未授权。C3 当前收不到 Z1 UART 字节，必须先恢复独立监视通道，再单独测试 MMSYS status (`read_mask=1`)。

## MT6323 poweroff

- 上游已有 `drivers/power/reset/mt6323-poweroff.c`，`mt6323.dtsi` 也有 `mediatek,mt6323-pwrc`；旧结论“没有驱动”不准确。
- 已完成最小编译审计：`MFD_MT6397=y + POWER_RESET_MT6323=y` 可链接，归档 `products_mt6323_poweroff_compile_audit_20260717_164951/`；审计后 `.config` 已原样恢复。
- 该驱动依赖 MT6397/MT6323 MFD 实例化。当前源码配置仍有 `# CONFIG_MFD_MT6397 is not set`，所以仅设置 `CONFIG_POWER_RESET=y` 不会得到 `CONFIG_POWER_RESET_MT6323`。
- `out/boot_mainline_mt6572_lima_simplefb_poweroff.img` 名称具有误导性：实际未包含 MT6323 poweroff，已标为 INVALID/DO NOT FLASH。
- PMIC MFD 附带设备已完成审计。最初 MFD-only 镜像
  `26e8d4...bd42` 已标为 NO-GO：`PINCTRL_MT6397=y` 且未显式禁用
  `mt6397codec`。严格替代候选已离线通过：仅
  `MFD_MT6397=y`/`MTK_PMIC_WRAP=y`，PMIC 子驱动与所有直接 DT 子节点均关闭；
  镜像 `out/boot_mainline_mt6572_lima_simplefb_mfd_only_strict.img`，
  SHA256 `8c53ccf62029a4813ab897b6bc88a87717a3a94aca9e576ff794588ad79d1c85`。
  2026-07-18 上板后，PWRAP/MFD 绑定正常，一次性只读 regmap 探针读到
  `raw=0x2023 cid=0x23`，IRQ 无增长且无 PMIC/PWRAP fault。但该镜像误用
  旧 `0x80000000` RAM 布局，没有保留已经通过的 layout2m 基线，因此正式
  Gate 判定 INVALID/SUPERSEDED，禁止再次刷入。必须从 layout2m 构建新的
  单变量 MFD-only strict 候选后重测。详见
  `MT6323_MFD_ONLY_STRICT_BOARD_RESULT_20260718.md`。

### corrected layout2m + strict MFD-only

- corrected image:
  `out/boot_mainline_mt6572_lima_simplefb_layout2m_mfd_only_strict.img`
- SHA256:
  `e1439d7474aa49eab88b820519cff8753d8ccc174398b6a099bdaf83253a0871`
- exact delta: preserve fixed `0x80208000` decompression and
  `0x80200000..0xbfffffff` DT RAM; enable only MT6323 MFD parent; keep all
  PMIC child Kconfig symbols and all nine direct DT child nodes disabled.
- validator now directly scans `zImage`, runtime-decompiles the exact DTB,
  inspects archives/`vmlinux`, verifies disabled-node MFD semantics, checks
  source-audit equality and binds the corrected CID helper.
- candidate and external manifests pass; flash launcher preflight prints
  `VALIDATION=PASS`, both manifest PASS markers and
  `BOARD_GATE_PREFLIGHT=PASS`.
- independent Terra re-audit verdict: **OFFLINE BOARD-CANDIDATE GO**.
- product archive:
  `bridge_backups/products/products_mt6572_layout2m_mfd_only_strict_candidate_20260718_164718/`
- archive manifest SHA256:
  `003731ae40e6b9634999f7a76cd82ec1368f488b23cc602a8dab91c0c18795bf`
- hardware result remains UNTESTED. Do not flash before the battery is
  charged and the documented interactive gate is started.

## 其他驱动状态

- **连接架构已纠正**：Z1 是 MT6572 集成 CONNSYS，正确路径是 SOC WMT + BTIF + WLAN AHB HIF，不是外置 MT6620、STP-UART、SDIO、GPIO PMU/RST/RTC32K。
- 旧 7 个 `.ko` 只保留为外置 MT6620 分支的编译参考，已标记不适用于 Z1 上板。
- Z1 原厂日志已确证 1 MiB carveout `0x80000000..0x800fffff`、Linux 起点 `0x80100000` 和 343 KiB EMI `0x80080000`。当前 mainline 解压到 `0x80008000`，所以 reservation 失败；不是 carveout 证据不足。
- ARM mainline P:V patch 要求 2 MiB 对齐，不能直接把 `PHYS_OFFSET` 改成 `0x80100000`。隔离的 2 MiB-aligned layout-only 镜像已于 2026-07-17 完成受控刷写和完整分区读回校验：目标 SHA256 `950a13569a497f5b3365f051be36a42332f263ce537f6883f85b0428ed2d5744`。板上 `/proc/iomem` 为 `80200000-bf3fffff : System RAM`，证明低 2 MiB 未归 Linux；simplefb、Lima、`/dev/ttyGS0`、UDC 均通过，无 panic/Oops/abort/GPU fault。该 gate 未上电 CONNSYS。
- 已加入 default-off CONN genpd provider，无 consumer/上电写；zImage/DTB 编译归档：`products_mt6572_conn_genpd_compile_20260717_180122/`。
- 已完成 layout-only 编译审计：新增默认关闭的 Z1 fixed-ZRELADDR 机制；隔离构建确认解压地址 literal 仅为 `0x80208000`，DT RAM 为 `0x80200000..0xbfffffff`，归档 `products_mt6572_z1_layout2m_compile_20260717_183638/`。已打包并通过上板 gate；生产默认配置仍恢复为关闭。
- Goodix GT911 参考板节点已禁用；Z1 原厂只见 `hyn_ts`@0x15 失败，当前 `CONFIG_I2C_MT65XX` 仍关闭。
- 原生显示、音频、充电、传感器继续独立分批。

## 2026-07-18 离线审计增量

- CONNSYS chip-ID-only 驱动已在隔离源码树完成 W=1 编译：
  - 仅按 VCN18 → VCN28 → CONN genpd → 读取 `0x18070008` → 反向释放；
  - 不申请 IRQ/clock/reset，不映射 EMI，不加载固件，不执行 DMA；
  - object SHA256
    `17dd95cb2121a7d09d53f2522f50392fd66e9c943d70a2342621191f6f96b81f`；
  - DT 节点保持 disabled；没有 zImage/boot image，不能刷。
- I2C1 controller-only 已在另一隔离源码树完成 W=1 编译：
  - 生产源码未改，隔离配置仅启用 `CONFIG_I2C_MT65XX=y`；
  - object SHA256
    `1d372df3fae78d4aba26d121704aa6975596ce4c892dc9bba7f6148c58e476fc`；
  - Z1 DTB 确认 I2C1 okay、GT911 disabled；没有 zImage/boot image。
- 音频审计确认当前主线缺少 MT6572 AFE platform driver，也没有已验证的
  MT6323 codec 路径。厂商 3.4 代码依赖 raw PWRAP、raw AFE 寄存器和旧
  clock manager，现阶段只能继续提取寄存器/时钟/路由证据，不能打包上板。
- MMC 审计确认 `mmc0` 是 eMMC，不是 Wi-Fi/TF；当前 DTS 虽列出 8 根数据
  pin，却配置 `bus-width = <1>`，且依赖 MT6323 VEMC3V3/VIO18。`mmc1`
  仍 disabled，尚无 Z1 TF 卡槽/card-detect/供电证据。
- 基带审计确认厂商启用 MD1/CCCI，涉及 modem/DSP 内存、2 MiB shared
  memory、remap/MPU、固件校验和用户态服务；主线没有 MT6572 CCCI/CCIF
  或 Z1 DTS 节点，stock scatter 也没有独立 modem 分区项。严禁直接启动、
  reset 或加载基带固件。
- 详细恢复入口：
  - `CONNSYS_WIFI_BT_NEXT_GATE_20260717.md`
  - `MT6323_REGULATOR_NEXT_GATE_20260718.md`
  - `I2C_TOUCH_CONTROLLER_AUDIT_20260717.md`
  - `PRIORITY_DRIVER_GATE_MATRIX_20260718.md`

## MT6323 regulator read-only prerequisite

- Added `mainline_recon/mt6323_regulator_state_audit_20260718/`.
- The external module reads CID and the enable/QI state of all 28 upstream
  MT6323 regulator entries; it does not call a regmap write/update function or
  any regulator register/enable/disable/set API.
- Module SHA256:
  `9d6b7db8dee88e62872f73942abbe6e7057d2d649e3eaf21ca2d15e6e76be680`.
- Static validator confirms exact register/bit mapping, candidate ABI/imports
  and a self-contained transfer/run/unload helper.
- Run it only after the corrected strict MFD-only board gate passes. It needs
  no new boot image and must unload immediately after collecting the rail
  snapshot.
- Enabling the unmodified full regulator driver remains NO-GO: DT constraint
  application, boot/always-on enables and 30-second unused-rail cleanup can
  write or disable multiple system rails.

## 恢复任务列表

以 `mainline_recon/ACTIVE_TASKS_20260717.md` 为准。用户指定顺序为：
Wi-Fi/蓝牙 → 音频 → 触摸 → 按键 → TF/MMC → 基带。Wi-Fi/蓝牙仍必须遵守
严格 MFD-only → VCN rail evidence → chip-ID-only → BTIF → WLAN AHB HIF →
DMA/firmware 的单变量门禁；KPD 实体扫描受未焊按键限制，原生 OVL/RDMA/DSI
始终独立。

## 强制规则

- 改源/脚本/镜像/刷机前先备份到 `bridge_backups/`。
- 每次关键产物立即归档到 `bridge_backups/products/` 并生成 SHA256。
- 刷写顺序：先启动刷写程序等待 MTK 端口，再接电池/插 Z1 下载线/按开机；正常启动必须拔 Z1 下载线，C3 可保持连接。
- 分区编号文档存在冲突；写 rootfs 前必须用实机 `/proc/partitions`、dumchar、blkid 和 scatter 重新确认。
- 严格区分：编译成功、镜像格式正确、probe 成功、功能上板成功。

## Goal / loop 说明

- Codex goal 用于持续推进 GPU/显示、KPD、MT6620 和基础 SoC；goal continuation 是当前环境唯一能再次调度模型的机制。
- 当前工具集没有模型定时回调 API。`tools/z1_driver_supervisor_loop.sh`
  每 5 分钟记录文档、镜像、禁刷标记、隔离编译产物、进程和磁盘状态；
  它由 `z1-driver-supervisor.service` 持久运行并验证候选/外部 manifest；
  它是 telemetry，不能唤醒或重新派遣 Codex。
- 不安装系统 cron 自动执行 `codex exec resume`，避免与用户交互或构建并发造成源码竞态。
