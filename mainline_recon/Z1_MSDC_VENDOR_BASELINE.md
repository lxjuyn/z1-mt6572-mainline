# Z1 MSDC0 vendor baseline cross-check

## Scope

Read-only comparison of the MT6572 vendor host-0 configuration and the current Z1 direct-CID diagnostic. No eMMC command sequence or register programming was changed for this report.

## Vendor source evidence

The vendor board definitions are in:

- `wifi_recon/mt6572_kernel_drsami/mediatek/custom/cci72_we_jb3/kernel/core/src/board.c`
- `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/mmc-host/sd.c`
- `wifi_recon/mt6572_kernel_drsami/mediatek/platform/mt6572/kernel/drivers/mmc-host/mt_sd.h`

The board configuration contains conditional host-0 initializers. Their exact active branch for the Z1 cannot be inferred solely from this non-matching vendor tree, but all branches agree on the identification-stage delay baseline:

| Field | Vendor value in all host-0 branches |
|---|---:|
| `clkdly` | 0 |
| `dat0rddly` through `dat7rddly` | 0 |
| `datwrddly` | 0 |
| `cmdrrddly` | 0 |
| `cmdrddly` | 0 |

The eMMC-specific branch additionally uses 6 mA drive settings (`clk_drv = cmd_drv = dat_drv = 2`) and eight data pins. This agrees with the current Z1 DTS's 6 mA intent for the Stage-A CLK/CMD/DAT0 test.

## Vendor initialization baseline

Vendor `msdc_init_hw()` performs the following relevant identification-stage setup:

- releases eMMC reset handoff through `EMMC_IOCON.BOOTRST` in its power/reset flow;
- resets the local MSDC/FIFO/interrupt state;
- clears `MSDC_PAD_TUNE`, `MSDC_IOCON`, `MSDC_DAT_RDDLY0`, and `MSDC_DAT_RDDLY1`;
- sets `MSDC_IOCON.DDLSEL = 1`;
- assigns the vendor host-0 (eMMC) `PATCH_BIT1` baseline `0xFFFF0009` (`0xFFF700C9` is the separate host-1/2 SD/SDIO branch);
- enables SDIO mode while disabling SDIO interrupt detection.

## Current final diagnostic coverage

`out/boot_mainline_z1_mc0pads.img` reads, at both `post-pinctrl-pre-clock` and `before-CMD0`:

1. raw MC0 GPIO mode, direction, output, and input words;
2. raw IOCFG-B IES/SMT/R0R1/PUPD/drive and IOCFG-T RSTB drive words;
3. `MSDC_CFG`, `MSDC_IOCON`, `EMMC_IOCON`, `BOOTRST`, `SDC_CFG`, and `SDC_ADV_CFG0`;
4. `MSDC_PATCH_BIT`, `MSDC_PATCH_BIT1`, `MSDC_PATCH_BIT2`, `MSDC_PAD_TUNE`, `MSDC_DAT_RDDLY0`, `MSDC_DAT_RDDLY1`, and `EMMC50_CFG0`.

Thus the next board log can compare all nonzero pre-CMD1 controller state against the vendor baseline without changing any such register.

## Safety

The Z1 path remains direct CID only: CMD0, CMD1, and CMD2 only after CMD1 reports ready. It returns before `mmc_add_host()`, sends no CMD6, does not access EXT_CSD, and does not write eMMC media or persistent configuration.
