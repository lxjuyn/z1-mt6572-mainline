# MT6572 Z1 Kernel Slim: CONFIG Disable Analysis

## Summary

**Current zImage: 8.5 MB** (vmlinux: 26 MB, fully built-in)
**Target: < 4.96 MB** for 6 MB bootimg partition

This report analyzes removable CONFIG options across 8 categories.
All changes assume Z1 hardware: MT6572 SoC + MT6323 PMIC.
Phase 1 needs: start kernel + ttyMT0 console + keypad + goodix + musb gadget.

---

## 1. COMMON_CLK (Non-MT6572)

The .config has 12 `CONFIG_COMMON_CLK_MT*` entries, only `CONFIG_COMMON_CLK_MT6572` is Z1-relevant.
All other 11 clk drivers are dead weight.

| CONFIG | Current | Propose | Est. Savings | Safety | Reason |
|--------|---------|---------|-------------|--------|--------|
| CONFIG_COMMON_CLK_MT6795 | y | n | ~40 KB | HIGH | MT6795 SoC clk. Z1=MT6572. No runtime use. |
| CONFIG_COMMON_CLK_MT6795_MFGCFG | y | n | ~4 KB | HIGH | MT6795 mfg clk sub-driver. |
| CONFIG_COMMON_CLK_MT6795_MMSYS | y | n | ~7 KB | HIGH | MT6795 media sys clk. |
| CONFIG_COMMON_CLK_MT6795_VDECSYS | y | n | ~2 KB | HIGH | MT6795 decode sys clk. |
| CONFIG_COMMON_CLK_MT6795_VENCSYS | y | n | ~4 KB | HIGH | MT6795 encode sys clk. |
| CONFIG_COMMON_CLK_MT2701 | y | n | ~50 KB | HIGH | MT2701 SoC clk. |
| CONFIG_COMMON_CLK_MT7622 | y | n | ~35 KB | HIGH | MT7622 SoC clk (Router). |
| CONFIG_COMMON_CLK_MT7629 | y | n | ~27 KB | HIGH | MT7629 SoC clk (Router). |
| CONFIG_COMMON_CLK_MT7981 | y | n | ~30 KB | HIGH | MT7981 SoC clk (Fiber). |
| CONFIG_COMMON_CLK_MT7986 | y | n | ~21 KB | HIGH | MT7986 SoC clk. |
| CONFIG_COMMON_CLK_MT7988 | y | n | ~18 KB | HIGH | MT7988 SoC clk. |
| CONFIG_COMMON_CLK_MT8135 | y | n | ~21 KB | HIGH | MT8135 SoC clk (Tablet). |
| CONFIG_COMMON_CLK_MT8516 | y | n | ~20 KB | HIGH | MT8516 SoC clk. |
| **TOTAL clk savings** | | | **~299 KB** | | |

**Dependencies check:** No Z1-relevant driver (clk-mt6572, pinctrl-mt6577, serial-8250-mt6577) selects or depends on any of these. Safe to remove.

---

## 2. THERMAL (Full subsystem)

All 11 THERMAL entries are =y. Z1 has no thermal management; user previously disabled wmt_tm with `if (0)`.

| CONFIG | Current | Propose | Est. Savings | Safety | Reason |
|--------|---------|---------|-------------|--------|--------|
| CONFIG_THERMAL | y | n | ~200 KB | HIGH | Core thermal framework. Z1 has no thermal driver. |
| CONFIG_THERMAL_HWMON | y | n | small | HIGH | hwmon interface to thermal. |
| CONFIG_THERMAL_OF | y | n | small | HIGH | OF binding for thermal. |
| CONFIG_THERMAL_GOV_STEP_WISE | y | n | small | HIGH | Step-wise governor (only thermal governor =y). |
| CONFIG_CPU_THERMAL | y | n | ~2 KB | HIGH | CPU cooling, depends on THERMAL_OF. |
| CONFIG_CPU_FREQ_THERMAL | y | n | ~3 KB | HIGH | CPUFreq cooling, depends on CPUFREQ. |
| CONFIG_DEVFREQ_THERMAL | y | n | ~2 KB | HIGH | Devfreq cooling. |
| CONFIG_TI_SOC_THERMAL | y | n | ~50 KB | HIGH | TI PMIC thermal (Z1=MTK). |
| CONFIG_DRM_ETNAVIV_THERMAL | y | n | ~2 KB | HIGH | Etnaviv GPU thermal (not on Z1). |
| CONFIG_GENERIC_ADC_THERMAL | m | n | ~5 KB | HIGH | ADC thermal (Z1 has none). |
| **TOTAL thermal savings** | | | **~270 KB** | | |

**Dependencies check:**
- `CONFIG_CPU_FREQ_THERMAL` depends on `CPUFREQ` - if we also disable CPUFREQ, this is safe.
- No Z1-relevant driver depends on THERMAL. Safe to remove.

---

## 3. CPUFREQ

Only `CONFIG_CPUFREQ_DT` and `CONFIG_CPUFREQ_DT_PLATDEV` are =y.

| CONFIG | Current | Propose | Est. Savings | Safety | Reason |
|--------|---------|---------|-------------|--------|--------|
| CONFIG_CPUFREQ_DT | y | n | ~35 KB | HIGH | DT cpufreq driver (cpu-scaling). Z1 Phase1: no scaling needed. |
| CONFIG_CPUFREQ_DT_PLATDEV | y | n | ~8 KB | HIGH | DT platdev whitelist. |
| **TOTAL cpufreq savings** | | | **~43 KB** | | |

**Dependencies check:**
- `CONFIG_CPU_FREQ_THERMAL` (thermal entry above) depends on `CPU_FREQ`. Removing both together is clean.
- clk-mt6572 does NOT depend on CPUFREQ. Safe.

---

## 4. CRYPTO (Full subsystem)

77 CONFIG_CRYPTO entries. 35 =y + 39 =m.

### 4a. CRYPTO=y entries (17 that can be removed):

| CONFIG | Current | Propose | Est. Savings | Safety | Reason |
|--------|---------|---------|-------------|--------|--------|
| CONFIG_CRYPTO_ALGAPI | y | n | ~15 KB | HIGH | Core algo API. |
| CONFIG_CRYPTO_ALGAPI2 | y | n | ~8 KB | HIGH | Algo API v2. |
| CONFIG_CRYPTO_AEAD2 | y | n | ~5 KB | HIGH | AEAD API v2. |
| CONFIG_CRYPTO_SKCIPHER | y | n | ~12 KB | HIGH | Skcipher API. |
| CONFIG_CRYPTO_SKCIPHER2 | y | n | ~6 KB | HIGH | Skcipher v2. |
| CONFIG_CRYPTO_HASH | y | n | ~10 KB | HIGH | Hash API. |
| CONFIG_CRYPTO_HASH2 | y | n | ~5 KB | HIGH | Hash v2. |
| CONFIG_CRYPTO_RNG2 | y | n | ~4 KB | HIGH | RNG v2 API. |
| CONFIG_CRYPTO_AKCIPHER | y | n | ~6 KB | HIGH | Asymmetric cipher API. |
| CONFIG_CRYPTO_AKCIPHER2 | y | n | ~5 KB | HIGH | Akcipher v2. |
| CONFIG_CRYPTO_KPP2 | y | n | ~3 KB | HIGH | KPP v2. |
| CONFIG_CRYPTO_ACOMP2 | y | n | ~3 KB | HIGH | Async compression v2. |
| CONFIG_CRYPTO_MANAGER | y | n | ~8 KB | HIGH | Crypto manager. |
| CONFIG_CRYPTO_MANAGER2 | y | n | ~5 KB | HIGH | Crypto manager v2. |
| CONFIG_CRYPTO_RSA | y | n | ~15 KB | HIGH | RSA algo (no dm-crypt, no fscrypt). |
| CONFIG_CRYPTO_ECB | y | n | ~3 KB | HIGH | ECB mode (no disk encryption). |
| CONFIG_CRYPTO_DEFLATE | y | n | ~3 KB | HIGH | Deflate compression (modinfo). |
| CONFIG_CRYPTO_LZO | y | n | ~3 KB | HIGH | LZO compression (modinfo). |
| CONFIG_CRYPTO_ZSTD | y | n | ~5 KB | HIGH | Zstd compression (modinfo). |
| CONFIG_CRYPTO_DRBG_HMAC | y | n | ~4 KB | HIGH | DRBG PRNG. |
| CONFIG_CRYPTO_HW | y | n | ~2 KB | HIGH | Crypto hardware abstraction. |
| CONFIG_CRYPTO_LIB_UTILS | y | n | ~2 KB | HIGH | Crypto lib utils. |
| CONFIG_CRYPTO_LIB_AES_ARCH | y | n | ~2 KB | HIGH | AES arch lib. |
| CONFIG_CRYPTO_LIB_BLAKE2S_ARCH | y | n | ~2 KB | HIGH | Blake2s arch lib. |
| CONFIG_CRYPTO_LIB_SHA1 | y | n | ~2 KB | HIGH | SHA1 lib. |
| CONFIG_CRYPTO_LIB_SHA1_ARCH | y | n | ~2 KB | HIGH | SHA1 arch lib. |
| CONFIG_CRYPTO_LIB_SHA256 | y | n | ~3 KB | HIGH | SHA256 lib. |
| CONFIG_CRYPTO_LIB_SHA256_ARCH | y | n | ~2 KB | HIGH | SHA256 arch lib. |
| CONFIG_CRYPTO_LIB_SHA512_ARCH | y | n | ~2 KB | HIGH | SHA512 arch lib. |
| CONFIG_CRYPTO_HASH_INFO | y | n | ~1 KB | HIGH | Hash info. |
| **TOTAL crypto=y savings** | | | **~160 KB** | | |

### 4b. CRYPTO=m entries (39 modules - will be removed by `make modules_install` exclusion):

These are already modules so they won't be in zImage. However, if `modules.builtin` or module loading is needed, they can be set to `# not set`. For zImage slimming only, =m entries are already free.

### 4c. Special: CONFIG_CRYPTO_SIG, CONFIG_CRYPTO_SIG2, CONFIG_CRYPTO_USER_API_ENABLE_OBSOLETE

| CONFIG | Current | Propose | Est. Savings | Safety | Reason |
|--------|---------|---------|-------------|--------|--------|
| CONFIG_CRYPTO_SIG | y | n | ~5 KB | HIGH | Signature API (not needed). |
| CONFIG_CRYPTO_SIG2 | y | n | ~3 KB | HIGH | Sig v2. |
| CONFIG_CRYPTO_USER_API_ENABLE_OBSOLETE | y | n | ~1 KB | HIGH | User API obsolete. |
| **TOTAL special crypto savings** | | | **~9 KB** | | |

**Total crypto savings: ~229 KB**

**Dependencies check:** No Z1 driver depends on CRYPTO. Safe to remove.

---

## 5. REGULATOR (Non-MT6323)

31 `REGULATOR=y` entries. Only 3 are Z1-relevant: `MT6323`, `FIXED_VOLTAGE`, `GPIO`.

| CONFIG | Current | Propose | Est. Savings | Safety | Reason |
|--------|---------|---------|-------------|--------|--------|
| CONFIG_REGULATOR_ACT8945A | y | n | ~3 KB | HIGH | APM Act8945a PMIC. |
| CONFIG_REGULATOR_ARM_SCMI | y | n | ~3 KB | HIGH | ARM SCMI PMIC. |
| CONFIG_REGULATOR_AS3711 | y | n | ~3 KB | HIGH | AMS AS3711 PMIC. |
| CONFIG_REGULATOR_AS3722 | y | n | ~3 KB | HIGH | AMS AS3722 PMIC. |
| CONFIG_REGULATOR_AXP20X | y | n | ~5 KB | HIGH | X-Powers AXP209/221. |
| CONFIG_REGULATOR_BCM590XX | y | n | ~3 KB | HIGH | Broadcom BCM PMIC. |
| CONFIG_REGULATOR_CPCAP | y | n | ~3 KB | HIGH | Nokia CPCAP PMIC. |
| CONFIG_REGULATOR_DA9210 | y | n | ~3 KB | HIGH | Dialog DA9210. |
| CONFIG_REGULATOR_FAN53555 | y | n | ~3 KB | HIGH | Faradex FAN53555. |
| CONFIG_REGULATOR_LP872X | y | n | ~3 KB | HIGH | TI LP872x. |
| CONFIG_REGULATOR_MAX8907 | y | n | ~3 KB | HIGH | Maxim MAX8907. |
| CONFIG_REGULATOR_MAX8973 | y | n | ~3 KB | HIGH | Maxim MAX8973. |
| CONFIG_REGULATOR_MAX77686 | y | n | ~3 KB | HIGH | Maxim MAX77686. |
| CONFIG_REGULATOR_MAX77802 | y | n | ~3 KB | HIGH | Maxim MAX77802. |
| CONFIG_REGULATOR_PALMAS | y | n | ~5 KB | HIGH | TI Palmas PMIC. |
| CONFIG_REGULATOR_QCOM_SPMI | y | n | ~8 KB | HIGH | QCOM SPMI PMIC. |
| CONFIG_REGULATOR_RK808 | y | n | ~3 KB | HIGH | Rockchip RK808. |
| CONFIG_REGULATOR_RN5T618 | y | n | ~3 KB | HIGH | Renesas RN5T618. |
| CONFIG_REGULATOR_S2MPS11 | y | n | ~3 KB | HIGH | Samsung S2MPS11. |
| CONFIG_REGULATOR_S5M8767 | y | n | ~3 KB | HIGH | Samsung S5M8767. |
| CONFIG_REGULATOR_STPMIC1 | y | n | ~3 KB | HIGH | ST STPMIC1. |
| CONFIG_REGULATOR_TI_ABB | y | n | ~3 KB | HIGH | TI ABB regulator. |
| CONFIG_REGULATOR_TPS51632 | y | n | ~3 KB | HIGH | TI TPS51632. |
| CONFIG_REGULATOR_TPS62360 | y | n | ~3 KB | HIGH | TI TPS62360. |
| CONFIG_REGULATOR_TPS65090 | y | n | ~5 KB | HIGH | TI TPS65090. |
| CONFIG_REGULATOR_TPS65217 | y | n | ~3 KB | HIGH | TI TPS65217. |
| CONFIG_REGULATOR_TPS65218 | y | n | ~3 KB | HIGH | TI TPS65218. |
| CONFIG_REGULATOR_TPS65219 | y | n | ~3 KB | HIGH | TI TPS65219. |
| CONFIG_REGULATOR_TPS6586X | y | n | ~5 KB | HIGH | TI TPS6586x. |
| CONFIG_REGULATOR_TPS65910 | y | n | ~5 KB | HIGH | TI TPS65910. |
| **TOTAL regulator non-Z1 savings** | | | **~130 KB** | | |

**Safe regulators to KEEP:**
- `CONFIG_REGULATOR_FIXED_VOLTAGE=y` - Z1 needs fixed rail regulators
- `CONFIG_REGULATOR_GPIO=y` - Z1 may need GPIO-controlled rails
- `CONFIG_REGULATOR_MT6323=y` - Z1 PMIC (MANDATORY)
- `CONFIG_REGULATOR_PWM=y` - may be needed for display/backlight
- `CONFIG_REGULATOR_PBIAS=y` - PBIASLDO (keep for safety)

**Dependencies check:**
- `CONFIG_MFD_*` drivers for these PMICs are also enabled (see MFD section below). Removing both MFD and regulator together is clean.
- clk-mt6572, pinctrl-mt6577 do NOT depend on regulator. Safe.

---

## 6. MFD (Non-MT6323)

40 `MFD` entries. None of them are mt6323! The .config has `# CONFIG_MFD_MT6323 is not set` and mt6323 uses direct driver registration (not MFD framework in this tree).

| CONFIG | Current | Propose | Est. Savings | Safety | Reason |
|--------|---------|---------|-------------|--------|--------|
| CONFIG_MFD_ACT8945A | y | n | ~2 KB | HIGH | APM Act8945a. |
| CONFIG_MFD_AS3711 | y | n | ~2 KB | HIGH | AMS AS3711. |
| CONFIG_MFD_AS3722 | y | n | ~2 KB | HIGH | AMS AS3722. |
| CONFIG_MFD_ATMEL_FLEXCOM | y | n | ~3 KB | HIGH | Atmel Flexcom. |
| CONFIG_MFD_BCM590XX | y | n | ~2 KB | HIGH | Broadcom PMIC. |
| CONFIG_MFD_AXP20X | y | n | ~5 KB | HIGH | X-Powers AXP20x. |
| CONFIG_MFD_AXP20X_I2C | y | n | ~3 KB | HIGH | AXP20x I2C interface. |
| CONFIG_MFD_DA9052_I2C | y | n | ~2 KB | HIGH | Dialog DA9052. |
| CONFIG_MFD_MAX14577 | y | n | ~2 KB | HIGH | Maxim MAX14577. |
| CONFIG_MFD_MAX77686 | y | n | ~2 KB | HIGH | Maxim MAX77686. |
| CONFIG_MFD_MAX8907 | y | n | ~2 KB | HIGH | Maxim MAX8907. |
| CONFIG_MFD_MAX8997 | y | n | ~2 KB | HIGH | Maxim MAX8997. |
| CONFIG_MFD_MAX8998 | y | n | ~2 KB | HIGH | Maxim MAX8998. |
| CONFIG_MFD_MT6397 | y | n | ~15 KB | HIGH | MTK MT6397 PMIC (not Z1's MT6323). |
| CONFIG_MFD_CPCAP | y | n | ~3 KB | HIGH | Nokia CPCAP. |
| CONFIG_MFD_PM8XXX | y | n | ~3 KB | HIGH | QCOM PM8xxx. |
| CONFIG_MFD_RK8XX | y | n | ~5 KB | HIGH | Rockchip RK8xx. |
| CONFIG_MFD_RK8XX_I2C | y | n | ~3 KB | HIGH | RK8xx I2C. |
| CONFIG_MFD_RN5T618 | y | n | ~2 KB | HIGH | Renesas RN5T618. |
| CONFIG_MFD_SEC_CORE | y | n | ~5 KB | HIGH | Samsung SEC PMIC. |
| CONFIG_MFD_SEC_I2C | y | n | ~2 KB | HIGH | SEC I2C bus. |
| CONFIG_MFD_OMAP_USB_HOST | y | n | ~2 KB | HIGH | OMAP USB host. |
| CONFIG_MFD_PALMAS | y | n | ~3 KB | HIGH | TI Palmas. |
| CONFIG_MFD_TPS65090 | y | n | ~3 KB | HIGH | TI TPS65090. |
| CONFIG_MFD_TPS65217 | y | n | ~2 KB | HIGH | TI TPS65217. |
| CONFIG_MFD_TPS65218 | y | n | ~2 KB | HIGH | TI TPS65218. |
| CONFIG_MFD_TPS65219 | y | n | ~2 KB | HIGH | TI TPS65219. |
| CONFIG_MFD_TPS6586X | y | n | ~5 KB | HIGH | TI TPS6586x. |
| CONFIG_MFD_TPS65910 | y | n | ~5 KB | HIGH | TI TPS65910. |
| CONFIG_MFD_STPMIC1 | y | n | ~2 KB | HIGH | ST STPMIC1. |
| CONFIG_MFD_STMFX | y | n | ~20 KB | HIGH | ST STMFX touchscreen. |
| **TOTAL MFD non-Z1 savings** | | | **~140 KB** | | |

**MFD entries to KEEP:**
- `CONFIG_MFD_CORE=y` - core MFD framework (may be needed by other drivers)
- `CONFIG_MFD_SYSCON=y` - system controller (keep for safety)
- `CONFIG_MFD_STMPE=y` - may be needed for touchscreen on Z1

**Note:** MT6323 is NOT listed as an MFD in this .config. It uses direct regulator registration. The only mt63xx MFD is MT6397 (line 3057).

**Dependencies check:**
- `CONFIG_MFD_MT6397` has dependents: `drivers/pinctrl/mediatek` (mt6397 pinctrl), `drivers/rtc`, `drivers/leds`, `drivers/regulator` (mt6397). All are non-Z1. Safe to remove.
- No Z1 driver selects MFD_*.

---

## 7. LEDS TRIGGER (All triggers removable)

9 LED trigger entries are =y. Only keep LEDS_CLASS and GPIO-based LED drivers.

| CONFIG | Current | Propose | Est. Savings | Safety | Reason |
|--------|---------|---------|-------------|--------|--------|
| CONFIG_LEDS_TRIGGER_TIMER | y | n | ~2 KB | HIGH | Timer blink trigger. |
| CONFIG_LEDS_TRIGGER_ONESHOT | y | n | ~2 KB | HIGH | One-shot trigger. |
| CONFIG_LEDS_TRIGGER_HEARTBEAT | y | n | ~2 KB | HIGH | Heartbeat trigger. |
| CONFIG_LEDS_TRIGGER_BACKLIGHT | y | n | ~1 KB | HIGH | Backlight trigger. |
| CONFIG_LEDS_TRIGGER_CPU | y | n | ~2 KB | HIGH | CPU activity trigger. |
| CONFIG_LEDS_TRIGGER_GPIO | y | n | ~1 KB | HIGH | GPIO event trigger. |
| CONFIG_LEDS_TRIGGER_DEFAULT_ON | y | n | ~1 KB | HIGH | Default-on trigger. |
| CONFIG_LEDS_TRIGGER_TRANSIENT | y | n | ~1 KB | HIGH | Transient trigger. |
| CONFIG_LEDS_TRIGGER_CAMERA | y | n | ~1 KB | HIGH | Camera flash trigger. |
| **TOTAL trigger savings** | | | **~13 KB** | | |

**LED drivers to KEEP (HIGH safety):**
- `CONFIG_LEDS_CLASS=y` - LED class core (needed for any LED driver)
- `CONFIG_LEDS_GPIO=y` - GPIO LEDs (Z1 status indicator)
- `CONFIG_LEDS_PCA9532_GPIO=y` - PCA9532 GPIO interface
- `CONFIG_LEDS_PWM=y` - PWM LED (may be needed for backlight)

**LED drivers to potentially remove (MED safety):**
- `CONFIG_LEDS_CPCAP=m` - already module, skip
- `CONFIG_LEDS_PCA9532=m` - already module, skip  
- `CONFIG_LEDS_MAX8997=m` - already module, skip
- `CONFIG_LEDS_MAX77693=m` - already module, skip

**Dependencies check:**
- LED triggers depend on `LEDS_TRIGGERS` which depends on `LEDS_CLASS`.
- Removing triggers does NOT affect LED class or LED GPIO drivers. Safe.

---

## 8. Miscellaneous

| CONFIG | Current | Propose | Est. Savings | Safety | Reason |
|--------|---------|---------|-------------|--------|--------|
| CONFIG_MISC_FILESYSTEMS | y | n | ~1 KB | HIGH | Misc filesystems (not needed for Phase1). |
| **TOTAL misc savings** | | | **~1 KB** | | |

---

## Grand Total Savings Estimate

| Category | Savings |
|----------|---------|
| COMMON_CLK (non-MT6572) | ~299 KB |
| THERMAL (full subsystem) | ~270 KB |
| CPUFREQ (DT) | ~43 KB |
| CRYPTO (all =y entries) | ~229 KB |
| REGULATOR (non-Z1) | ~130 KB |
| MFD (non-core, non-Z1) | ~140 KB |
| LEDS_TRIGGER (all 9 triggers) | ~13 KB |
| MISC_FILESYSTEMS | ~1 KB |
| **GRAND TOTAL** | **~1,125 KB** |

### Reality Check

**zImage is 8.5 MB.** We need to reach **< 4.96 MB** = remove ~3,500 KB.

The ~1,125 KB from this report gets us partway. Remaining savings strategies:

1. **Convert more drivers to modules** (.modinfo): This is the biggest lever. Every driver compiled as `=m` instead of `=y` removes it from zImage entirely. The kernel build already has 39 crypto=m, 8 regulator=m, 11 MFD=m, 9 LED=m. Converting more (mmc, serial, keypad, goodix, musb, pinctrl, clk) to modules would save ~2-3 MB.

2. **Disable unused subsystems**: DRM, Bluetooth, WiFi, NFC, USB gadget config, network stack, filesystems (ext4, fat, etc.), sound, networking (if not needed for console-only).

3. **Use `make LOCALVERSION=-slim` + fewer symbols**: Strip debug info, reduce kallsyms.

4. **Use CONFIG_KALLSYMS=n** (if debugging not needed for Phase1): saves ~100-200 KB.

5. **Use lz4/zstd compression instead of gzip**: Compress zImage smaller.

---

## Ready-to-paste scripts/config --disable List (HIGH safety only)

```bash
# === COMMON_CLK (non-MT6572) ===
scripts/config --disable CONFIG_COMMON_CLK_MT2701
scripts/config --disable CONFIG_COMMON_CLK_MT6795
scripts/config --disable CONFIG_COMMON_CLK_MT6795_MFGCFG
scripts/config --disable CONFIG_COMMON_CLK_MT6795_MMSYS
scripts/config --disable CONFIG_COMMON_CLK_MT6795_VDECSYS
scripts/config --disable CONFIG_COMMON_CLK_MT6795_VENCSYS
scripts/config --disable CONFIG_COMMON_CLK_MT7622
scripts/config --disable CONFIG_COMMON_CLK_MT7629
scripts/config --disable CONFIG_COMMON_CLK_MT7981
scripts/config --disable CONFIG_COMMON_CLK_MT7981_ETHSYS
scripts/config --disable CONFIG_COMMON_CLK_MT7986
scripts/config --disable CONFIG_COMMON_CLK_MT7986_ETHSYS
scripts/config --disable CONFIG_COMMON_CLK_MT7988
scripts/config --disable CONFIG_COMMON_CLK_MT8135
scripts/config --disable CONFIG_COMMON_CLK_MT8516

# === THERMAL ===
scripts/config --disable CONFIG_THERMAL
scripts/config --disable CONFIG_THERMAL_HWMON
scripts/config --disable CONFIG_THERMAL_OF
scripts/config --disable CONFIG_THERMAL_GOV_STEP_WISE
scripts/config --disable CONFIG_CPU_THERMAL
scripts/config --disable CONFIG_CPU_FREQ_THERMAL
scripts/config --disable CONFIG_DEVFREQ_THERMAL
scripts/config --disable CONFIG_TI_SOC_THERMAL
scripts/config --disable CONFIG_DRM_ETNAVIV_THERMAL

# === CPUFREQ ===
scripts/config --disable CONFIG_CPUFREQ_DT
scripts/config --disable CONFIG_CPUFREQ_DT_PLATDEV

# === CRYPTO ===
scripts/config --disable CONFIG_CRYPTO_ALGAPI
scripts/config --disable CONFIG_CRYPTO_ALGAPI2
scripts/config --disable CONFIG_CRYPTO_AEAD2
scripts/config --disable CONFIG_CRYPTO_SKCIPHER
scripts/config --disable CONFIG_CRYPTO_SKCIPHER2
scripts/config --disable CONFIG_CRYPTO_HASH
scripts/config --disable CONFIG_CRYPTO_HASH2
scripts/config --disable CONFIG_CRYPTO_RNG2
scripts/config --disable CONFIG_CRYPTO_AKCIPHER
scripts/config --disable CONFIG_CRYPTO_AKCIPHER2
scripts/config --disable CONFIG_CRYPTO_KPP2
scripts/config --disable CONFIG_CRYPTO_ACOMP2
scripts/config --disable CONFIG_CRYPTO_MANAGER
scripts/config --disable CONFIG_CRYPTO_MANAGER2
scripts/config --disable CONFIG_CRYPTO_RSA
scripts/config --disable CONFIG_CRYPTO_ECB
scripts/config --disable CONFIG_CRYPTO_DEFLATE
scripts/config --disable CONFIG_CRYPTO_LZO
scripts/config --disable CONFIG_CRYPTO_ZSTD
scripts/config --disable CONFIG_CRYPTO_DRBG_HMAC
scripts/config --disable CONFIG_CRYPTO_HW
scripts/config --disable CONFIG_CRYPTO_LIB_UTILS
scripts/config --disable CONFIG_CRYPTO_LIB_AES_ARCH
scripts/config --disable CONFIG_CRYPTO_LIB_BLAKE2S_ARCH
scripts/config --disable CONFIG_CRYPTO_LIB_SHA1
scripts/config --disable CONFIG_CRYPTO_LIB_SHA1_ARCH
scripts/config --disable CONFIG_CRYPTO_LIB_SHA256
scripts/config --disable CONFIG_CRYPTO_LIB_SHA256_ARCH
scripts/config --disable CONFIG_CRYPTO_LIB_SHA512_ARCH
scripts/config --disable CONFIG_CRYPTO_HASH_INFO
scripts/config --disable CONFIG_CRYPTO_SIG
scripts/config --disable CONFIG_CRYPTO_SIG2
scripts/config --disable CONFIG_CRYPTO_USER_API_ENABLE_OBSOLETE

# === REGULATOR (non-Z1) ===
scripts/config --disable CONFIG_REGULATOR_ACT8945A
scripts/config --disable CONFIG_REGULATOR_ARM_SCMI
scripts/config --disable CONFIG_REGULATOR_AS3711
scripts/config --disable CONFIG_REGULATOR_AS3722
scripts/config --disable CONFIG_REGULATOR_AXP20X
scripts/config --disable CONFIG_REGULATOR_BCM590XX
scripts/config --disable CONFIG_REGULATOR_CPCAP
scripts/config --disable CONFIG_REGULATOR_DA9210
scripts/config --disable CONFIG_REGULATOR_FAN53555
scripts/config --disable CONFIG_REGULATOR_LP872X
scripts/config --disable CONFIG_REGULATOR_MAX8907
scripts/config --disable CONFIG_REGULATOR_MAX8973
scripts/config --disable CONFIG_REGULATOR_MAX77686
scripts/config --disable CONFIG_REGULATOR_MAX77802
scripts/config --disable CONFIG_REGULATOR_PALMAS
scripts/config --disable CONFIG_REGULATOR_QCOM_SPMI
scripts/config --disable CONFIG_REGULATOR_RK808
scripts/config --disable CONFIG_REGULATOR_RN5T618
scripts/config --disable CONFIG_REGULATOR_S2MPS11
scripts/config --disable CONFIG_REGULATOR_S5M8767
scripts/config --disable CONFIG_REGULATOR_STPMIC1
scripts/config --disable CONFIG_REGULATOR_TI_ABB
scripts/config --disable CONFIG_REGULATOR_TPS51632
scripts/config --disable CONFIG_REGULATOR_TPS62360
scripts/config --disable CONFIG_REGULATOR_TPS65090
scripts/config --disable CONFIG_REGULATOR_TPS65217
scripts/config --disable CONFIG_REGULATOR_TPS65218
scripts/config --disable CONFIG_REGULATOR_TPS65219
scripts/config --disable CONFIG_REGULATOR_TPS6586X
scripts/config --disable CONFIG_REGULATOR_TPS65910

# === MFD (non-core, non-Z1) ===
scripts/config --disable CONFIG_MFD_ACT8945A
scripts/config --disable CONFIG_MFD_AS3711
scripts/config --disable CONFIG_MFD_AS3722
scripts/config --disable CONFIG_MFD_ATMEL_FLEXCOM
scripts/config --disable CONFIG_MFD_BCM590XX
scripts/config --disable CONFIG_MFD_AXP20X
scripts/config --disable CONFIG_MFD_AXP20X_I2C
scripts/config --disable CONFIG_MFD_DA9052_I2C
scripts/config --disable CONFIG_MFD_MAX14577
scripts/config --disable CONFIG_MFD_MAX77686
scripts/config --disable CONFIG_MFD_MAX8907
scripts/config --disable CONFIG_MFD_MAX8997
scripts/config --disable CONFIG_MFD_MAX8998
scripts/config --disable CONFIG_MFD_MT6397
scripts/config --disable CONFIG_MFD_CPCAP
scripts/config --disable CONFIG_MFD_PM8XXX
scripts/config --disable CONFIG_MFD_RK8XX
scripts/config --disable CONFIG_MFD_RK8XX_I2C
scripts/config --disable CONFIG_MFD_RN5T618
scripts/config --disable CONFIG_MFD_SEC_CORE
scripts/config --disable CONFIG_MFD_SEC_I2C
scripts/config --disable CONFIG_MFD_OMAP_USB_HOST
scripts/config --disable CONFIG_MFD_PALMAS
scripts/config --disable CONFIG_MFD_TPS65090
scripts/config --disable CONFIG_MFD_TPS65217
scripts/config --disable CONFIG_MFD_TPS65218
scripts/config --disable CONFIG_MFD_TPS65219
scripts/config --disable CONFIG_MFD_TPS6586X
scripts/config --disable CONFIG_MFD_TPS65910
scripts/config --disable CONFIG_MFD_STPMIC1
scripts/config --disable CONFIG_MFD_STMFX

# === LEDS TRIGGER ===
scripts/config --disable CONFIG_LEDS_TRIGGER_TIMER
scripts/config --disable CONFIG_LEDS_TRIGGER_ONESHOT
scripts/config --disable CONFIG_LEDS_TRIGGER_HEARTBEAT
scripts/config --disable CONFIG_LEDS_TRIGGER_BACKLIGHT
scripts/config --disable CONFIG_LEDS_TRIGGER_CPU
scripts/config --disable CONFIG_LEDS_TRIGGER_GPIO
scripts/config --disable CONFIG_LEDS_TRIGGER_DEFAULT_ON
scripts/config --disable CONFIG_LEDS_TRIGGER_TRANSIENT
scripts/config --disable CONFIG_LEDS_TRIGGER_CAMERA
```

---

## ABSOLUTELY NOT TOUCHED (preserve these)

```
CONFIG_COMMON_CLK_MT6572          # Z1 SoC clock driver
CONFIG_REGULATOR_FIXED_VOLTAGE    # Z1 fixed rails
CONFIG_REGULATOR_GPIO             # Z1 GPIO-regulated rails
CONFIG_REGULATOR_MT6323           # Z1 PMIC
CONFIG_REGULATOR_PWM              # Display backlight PWM
CONFIG_REGULATOR_PBIAS            # PBIASLDO (keep for safety)
CONFIG_MFD_CORE                   # MFD framework core
CONFIG_MFD_SYSCON                 # System controller
CONFIG_MFD_STMPE                  # Touchscreen (may be needed)
CONFIG_MFD_STPMIC1                # ST PMIC (already removed above)
# pinctrl-mt6577
# serial-8250-mt6577
# keypad
# goodix
# musb gadget
```

## Build Safety Notes

1. **Disabling THERMAL=y automatically disables CPU_FREQ_THERMAL** (depends on THERMAL) and DEVFREQ_THERMAL/CPU_THERMAL. The `scripts/config` calls handle this correctly if THERMAL is disabled first.

2. **Disabling CPUFREQ_DT makes CPU_FREQ_THERMAL fail** - but both are being disabled together. Order: disable THERMAL first, then CPUFREQ_DT.

3. **Disabling all crypto=y removes the crypto library** - no driver depends on this for Z1. All crypto=m entries remain as modules (not loaded by default).

4. **MFD MT6397 removal is safe** because the mt6397 pinctrl/rtc/leds/regulator drivers all depend on it - they won't compile without it.

5. **After changes, run `make olddefconfig`** to clean up any orphaned dependencies, then rebuild.

