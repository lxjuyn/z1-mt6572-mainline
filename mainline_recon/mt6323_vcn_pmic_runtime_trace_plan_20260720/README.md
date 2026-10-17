# MT6323 VCN candidate-runtime PMIC trace plan — 2026-07-20

Status: **PLAN STATIC PASS / RUNTIME EVIDENCE NOT COLLECTED / NO HARDWARE ACTION**

The current kernel source already contains `regmap:regmap_reg_read` and
`regmap:regmap_reg_write` tracepoints.  Each event records PMIC regmap name,
register and value.  This plan adds the missing phase/function attribution and
machine-checkable parser contract; it does not modify the kernel or ask for a
board connection.

## Required trace data

Capture, from the exact future VCN candidate:

```text
tracepoint: regmap:regmap_reg_read
tracepoint: regmap:regmap_reg_write
fields: timestamp, regmap name, reg, val
function graph: mt6397_irq_init, mt6397_irq_handle_reg,
                mt6397_irq_thread, mt6397_irq_pm_notifier,
                mt6397_irq_sync_unlock, mt6323_regulator_probe
phase markers: BOOT_REGISTRATION_BEGIN/END, IRQ_EXERCISE_BEGIN/END
PM markers: no PM_SUSPEND_PREPARE or PM_POST_SUSPEND in the first gate
identity: MT6323 CID 0x23 and PWRAP regmap/device identity
```

## Mechanical acceptance rules

During the ordinary registration phase, every PMIC write must be one of:

1. `mt6397_irq_init`: `0x0160 <- 0x0000`, `0x0166 <- 0x0000`;
2. `mt6397_irq_handle_reg`: `0x0172` or `0x0174`, with value equal to the
   immediately preceding same-register status read in the same handler;
3. no regulator-originated write, no VCN18/VCN28 enable/mode/vsel write, no
   child-device write, no suspend/resume write.

Any unclassified write, missing read-before-ack pair, unknown regmap identity,
or phase ambiguity is `FAIL` and must stop promotion.

## Current configuration gap

The audited `.config` has `CONFIG_FTRACE=y`, `CONFIG_DEBUG_FS=y` and
`CONFIG_TRACING_SUPPORT=y`, but does not yet prove the complete function-graph
tracing configuration.  A future compile-only instrumentation rebase must
explicitly record `CONFIG_FUNCTION_TRACER`, `CONFIG_FUNCTION_GRAPH_TRACER`,
tracefs/debugfs availability, and the parser/tool hashes before any board gate.

This plan does not authorize flash, VCN writes, USB runtime testing, or
suspend/resume.
