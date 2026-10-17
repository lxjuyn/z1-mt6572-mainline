# Z1 pinctrl -524 fix (2026-07-27)

## Symptom
```
mt6572-pinctrl 10005000.pinctrl: Invalid pull argument
mt6572-pinctrl 10005000.pinctrl: pin_config_group_set op failed for group 44
mtk-msdc 11120000.mmc: Z1 pinctrl handoff failed: -524
```

## Root cause
`pinctrl-mt6572.c` was missing:
1. `[PINCTRL_PIN_REG_PUPD]` field mapping (R1R0 path calls `mtk_hw_set_value(..., PINCTRL_PIN_REG_PUPD, ...)`)
2. `.pull_type` table so combo bias helper selects `MTK_PULL_PUPD_R1R0_TYPE` for pins 44-53

Without those, DTS `bias-pull-* = <MTK_PUPD_SET_R1R0_*>` on mmc0 pins falls through all pull helpers and returns `-ENOTSUPP` (-524).

Upstream `mt6572-mainline-linux` copy has the same gap — this is a real driver bug, not a DTS mistake. Prestigio/jty DTS use the same R1R0 syntax, so they would hit it too.

## Fix
- Map `PINCTRL_PIN_REG_PUPD` to `mt6572_pin_pupd_range` (same HW field as PULLSEL direction bit)
- Add `mt6572_pull_type[154]`: pins 44-53 = `MTK_PULL_PUPD_R1R0_TYPE`, rest = `MTK_PULL_PULLSEL_TYPE`
- Wire `.pull_type = mt6572_pull_type` into `mt6572_data`

R0/R1 ranges already covered pins 44-53 only. No DTS change required for Stage A.

## Safety
Stage A still: no mmcblk, no regulator enable, pwrap write-deny, 1-bit/400kHz CID only.
