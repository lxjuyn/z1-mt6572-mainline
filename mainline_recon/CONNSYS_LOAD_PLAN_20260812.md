# Z1 (MT6572) CONNSYS WiFi/BT 集成 — 加载方案 (2026-08-12)

路径: Z1 走 **MT6572 集成 CONNSYS** (die 上 MAC/baseband + MT6627N 仅 RF 前端),
非外部 SDIO/UART combo 芯片。外编 .ko 对 fork_linux 7.0-rc7 已编出, 本文件是
insmod / DT / 固件 / connsys-up / 验证判据的完整操作单。

## 1. 模块清单 (外编, /tmp/mt6572-connsys-modules/)

| .ko | 功能 | 绑定 DT compatible |
|---|---|---|
| `btif/mtk_btif_drv.ko` | BTIF 传输 (PIO+APDMA) | `mediatek,btif` (+子节点 `mediatek,btif_tx`/`btif_rx`) |
| `conn_soc/mtk_stp_wmt_soc.ko` | WMT/STP 控制面 + 固件下载 | `mediatek,mt6572-consys` |
| `conn_soc/mtk_stp_bt_soc.ko` | BT 通道 `/dev/stpbt` | (靠 WMT 内部, 无 DT 节点) |
| `conn_soc/mtk_wmt_wifi_soc.ko` | WiFi func ctrl `/dev/wmtWifi` | (靠 WMT 内部) |
| `wlan/wlan_gen2.ko` | cfg80211 `wlan0` | `mediatek,wifi` |

前置内核模块: `cfg80211.ko` (wlan 之前), `CONFIG_BT/HCIVHCI/HIDP/UHID` (BT 全栈)。
vermagic 必须与运行内核完全一致; 重编内核后整套 .ko 一起重编重发。

## 2. DT 节点 (已入 mt6572-z1.dts, 见 git 提交)

三个新节点已加到 `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts`,
独立于旧的 `connsys_pwr` 节点 (compatible `mediatek,mt6572-connsys` 仍 `disabled`,
避免双重供电 CONN 域/regulator)。

### btif@1100e000 (`mediatek,btif`)
```dts
btif: btif@1100e000 {
    compatible = "mediatek,btif";
    #address-cells = <1>;
    #size-cells = <1>;
    reg = <0x1100e000 0x1000>,
          <0x11000380 0x80>,   /* APDMA VFF TX */
          <0x11000400 0x80>;   /* APDMA VFF RX */
    interrupts = <GIC_SPI 52 IRQ_TYPE_LEVEL_LOW>;          /* MT_BTIF_IRQ_ID */
    clocks = <&topckgen CLK_TOP_BTIF>, <&topckgen CLK_TOP_APDMA>;
    clock-names = "btifc", "apdmac";

    btif_tx: btif_tx@11000380 {
        compatible = "mediatek,btif_tx";
        reg = <0x11000380 0x80>;
        interrupts = <GIC_SPI 41 IRQ_TYPE_LEVEL_LOW>;      /* MT_DMA_BTIF_TX_IRQ_ID */
    };
    btif_rx: btif_rx@11000400 {
        compatible = "mediatek,btif_rx";
        reg = <0x11000400 0x80>;
        interrupts = <GIC_SPI 42 IRQ_TYPE_LEVEL_LOW>;      /* MT_DMA_BTIF_RX_IRQ_ID */
    };
};
```

### connectivity@18070000 (`mediatek,mt6572-consys`)
```dts
connectivity: connectivity@18070000 {
    compatible = "mediatek,mt6572-consys";
    reg = <0x18070000 0x1000>,   /* reg[0] CONN MCU config, chip ID @+0x8 (0x6572) */
          <0x10000000 0x2000>;   /* reg[1] TOPCKGEN block base (驱动 of_iomap(node,1)
                                    后访问 base+0x1800 = 0x10001800 AP2CONN_OSC_EN) */
    interrupts = <GIC_SPI 122 IRQ_TYPE_LEVEL_LOW>,  /* conn2ap / BTIF wakeup (BGF EINT) */
                 <GIC_SPI 124 IRQ_TYPE_LEVEL_LOW>;  /* CONN WDT */
    mediatek,pwrap-regmap = <&pwrap>;               /* mtk_wcn_consys_hw.c 需 pwrap regmap */
    power-domains = <&spm MT6572_POWER_DOMAIN_CONN>; /* CONN genpd, 默认 off, pm_runtime_get 开 */
    resets = <&watchdog MT6572_TOPRGU_CONNSYS_SW_RST>;  /* TOPRGU 0x10007018 bit12 */
    reset-names = "connsys";
    vcn18-supply = <&mt6323_vcn18_reg>;
    vcn28-supply = <&mt6323_vcn28_reg>;
    vcn33_bt-supply = <&mt6323_vcn33_bt_reg>;
    vcn33_wifi-supply = <&mt6323_vcn33_wifi_reg>;
};
```

### wifi@180f0000 (`mediatek,wifi`)
```dts
wifi: wifi@180f0000 {
    compatible = "mediatek,wifi";
    reg = <0x180f0000 0x1000>;              /* CONSYS Wi-Fi AHB 窗口 (驱动内硬编码 HIF_DRV_BASE) */
    interrupts = <GIC_SPI 123 IRQ_TYPE_LEVEL_LOW>;  /* WF_HIF_IRQ_ID */
};
```

IRQ 号来源: vendor `mediatek/platform/mt6572/kernel/core/include/mach/mt_irq.h`
(`GIC_PRIVATE_SIGNALS + N`)。已实测编入 dtb (SPI 52/41/42/122/123/124 全部正确)。

## 3. reset 头 (已改)

`include/dt-bindings/reset/mediatek,mt6572-resets.h` 补:
```c
#define MT6572_TOPRGU_CONNSYS_SW_RST   12   /* TOPRGU 0x10007018 bit12 */
#define MT6572_TOPRGU_SW_RST_NUM       13
```
`drivers/watchdog/mtk_wdt.c` (mt6572_data) 的 reset controller 对 id=12 写
`BIT(12)|WDT_SWSYS_RST_KEY` 到 0x10007018 —— 正是 vendor CONNSYS CPU SW reset
序列 (`mtk_wcn_consys_hw.c` 注释 "[12]=1'b1, [31:24]=8'h88 key")。

## 4. 固件部署

`scripts/deploy_connsys_fw.sh [ROOT]` (ROOT 默认 `/`, 可指 SD rootfs 挂载点):
- 源: `out/firmware_backup/firmware/` (全部在位: WIFI_RAM_CODE_SOC 160464B,
  WMT_SOC.cfg, mt6572_82_patch_e1_{0,1}_hdr.bin)
- 落位:
  - `/lib/firmware/WIFI_RAM_CODE_MT6582` ← **WIFI_RAM_CODE_SOC 改名** (wlan
    `kalFirmwareOpen()` 第一个请求名, 必中; 另留 `WIFI_RAM_CODE_SOC` 兜底)
  - `/system/etc/firmware/mt6572_82_patch_e1_{0,1}_hdr.bin` + 同字节
    `ROMv1_patch_{0,1}_hdr.bin` (launcher 无 Android system prop 时回退名)
  - `/system/etc/firmware/WMT_SOC.cfg`
- Wi-Fi NVRAM (MAC/RF 校准) 不在本脚本范围: 需从 eMMC `/data/nvram/APCFG/APRDEB/WIFI`
  取到 `/etc/firmware/nvram/WIFI`, 否则固件每次查询报随机 MAC、单播 RX 坏。

## 5. connsys-up 流程 (insmod 顺序, 关键)

顺序由依赖决定: **btif → wmt → bt → (WiFi) cfg80211 → wlan_gen2**。
`mtk_stp_wmt_soc.ko` 在 insmod 时做 WMT init 并注册 STP 传输 (依赖 btif 已就位);
`mtk_stp_bt_soc.ko` 注册 BT 通道; `wlan_gen2.ko` 的 platform probe 只是把 WLAN
回调注册进 WMT (`HifAhbPltmProbe`), 硬件只在 func-on (`echo 1 > /dev/wmtWifi`) 才碰。

```sh
cd /root/connsys     # .ko + tools 部署目录
insmod mtk_btif_drv.ko
insmod mtk_stp_wmt_soc.ko      # 绝不可 rmmod, 会不可中断挂死 → reboot
insmod mtk_stp_bt_soc.ko
mknod /dev/stpwmt c 190 0
mknod /dev/stpbt  c 192 0
./mtk_stp_launcher -m 3 -p /system/etc/firmware/ &   # 固件下载 + WMT handshake
echo '0 0' > /proc/driver/wmt_dbg                    # bring-up 全程 PSM 关
./stpbt-hci-test    # HCI reset 冒烟; 失败即 reboot, 不要重试/rmmod
```

WiFi:
```sh
insmod wifi/cfg80211.ko
insmod mtk_wmt_wifi_soc.ko
mknod /dev/wmtWifi c 155 0
insmod wifi/wlan_gen2.ko
echo '0 0' > /proc/driver/wmt_dbg    # PSM 对 WiFi 不可见, WiFi 会话必须常关
echo 1 > /dev/wmtWifi                # func-on: PALDO->WMT patch->wlan probe->RAM code->wlan0
# 等 ≤20s 出现 /sys/class/net/wlan0, 然后 ip link set wlan0 up; iw dev wlan0 scan ...
```

BT 全栈 (可选): `bt-up.sh` — insmod BT/HID 内核模块 → `stpbt-vhci-bridge`
(建 `hci0`, H4 重帧, PSM governor) → D-Bus + bluetoothd → 上电。

## 6. 验证判据

1. **insmod 链**: `lsmod` 见 5 个 .ko; `dmesg | grep -i "BTIF\|WMT\|CONSYS"` 无
   fatal; `mtk_btif_probe` 打印 `DO BTIF PROBE`, WMT probe 打印 `clk_infra_conn_main=...`。
2. **chip ID**: `mtk_wcn_consys_hw_init()` poll `0x18070008` == `0x6572`
   (超时打印 "known non-fatal on mt6572, FW download boots the core" 可容忍)。
3. **HCI**: `./stpbt-hci-test` 返回 0; `./hci-localver` 读到 Local Version。
   `/dev/stpbt` 单开、shell 阻塞读会卡死 console —— 只用提供工具。
4. **wlan0**: `echo 1 > /dev/wmtWifi` 后 `[ -d /sys/class/net/wlan0 ]` 成立;
   MAC 应为固定 NVRAM MAC (随机 MAC = NVRAM 没装)。
5. **WiFi 连接**: `iw dev wlan0 scan` 见 BSS; `wpa_supplicant -D nl80211` +
   `udhcpc` 拿到 IP 有流量。
6. **BT 连接**: `hci0` 出现, `bluetoothctl` pair 经典 HID 键盘, 入站重连可用
   (README `tools/kbd-pair.md` 全流程)。
7. **PSM**: `psm-sleep-probe.sh` 递增 idle 窗口 (10s→360s) 唤醒全过。

## 7. 已知坑

- 不要用 `devmem` 在 btif 空闲时读 BTIF 寄存器 → 总线挂死 → WDT reboot。
- 不要 `rmmod mtk_stp_wmt_soc`; 崩溃恢复重启别重试。
- `AP2CONN_OSC_EN` (0x10001800 bit16) 由驱动在 power-on 强制置位 —— reg[1]
  必须 map 0x10000000 且 size 盖住 0x1800 (本 dts 用 0x2000)。
- 旧 `connsys_pwr` 节点保持 `disabled`; 它绑 in-tree `mtk6572-connsys.c`,
  与 out-of-tree 栈不共存。
- 固件 `WIFI_RAM_CODE_MT6582` 必须赢 (plain `WIFI_RAM_CODE` 是另一芯片 build,
  FW PC 停在 ROM)。
