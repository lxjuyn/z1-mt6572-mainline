# MT6572 佳伯 Z1 听力机 — Mainline 7.0 音频适配方案

> 日期: 2026-07-10
> 真机 Linux 3.4.67, Android 4.4 KK1.MP7
> Mainline: Linux 7.0-rc7, fork https://github.com/mt6572-mainline/linux

---

## 芯片识别结果

### Codec 型号: 无法直接确认, **最高概率是 MT6323 PMIC 内部集成的音频 codec**

| 证据项 | 结果 |
|---|---|
| PMIC 芯片 | **MT6323** — 已确认 (fork 有 `mt6323-regulator.c`、`mt6323.dtsi`、`mt6323-auxadc.c`) |
| 独立音频 codec (Realtek/ALC/Wolfson) | **未找到** — strings 搜遍所有 audio so 无 "mt6582""mt6589""rt5640""mt6351""WM8960" 等 |
| HAL so 名 | `audio.primary.default.so` — **无平台后缀**, 非 `audio.primary.mt6572.so` |
| audio_policy.conf | 只有 `primary`/`a2dp`/`usb`/`r_submix`, **未提及 codec 名或特定 hw 模块** |
| vendor lib | 无 `/vendor/lib/audio.*.so` |

**结论**: MT6572 平台音频采用 MTK 专有架构, **不依赖独立音频 codec 芯片**, 而是通过 **PMIC MT6323 内部的 DAC/ADC+amp** 完成音频编解码。具体链路为:
- 数字域: MTK AFE (Audio Front-End) IP (SoC 内部)
- 模拟域: MT6323 PMIC 内建音频 codec (DAC/ADC/Headset detect/earpiece amp)
- 控制总线: PWRAP (PMIC wrapper, I2C 类协议)

**依据**:
1. `AudioMTKHardware`、`AudioPlatformDevice`、`AudioAnalogType` 类名表明 MTK 抽象层直接操控 SoC 内部 AFE + PMIC codec
2. `BesLoudness` (BestAudio/BES 响度增强) 和 `MtkAudioLoud` 是 MTK 专有 DSP 音效
3. MT6323 是 MTK 为 MT6572 配套的低成本 PMIC, 内建音频功能
4. vendor kernel `.config` 中 `# CONFIG_SND_SOC is not set` — 确认不用标准 ALSA SoC

### 音频控制接口 (Kernel → Userspace)

| 设备节点 | 用途 |
|---|---|
| `/dev/eac` | Enhanced Audio Codec — 音频主控制设备 (AFE 寄存器读写/流控制) |
| `/dev/ebc` | BT Codec — 蓝牙 SCO 音频路由 |
| `/dev/accdet` | Accessory Detect — 耳机/按键检测 |
| `/dev/fm` | FM 收音机音频路由 |
| `/dev/ttyC0` | 蓝牙 AT 命令通道 (ccci) |

**关键**: HAL 直接通过 ioctl 操作 `/dev/eac` 读写 MTK AFE 寄存器 (AFE 寄存器操作见 `AudioAfeReg` 类: `SetAfeReg`/`GetAfeReg`)。这是 **MTK 专有内核字符设备**, 不是标准 ALSA snd_soc 框架。

---

## 当前固件音频栈结构

### Userspace (Android 4.4 HAL 层)

```
AudioFlinger (libmedia)
  └── libaudio.primary.default.so  (MTK 专有 HAL, C++)
       ├── AudioMTKHardware / AudioMTKStreamManager / AudioMTKStreamOut
       ├── AudioPlatformDevice (模拟 mux/gain/fade)
       ├── AudioAnalogControl (模拟信号路由)
       ├── AudioDigitalControl (数字 AFE 控制)
       ├── AudioAfeReg (AFE 寄存器读写)
       ├── BesLoudness (DSP 响度增强)
       ├── MtkAudioSrc (采样率转换)
       ├── MtkAudioLoud (响度控制)
       ├── MtkAudioBitConverter (CVSD/MSBC 编解码)
       ├── SpeechDriver* (语音通话增强)
       ├── AudioFtmBase (工厂测试)
       ├── AudioMTKHeadSetMessager (耳机检测)
       │
       └── 依赖库:
           ├── libaudiocustparam.so   (NV 音量/参数)
           ├── libaudiocompensationfilter.so
           ├── libaudiocomponentengine.so
           ├── libaudiodcrflt.so
           ├── libaed.so              (降噪)
           ├── libblisrc.so           (采样率转换)
           ├── libcvsd_mtk.so         (BT SCO CVSD)
           ├── libmsbc_mtk.so         (BT SCO MSBC)
           ├── libspeech_enh_lib.so   (语音增强)
           ├── libnvram.so            (NVRAM 参数存储)
           └── libbluetoothdrv.so     (BT 音频驱动)
```

### DSP 固件

| 固件 | 路径 | 用途 |
|---|---|---|
| `mt6572_82_patch_e1_0_hdr.bin` | `/etc/firmware/` | WiFi/BT combo 固件 |
| `mt6572_82_patch_e1_1_hdr.bin` | `/etc/firmware/` | WiFi/BT combo 固件 |
| `mt6627_fm_*.bin` | `/etc/firmware/mt6627/` | FM 收音机固件 |
| (无) | (无) | **无独立音频 DSP 固件** — 音频 DSP 集成在 HAL 层 (libaed.so 等) |

**关键发现**: 没有独立的音频 DSP 固件 blob (如 Yamaha/mtk_dsp/VBox)。音频 DSP 功能 (降噪、响度增强、采样率转换) 全部由 userspace HAL 中的 C++ 库实现。

---

## Mainline 适配方案

### 路径 A: 完整移植 MTK 专有 AFE 驱动 + 重写 HAL

**方案**: 从 MTK 3.4 BSP 内核提取 AFE 内核驱动, 编译进 mainline 内核; 用 standard ALSA userspace (ALSA lib + tinyalsa) 替代 MTK HAL。

| 维度 | 评估 |
|---|---|
| 工作量 | **极大** |
| 可行性 | **低** |
| 原因 | 1) MTK 专有 AFE 驱动不在开源社区, 无法从 vendor 内核提取完整源码 (BSP 不公开)；2) HAL 层依赖 MTK 专有 API (AudioMTKHardware、AudioPlatformDevice 等)；3) Android 7+ 已全面转向 AudioPolicyConfiguration XML + audio HAL 2.0，旧版 audio.primary.default.so 架构不兼容；4) `/dev/eac` 是 MTK 专有字符设备, mainline 无等价设备 |
| 收益 | 可完整复用现有 HAL 功能, 但无法使用标准 ALSA 生态 |

### 路径 B: 构建 "dummy" ALSA sound card + 简单 I2S codec (simple-card)

**方案**: 写一个 custom kernel driver, 将 MT6323 PMIC 内建 codec 暴露为标准 ALSA ASoC 设备, 用 simple-card machine driver 连接 MTK 内部 I2S controller 和 MT6323 codec。

| 维度 | 评估 |
|---|---|
| 工作量 | **中等偏高** |
| 可行性 | **中等** |
| 前提条件 | 1) 需要确认 MT6323 在 MT6572 平台上的 I2S 引脚映射和时钟配置；2) mainline 已有 `mt6323` MFD 驱动 (在 `drivers/mfd/`), 但**没有** audio codec 子驱动；3) mainline 已有 `sound/soc/codecs/mt6351.c`/`mt6358.c`/`mt6359.c`/`mt6357.c` 作为 MTK PMIC codec 的参考, MT6323 需新建 |
| 难点 | MT6323 是老旧 PMIC, mainline 仅有 regulator/adc 驱动, **没有 audio codec 驱动**。需参考 mt6358/mt6359 实现 mt6323 的 ASoC codec 驱动, 并编写 I2S 控制器驱动 (MT6572 的 AFE/I2S IP 与 MT8173/MT8183 的 AFE 差异大) |

### 路径 C: 纯 Userspace 方案 — ALSA + dummy device + 绕过

**方案**: 用 ALSA `snd-aloop` (虚拟 Loopback) 提供输出设备, 用 USB 音频 Dongle (如 C-Media CMedia6534) 作为物理输出, 绕过板载音频完全不用。

| 维度 | 评估 |
|---|---|
| 工作量 | **极低** |
| 可行性 | **高** |
| 限制 | Z1 是 "听力机", 没有 USB OTG 或 USB host (需硬件确认)。如果板上有 USB 主机口可插 USB 声卡, 这是最快的"能出声"方案 |
| 适用场景 | 快速验证 mainline boot + ALSA 栈, 但 **不适合作为 Z1 听力机的最终方案** |

### 路径 D: 最现实的路径 — 写 minimal ASoC driver + 借用 fork 驱动

**方案**: 
1. 在 fork 的 MT6572 时钟驱动 (apmixedsys) 基础上, 写一个 minimal ASoC platform driver 驱动 MT6572 内部 I2S/TDM controller (参考 `sound/soc/mediatek/common/` 的 `mtk-afe-platform-driver.c`, 但简化到只有基本 I2S)
2. 为 MT6323 写一个 minimal ASoC codec driver (参考 `sound/soc/codecs/mt6358.c`, MT6323 功能比 MT6358 少, 只需要 DAC/ADC/Earphone Amp/Headset detect)
3. 用 simple-card 连接两者
4. userspace 用 standard ALSA + pulseaudio/pipewire, 完全放弃 MTK HAL

| 维度 | 评估 |
|---|---|
| 工作量 | **高, 但可分步完成** |
| 可行性 | **中等** |
| 优势 | 1) 使用标准 Linux ASoC 框架; 2) 可复用 fork 已有的 MT6572 clk/pinctrl/pwrap 驱动; 3) MT6323 是标准 I2C 可寻址 PMIC, 寄存器公开 |
| 难点 | 1) MT6572 的 AFE I2S controller 寄存器需要 datasheet; 2) MT6323 音频子模块寄存器需要 datasheet; 3) 时钟树配置 (需要 apmixedsys 中的 audio clk 配置) |

---

## 适配方案对比

| 路径 | 成本 | 可行性 | 时间估算 | 最终能出声 |
|---|---|---|---|---|
| A: 移植 MTK 专有驱动+HAL | 极高 | 低 | 数月+ | 可能 |
| B: minimal ASoC + MT6323 codec | 中-高 | 中 | 数周 | 基本 |
| C: USB 声卡绕过 | 极低 | 高(如有USB) | 小时 | 是(需硬件) |
| **D: minimal ASoC + simple-card** | **高** | **中等** | **数周** | **基本** |

---

## 下一步动作建议 (最高性价比)

### Step 1: 确认 MT6323 音频寄存器 (1-2天)

获取 MT6323 datasheet, 确认:
- 音频相关寄存器地址 (DAC/ADC/Earphone Amp/Headset Detect)
- I2C/PWRAP 通信方式
- 是否有 I2S/TDM 输出

如果 MT6323 没有独立 I2S 接口, 则音频输出需通过 SoC 内部 PCM 总线 → MT6323 PWRAP 接口, 这需要更复杂的驱动。

### Step 2: 确认 MT6572 音频 AFE I2S 寄存器 (2-3天)

获取 MT6572 datasheet, 确认:
- I2S/PCM controller 寄存器映射
- 时钟源 (apmixedsys 中已有 `clk-mt6572-apmixedsys.c`, 但 `audio` 时钟标为 "DEAD")
- 引脚复用 (pinctrl-mt6572.c)

### Step 3: 实现最小 ASoC codec driver (1周)

参考 `sound/soc/codecs/mt6358.c` 实现 `mt6323` 的 minimal ASoC codec driver, 仅支持:
- 16-bit PCM playback
- 单声道录音
- Earphone/Headset detection

### Step 4: 实现 minimal platform driver (1-2周)

写一个简化的 MT6572 AFE platform driver, 参考 `sound/soc/mediatek/common/` 中的 `mtk-afe-platform-driver.c`, 但砍掉所有 AFE 高级功能 (DSP/多流), 仅保留:
- 基本 PCM 流
- 基本 I2S/TDM 接口
- 基本时钟控制

### Step 5: 编写 machine driver (DTS) (2-3天)

在 `mt6572-j72-z1.dts` 中添加 sound card 节点:
```dts
sound {
    compatible = "simple-audio-card";
    simple-audio-card,name = "MT6572-MT6323";
    simple-audio-card,format = "i2s";
    simple-audio-card,bitclock-master = <&simple_card_cpu>;
    simple-audio-card,frame-master = <&simple_card_cpu>;
    simple-audio-card,bitclock-inversion;
    // ...
};
```

### Step 6: Userspace ALSA 测试

```bash
aplay -D plughw:0,0 /usr/share/sounds/alsa/Front_Center.wav
```

---

## 补充: mainline 已有参考

| 参考源 | 路径 |
|---|---|
| fork mt6572 clock (apmixedsys) | `/home/<user>/claude/6572/mt6572-mainline-linux/clk/clk-mt6572-apmixedsys.c` |
| fork pinctrl | `/home/<user>/claude/6572/mt6572-mainline-linux/pinctrl/pinctrl-mt6572.c` |
| mainline mt6358 codec (参考) | `fork_linux/sound/soc/codecs/mt6358.c` |
| mainline mt6359 codec (参考) | `fork_linux/sound/soc/codecs/mt6359.c` |
| mainline MTK common AFE | `fork_linux/sound/soc/mediatek/common/` |
| mainline MTK machine drivers | `fork_linux/sound/soc/mediatek/mt8173/` 等 |
| mainline mt6572 dtsi | `mainline_recon/fork_files/arch/arm/boot/dts/mediatek/mt6572.dtsi` |
| mainline recon report | `mainline_recon/REPORT.md` — 确认 audio clock 状态: **DEAD** |
