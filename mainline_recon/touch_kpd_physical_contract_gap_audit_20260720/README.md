# CST836U/I2C1 touch + Z1 KPD physical-contract gap audit — 2026-07-20

Status: **READ-ONLY EVIDENCE ARTIFACT / NO HARDWARE / NO FLASH / NO BUILD**

This directory freezes only the evidence that is already present in the repo.
It does not run hardware, compile code, flash images, edit DTS/source, or
inject events.

## NO-GO

The following operations are explicitly forbidden for this audit:

- `NO-GO:编译`
- `NO-GO:硬件/刷写`
- `NO-GO:DTS/源修改`
- `NO-GO:i2cdetect`
- `NO-GO:短接`
- `NO-GO:事件注入`

## Touch: CST836U / I2C1 / 0x15 / probe failed / current compatible gap

### Evidence-locked conclusions

| Claim | Evidence |
| --- | --- |
| Factory identity evidence exists, but it is not live-controller proof. | `mainline_recon/I2C_TOUCH_CONTROLLER_AUDIT_20260717.md:12-18` and validation token `driver_name=hyn_ts`, `controller_id=HYN_CST836U_DWZN_MT6572_4.4_V1.0_20181106`. |
| Factory runtime attempted `hyn_ts` on I2C1 address `0x15`, then failed. | `mainline_recon/I2C_TOUCH_CONTROLLER_AUDIT_20260717.md:20-26` and validation tokens `stock_i2c_address=0x15`, `runtime_probe_result=failed`. |
| Current mainline compatible gap remains: only `hynitron,cst340` and `hynitron,cst816s` are evidenced; `hynitron,cst836u` is absent. | `mainline_recon/touch_identity_audit_20260718/README.md:43-47` and validation token `mainline_cst836u_compatible=absent`. |
| The controller-only candidate exists, but it is still unflashed. This is the controller-only unflashed state. | `bridge_backups/products/products_mt6572_i2c1_controller_only_current_20260718_235041/README.md:3-23` and `bridge_backups/products/products_mt6572_i2c1_controller_only_current_20260718_235041/UNTESTED_BOARD_GATE.txt:1-5,14`. |

### Unknowable / barred items

Do **not** infer any of the following from the above evidence:

- touch reset GPIO;
- touch interrupt GPIO, type, or polarity;
- touch power rails or sequencing;
- panel orientation or coordinate transforms;
- successful transfer at `0x15`;
- protocol compatibility with any mainline driver;
- any claim that the controller-only candidate is flashed or board-tested.

## KPD: slot0 / slot9 / /9,%9 / volume switches absent / KPD_DL_KEY3 non-runtime

### Evidence-locked conclusions

| Claim | Evidence |
| --- | --- |
| Runtime keymap is slot `0` and slot `9` only. | `wifi_recon/mt6572_kernel_drsami/mediatek/custom/cci72_we_jb3/kernel/dct/dct/cust_kpd.h:64-69` and validation token `vendor_runtime_keymap=slot0:KEY_VOLUMEUP(115),slot9:KEY_VOLUMEDOWN(114)`. |
| Single-keypad mapping uses `/9,%9`, not a three-column stride. | `mainline_recon/fork_linux_layout2m_mfd_only_strict_src_20260718/drivers/input/keyboard/mt6779-keypad.c:95-100` and validation token `driver_single_keypad_formula=row=slot/9,column=slot%9`. |
| Decompiled DTB shows the 8x9 keypad node and the slot 0 / slot 9 keymap entries. | `mainline_recon/build_touch_i2c1_controller_only_current_20260718/mt6572-z1.decompiled.dts:235-246` and validation tokens `dt_slot0=encoded:0x00000073,row:0,column:0,keycode:115,KEY_VOLUMEUP`, `dt_slot9=encoded:0x01000072,row:1,column:0,keycode:114,KEY_VOLUMEDOWN`. |
| Volume-up/down switches are not soldered on this unit, so a physical scan event is untestable. | `mainline_recon/z1_kpd_physical_limit_20260717.md:3-7` and validation token `physical_reason=volume-up/down switches absent; pads must not be shorted`. |
| `KPD_DL_KEY3` is boot/download-only, not a runtime keymap entry. | `wifi_recon/mt6572_kernel_drsami/mediatek/custom/cci72_we_jb3/kernel/dct/dct/cust_kpd.h:77-79` and validation token `vendor_boot_download_only=KPD_DL_KEY3:slot8:KEY_POWER`. |

### Unknowable / barred items

Do **not** infer any of the following from the above evidence:

- actual continuity or solder state of the PCB pads beyond the reported absence of switches;
- active level, pull-up/pull-down, or reference scheme for the pad pair;
- any physical scan on this unit;
- any new runtime key mapping beyond slots `0` and `9`;
- any interpretation of `KPD_DL_KEY3` as a runtime slot.

## Final NO-GO summary

- `NO-GO:编译`
- `NO-GO:硬件/刷写`
- `NO-GO:DTS/源修改`
- `NO-GO:i2cdetect`
- `NO-GO:短接`
- `NO-GO:事件注入`

## Validation tokens used verbatim

- `factory_boot_sha256=2dd41702e839ec2547ce0dea2bbd3603a5d019786502d939ebba4a6b578eaab5`
- `runtime_probe_result=failed`
- `mainline_cst836u_compatible=absent`
- `vendor_runtime_keymap=slot0:KEY_VOLUMEUP(115),slot9:KEY_VOLUMEDOWN(114)`
- `driver_single_keypad_formula=row=slot/9,column=slot%9`
- `physical_reason=volume-up/down switches absent; pads must not be shorted`
- `vendor_boot_download_only=KPD_DL_KEY3:slot8:KEY_POWER`

## Reproducibility note

`INPUT_SHA256SUMS` pins the evidence sources; `OUTPUT_SHA256SUMS` pins the
five files in this directory.
