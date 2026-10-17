# Read-only and authorization constraints

This is a **configuration-preparation and static-validation** work package only.
It is not a kernel build result and it is not board eligible.

## Writable boundary

Only this directory may be created or changed:

`/home/<user>/claude/6572/mainline_recon/build_mt6323_vcn_pmic_function_graph_compile_20260720/`

The following are read-only inputs and must not be modified:

- `fork_linux_mt6323_vcn_register_audit_src_20260718/` (source snapshot)
- `build_mt6323_vcn_register_audit_20260718/` (registration-audit baseline)
- any existing source tree, `.config`, `out/`, build, backup, package, image, or flash tree outside this package

## Allowed command

Exactly one Kbuild operation is allowed, in the isolated output directory:

```text
make -C /home/<user>/claude/6572/mainline_recon/fork_linux_mt6323_vcn_register_audit_src_20260718 \
  O=/home/<user>/claude/6572/mainline_recon/build_mt6323_vcn_pmic_function_graph_compile_20260720/kbuild \
  olddefconfig
```

No compiler/linker/vmlinux step is part of this package. If `olddefconfig` fails,
the failure is recorded and scope is not expanded.

## Explicitly forbidden

Do not run `vmlinux`, `zImage`, `Image`, `all`, `dtbs`, `boot`, `ramdisk`,
`modules`, `modules_install`, packaging, `mkbootimg`, flash launchers, fastboot,
SP Flash Tool, hardware probes, UART/USB procedures, or any board gate.

## Authorization record

- BOARD_GATE: `NO`
- BOARD_IMAGE: `NO`
- VCN_WRITE: `NO`
- HARDWARE: `NO`
- FLASH: `NO`
