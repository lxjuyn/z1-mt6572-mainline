# MT6323 audio resource / ABI gap audit

Status: **read-only evidence artifact**

This directory does not implement audio. It only records what can be proven from
archived vendor MT6572/MT6323 audio material and the earlier offline audio
register-contract audit.

## Proven resources / ABI

### 1) MT6572 AFE register window and offsets
- The earlier offline audit already proved the non-FPGA MT6572 AFE base,
  window, and SRAM layout: `mainline_recon/audio_register_contract_audit_20260718/README.md:16-22`.
- The same audit verified the concrete offsets for the selected AFE registers,
  including DAC, I2S, ADDA, IRQ, PCM, and ASRC: `mainline_recon/audio_register_contract_audit_20260718/validation.txt:42-60`.
- Vendor definitions for the same register bank are in
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/sound/AudDrv_Afe.h:413-559`.
- The runtime access pattern is direct MMIO read/modify/write via `Afe_Set_Reg`
  and `Afe_Get_Reg`: `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/sound/AudDrv_Afe.c:72-105`.

### 2) MT6323 PMIC digital / analog audio register bank
- The vendor MT6323 audio header defines the digital PMIC audio base and the
  analog speaker / top-control / AUDTOP registers:
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/sound/AudDrv_Ana.h:71-135`.
- The earlier audit verified the normalized MT6323 digital base and selected
  analog offsets: `mainline_recon/audio_register_contract_audit_20260718/validation.txt:61-68`.
- The runtime access pattern is PMIC wrapper read/modify/write via
  `Ana_Set_Reg` / `Ana_Get_Reg`: `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/sound/AudDrv_Ana.c:66-93`.

### 3) Audio clock control ABI
- Vendor audio clock control exists as explicit helper functions:
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/sound/AudDrv_Clk.c:92-143`.
- The same file exposes ANA / ADC / I2S clock helpers:
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/sound/AudDrv_Clk.c:232-360`.
- The user-visible ioctl ABI includes `AUD_SET_CLOCK`, `AUD_SET_26MCLOCK`,
  `AUD_SET_ADC_CLOCK`, `AUD_SET_I2S_CLOCK`, and `AUD_SET_ANA_CLOCK`:
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/sound/AudDrv_ioctl.h:83-89`.
- Those ioctls are dispatched in the vendor audio driver at
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/sound/AudDrv_Kernel.c:2312-2395`.

### 4) Speaker / earpiece / headset user ABI
- The vendor audio ioctl ABI includes `SET_SPEAKER_ON`, `SET_SPEAKER_OFF`,
  `SET_HEADSET_STATE`, `SET_HEADPHONE_ON`, `SET_HEADPHONE_OFF`,
  `SET_EARPIECE_ON`, and `SET_EARPIECE_OFF`:
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/sound/AudDrv_ioctl.h:128-136`.
- Those ioctls are dispatched in the driver and mapped to speaker / headset /
  earpiece helpers: `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/sound/AudDrv_Kernel.c:2481-2533`.
- The audio misc device is named `/dev/eac` in the vendor driver:
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/sound/AudDrv_Kernel.c:3188-3203`.
- The BT-CVSD companion misc device is named `/dev/ebc`:
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/btcvsd/AudDrv_BTCVSD.c:898-913`.

### 5) Headset detect / accdet user ABI
- The accdet UAPI names the device and ioctl numbers:
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/accdet/accdet.h:47-54`.
- The accdet driver binds those ioctls in `accdet_unlocked_ioctl`:
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/accdet/accdet.c:1505-1527`.
- The same driver registers the `h2w` switch device and the `/dev/accdet`
  character node through `alloc_chrdev_region`, `cdev_add`, `class_create`, and
  `device_create`: `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/accdet/accdet.c:1798-1830`.
- The accdet PMIC / EINT setup and power sequence are still vendor-specific and
  not mainline-generic: `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/accdet/accdet.c:509-524` and `:1443-1493`.

## No-go gaps: do not guess these

### A) IRQ topology remains unproven
- The earlier audit already states that it does not add an IRQ path or driver
  entry point: `mainline_recon/audio_register_contract_audit_20260718/README.md:34-36`.
- The vendor accdet path only proves MTK EINT + PMIC bits, not a mainline IRQ or
  Device Tree interrupt contract: `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/accdet/accdet.c:509-524` and `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/core/include/mach/reg_accdet.h:13-21,43-53`.
- NO-GO: assume any generic `request_irq` / `irqdomain` / DT interrupt wiring.

### B) Clock / reset framework wiring remains unproven
- The vendor audio code shows clock counters and ad-hoc `enable_clock` /
  `disable_clock` calls, plus PMIC wrapper toggles for analog and accdet
  power: `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/sound/AudDrv_Clk.c:92-143,232-360` and `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/accdet/accdet.c:1443-1493`.
- The earlier audit explicitly says no clock or reset path is added:
  `mainline_recon/audio_register_contract_audit_20260718/README.md:34-36`.
- NO-GO: infer a stable clk/reset binding or reset sequence for MT6323 audio.

### C) DTS binding / endpoint graph remains unproven
- The MT6323 DTS only instantiates a `mt6397codec` child and generic PMIC
  subnodes; it does not prove an audio graph endpoint or a codec-specific binding
  for the target board: `fork_linux/arch/arm/boot/dts/mediatek/mt6323.dtsi:11-17` and `:278-280`.
- The MT6323 MFD cell list registers auxadc, efuse, rtc, regulator, led, keys,
  pwrc, thermal; it does not add a codec cell under the MT6323 list itself:
  `fork_linux/drivers/mfd/mt6397-core.c:126-157`.
- The earlier audit explicitly says it does not add a Device Tree node:
  `mainline_recon/audio_register_contract_audit_20260718/README.md:34-36`.
- NO-GO: guess `ports`, `endpoints`, `dai-link`, or any TDM graph wiring.

### D) `/dev/eac` / `/dev/ebc` / `/dev/accdet` ioctl semantics remain vendor-private
- The vendor ABI proves the node names and command numbers, but not a safe
  compatibility contract for any new user-space implementation.
- `/dev/eac` is the audio misc device, `/dev/ebc` is the BT-CVSD misc device,
  and `/dev/accdet` is the headset-detect node:
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/sound/AudDrv_Kernel.c:3188-3203`,
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/btcvsd/AudDrv_BTCVSD.c:898-913`,
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/accdet/accdet.c:1798-1830`.
- NO-GO: infer a generic ALSA control ABI or assume those ioctl numbers are
  portable outside the archived vendor driver.

### E) Register bitfields and access sequences remain implementation detail
- The vendor files expose bit positions and raw register sequences, but not a
  higher-level spec for which orderings are required to be safe:
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/core/include/mach/reg_accdet.h:43-70`,
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/sound/AudDrv_Afe.h:650-777`,
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/sound/AudDrv_Clk.c:92-143`.
- The earlier audit also says the contract does not provide a PMIC write path
  for boot integration: `mainline_recon/audio_register_contract_audit_20260718/README.md:34-36`.
- NO-GO: guess undocumented bit meanings or reorder the vendor read/modify/
  write sequences.

### F) I2S / TDM timing remains unproven
- Vendor headers only define format, word length, master/slave, sync-length,
  and PCM slot-related bit positions:
  `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/sound/AudDrv_Afe.h:131-166` and `:667-717`.
- They do not establish the board-level BCLK/LRCLK polarity, slot timing,
  endpoint cadence, or any specific TDM frame contract.
- NO-GO: infer a concrete I2S/TDM timing model from these bitfield macros alone.

## Manifest policy

- `INPUT_SHA256SUMS` lists the exact evidence inputs used by this gap audit.
- `OUTPUT_SHA256SUMS` lists the files produced by this directory.
- `validate_mt6323_audio_resource_abi_gap_audit.py` must only read local files;
  it must not compile, flash, access device nodes, or touch hardware.

