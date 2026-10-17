# MT6323 PMIC function-graph tracing rebase — compile-only execution plan

**Date:** 2026-07-20  
**Scope:** host-side plan and static validation package only  
**Status:** `PLAN_STATIC_VALIDATION=PASS` after running the validator; `COMPILE_REBASE_EXECUTION=NOT_RUN`; `BOARD_GATE_PROMOTION=NO_GO`

This package is the rebase plan for adding function-graph tracing prerequisites
for the existing MT6323 VCN PMIC trace contract. It is deliberately **not** a
kernel build, boot-image package, board candidate, flash package, or hardware
procedure. No DTS, kernel source, existing `.config`, `out/`, image, or flash
tree is changed by this package.

## Immutable inputs

The plan is bound to these read-only inputs:

| Input | Role | SHA256 |
|---|---|---|
| `mainline_recon/mt6323_vcn_pmic_runtime_trace_plan_20260720/README.md` | existing PMIC trace plan and runtime contract | `b416d7f53d9ec18a369b45e457c600f220317f2d4f59380fb2e537ae0e805fa8` |
| `mainline_recon/mt6323_vcn_pmic_runtime_trace_plan_20260720/validation.txt` | trace-plan static result | `3ee52311d9f63604316fc6b43402e7ee38d903b81f722294ec4491a201add639` |
| `mainline_recon/build_mt6323_vcn_registration_only_board_candidate_20260720/README.md` | VCN candidate gate scope | `fe40599e5794c5faf53b37b606bd9d4e1c64279bfbd3ad81a9c3401ad5bd5955` |
| `mainline_recon/build_mt6323_vcn_registration_only_board_candidate_20260720/validation.txt` | candidate host validation | `8934068649ec0609b1922ef471ff426282252c618b3bcc548bd38c713f7a0057` |
| `mainline_recon/build_mt6323_vcn_registration_only_board_candidate_20260720/UNTESTED_BOARD_GATE.txt` | candidate authorization state | `0853481cc49513ed59568b4684fbb85e18becb6a0fa5cb482f650619b891f336` |
| `mainline_recon/fork_linux_mt6323_vcn_register_audit_src_20260718/drivers/base/regmap/trace.h` | tracepoint fields and event names | `58ddc0930c6dbbd120fb1707f3e08651c9068465be2e0d9905f5514f060fa41c` |
| `mainline_recon/fork_linux_mt6323_vcn_register_audit_src_20260718/drivers/mfd/mt6397-irq.c` | PMIC IRQ callsites to attribute | `be6f4059b169ddfcc48a65539f94969d83f220602c09d4df66b7a4e81bb9a61e` |
| `mainline_recon/fork_linux_mt6323_vcn_register_audit_src_20260718/drivers/regulator/mt6323-regulator.c` | regulator probe attribution | `e1b175d35301458515727ca3a7bd7716ac4bbdabc3bb309e7285117cd324e00d` |
| `mainline_recon/build_mt6323_vcn_register_audit_20260718/dot.config` | current baseline configuration | `23189a3af137bc60c346c3930fe23a0459b7a9b27d74cc491be02e84bc6774ce` |

The current baseline is the `dot.config` above, not an unbound config copied
from another build. The existing trace plan confirms `CONFIG_FTRACE=y`,
`CONFIG_DEBUG_FS=y`, and `CONFIG_TRACING_SUPPORT=y`, while function-graph
symbols are not yet proven enabled.

## Minimum allowed configuration delta

The only intentional config changes permitted by this rebase are exactly:

```diff
-# CONFIG_FUNCTION_TRACER is not set
+CONFIG_FUNCTION_TRACER=y
+# CONFIG_FUNCTION_GRAPH_TRACER is not set   (if emitted by the baseline)
+CONFIG_FUNCTION_GRAPH_TRACER=y
```

Semantics:

- `CONFIG_FUNCTION_TRACER=y` is the prerequisite for function entry/return
  instrumentation.
- `CONFIG_FUNCTION_GRAPH_TRACER=y` enables the function-graph tracer.
- Existing `CONFIG_FTRACE=y`, `CONFIG_TRACING_SUPPORT=y`, `CONFIG_DEBUG_FS=y`,
  `CONFIG_TRACE_CLOCK=y`, `CONFIG_STACKTRACE=y`, and `CONFIG_KALLSYMS=y` are
  retained; they are not new delta items.
- `CONFIG_FTRACE_SYSCALLS`, `CONFIG_DYNAMIC_FTRACE`, `CONFIG_FUNCTION_PROFILER`,
  PM tracing, or any unrelated debug/performance option must not be added as
  convenience changes. If `olddefconfig` would change any symbol beyond the
  two requested symbols, the rebase fails and must not proceed.

This package does **not** run `olddefconfig`; it records the command for a
future, separately authorized compile-only execution and statically checks the
expected minimal delta.

## Isolated `O=` plan

The future output directory is inside this package and must be new/empty before
execution:

```text
SOURCE=/home/<user>/claude/6572/mainline_recon/fork_linux_mt6323_vcn_register_audit_src_20260718
O=/home/<user>/claude/6572/mainline_recon/mt6323_pmic_function_graph_compile_rebase_plan_20260720/O/kbuild
CONFIG_INPUT=/home/<user>/claude/6572/mainline_recon/build_mt6323_vcn_register_audit_20260718/dot.config
```

The only future Kbuild operation in scope is `olddefconfig` with this isolated
`O=`. The plan does not authorize executing it in this task. The source tree,
the historical build output, `out/`, and every other directory remain
read-only. The future operator must copy the input config into the isolated
`O=` before `olddefconfig`; the copy and resulting config must be hashed.

## Explicitly forbidden

The following are hard failures, not optional follow-up steps:

- any actual compiler/linker or target build: `vmlinux`, `zImage`, `Image`,
  `dtbs`, `all`, modules, `modules_install`, packaging, `mkbootimg`, or boot
  image generation;
- creation or modification of `out/`, boot images, ramdisks, DTB/DTS products,
  flash artifacts, or files in any directory outside this package;
- flash/download/fastboot/SP Flash Tool commands, UART/USB/serial procedures,
  power-cycle instructions, board probes, `devmem`, PMIC register writes, or
  any hardware access;
- DTS edits, kernel-source edits, driver patches, or config changes outside
  the two-symbol delta above;
- adding a board-gate launcher or treating a future trace-capable kernel as a
  board-authorized image.

Authorization remains:

```text
COMPILE_ONLY=YES
BOOT_IMAGE=NO
OUT_WRITE=NO
FLASH=NO
HARDWARE=NO
VCN_WRITE=NO
BOARD_GATE_PROMOTION=NO_GO
```

## Static success and failure criteria

### This package succeeds when

1. all immutable input hashes match `INPUT_SHA256SUMS`;
2. tracepoints contain `regmap_reg_read` and `regmap_reg_write` with regmap
   name, register, and value fields;
3. the required IRQ/regulator attribution callsites exist;
4. the candidate gate is host-validated but still `BOARD_GATE_PROMOTION=NO_GO`,
   with `BOARD_IMAGE_AUTHORIZATION=NO` and `VCN_WRITE_AUTHORIZATION=NO`;
5. the baseline has the expected existing tracing symbols and no function-graph
   enablement, and the requested delta is exactly the two symbols above;
6. the README contains one isolated `O=` path under this package and no
   executable build/flash/hardware command outside the allowed `olddefconfig`
   record;
7. the package contains no forbidden artifact; and
8. `validation.txt` and both SHA256 manifests self-check successfully.

A PASS here means **the rebase plan is internally consistent**. It does not
mean that compilation, boot, trace capture, or board behavior has been proven.

### Any of these is a hard failure

- input hash mismatch or missing input;
- any unexpected config delta, missing prerequisite, or ambiguous `olddefconfig`
  result;
- `O=` outside this package or reuse of a pre-existing build/output directory;
- any boot/image/flash/hardware operation in an executable command record;
- any forbidden artifact or an output manifest mismatch;
- candidate authorization changing to GO, or runtime PMIC evidence being
  mistaken for this host-only static result.

On failure: record the failing check, do not broaden the config, do not build,
and do not promote or flash anything.

## Future board-test log contract (data contract only; no action authorized)

A later, separately approved board test must provide machine-readable evidence
from the exact trace-capable candidate. This section defines **what logs must
contain**, not how to access hardware.

### Required evidence files

```text
trace_contract_manifest.txt
function_graph_raw.txt
pmic_regmap_raw.txt
boot_identity.txt
trace_parser_validation.txt
```

The manifest must bind SHA256 values for all five files, the exact kernel/image
identity if one is ever approved, the source/config/O= provenance, parser
version/hash, UTC and local timestamps, and the test authorization record.
This package intentionally has no such runtime files.

### Required trace streams and fields

- `function_graph_raw.txt`: function-graph records with timestamp, CPU,
  duration/return marker, and function name; preserve the raw stream without
  hand-editing.
- `pmic_regmap_raw.txt`: `regmap:regmap_reg_read` and
  `regmap:regmap_reg_write`, with timestamp, CPU if available, regmap/device
  identity, register, value, and source event name.
- Required function names: `mt6397_irq_init`, `mt6397_irq_handle_reg`,
  `mt6397_irq_thread`, `mt6397_irq_pm_notifier`,
  `mt6397_irq_sync_unlock`, and `mt6323_regulator_probe`.
- `boot_identity.txt`: MT6323 CID `0x23`, PWRAP/regmap identity, candidate
  identity, and phase-marker status.

### Required phase/negative markers

The parser contract must identify:

```text
BOOT_REGISTRATION_BEGIN
BOOT_REGISTRATION_END
IRQ_EXERCISE_BEGIN
IRQ_EXERCISE_END
PM_SUSPEND_PREPARE=ABSENT
PM_POST_SUSPEND=ABSENT
```

The first gate must not exercise suspend/resume. Any missing, ambiguous, or
out-of-order marker is `FAIL`.

### PMIC write acceptance policy

During ordinary registration, only these writes may be accepted:

1. `mt6397_irq_init`: `0x0160 <- 0x0000` and `0x0166 <- 0x0000`;
2. `mt6397_irq_handle_reg`: `0x0172` or `0x0174`, with the value equal to the
   immediately preceding same-register status read in the same handler.

There must be no regulator-originated VCN18/VCN28 enable, disable, vsel, mode,
child-device, suspend, or resume write. Unknown regmap identity, unclassified
write, missing read-before-ack pair, or phase ambiguity is `FAIL` and stops
promotion. This is a future parser/log rule, not runtime evidence collected
here.

## Validation status and next action

```text
PLAN_STATIC_VALIDATION=PASS
COMPILE_REBASE_EXECUTION=NOT_RUN
RUNTIME_TRACE_EVIDENCE=NOT_COLLECTED
BOARD_GATE_PROMOTION=NO_GO
BOARD_IMAGE_AUTHORIZATION=NO
VCN_WRITE_AUTHORIZATION=NO
```

The next allowed step is a human-reviewed, compile-only execution in the
isolated `O=` after verifying this package and preserving the same input
hashes. It must not create a boot image or touch hardware.
