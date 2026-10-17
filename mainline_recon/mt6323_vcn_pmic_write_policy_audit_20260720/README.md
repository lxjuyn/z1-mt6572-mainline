# MT6323 VCN registration-only PMIC write policy audit — 2026-07-20

Status: **STATIC POLICY PASS / RUNTIME EVIDENCE REQUIRED / NOT A BOARD GATE**

This is an offline policy audit for the VCN18/VCN28 registration-only source
lineage.  It does not change the kernel, device tree, ramdisk, boot image, or
PMIC state.

## MT6323 ordinary registration-phase policy

Default policy is **deny**.  The only statically recognized parent-IRQ writes
in an ordinary boot/registration phase are:

| Class | Function | Register | Value rule |
| --- | --- | --- | --- |
| init mask | `mt6397_irq_init` | `0x0160` (`INT_CON0`) | exactly `0x0000` |
| init mask | `mt6397_irq_init` | `0x0166` (`INT_CON1`) | exactly `0x0000` |
| IRQ acknowledge | `mt6397_irq_handle_reg` | `0x0172`/`0x0174` (`INT_STATUS0/1`) | write back the immediately preceding read from the same register |

The following are denied in the ordinary VCN registration-only phase:

- any regulator-originated PMIC write;
- any VCN18/VCN28 enable, disable, voltage-select, or mode write;
- any PMIC-child write outside the MFD parent IRQ controller;
- any PMIC write without PMIC/regmap identity, phase, and function/callsite
  attribution;
- any suspend/resume notifier write.

Suspend/resume writes to `0x0160`/`0x0166` are only conditionally allowable in
a future explicit suspend/resume experiment, with PM-event markers and
callsite evidence.  They are not allowed in the first VCN registration-only
gate.

## Runtime evidence still required

The source audit cannot prove the board's actual PMIC write sequence.  A future
candidate must collect and archive, from that candidate itself:

1. PMIC regmap read/write events with register and value;
2. function/callchain attribution for `mt6397_irq_init`,
   `mt6397_irq_handle_reg`, `mt6397_irq_pm_notifier`, and regulator write
   helpers;
3. explicit boot-phase markers and no suspend/resume marker;
4. CID/regmap identity tying events to MT6323;
5. a machine-checkable result that every observed write is in the allowlist,
   every IRQ ack has matching preceding read, and no regulator write occurred.

Until those runtime records exist, this audit does **not** authorize a board
image, VCN rail write, or flash command.
