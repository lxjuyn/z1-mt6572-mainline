# MT6572 multimedia read-only probe: read_mask=0 board result

Result: **PASS**

## Exact target

- Running kernel: `Linux 7.0.0-rc7 #23 SMP Fri Jul 17 20:47:26 CST 2026 armv7l`
- Module SHA256:
  `1bb3ecc2d8e0b49a6756a63d4dee1d3e1015618f46a9d7bfe249bae0842b79cd`
- Module vermagic: `7.0.0-rc7 SMP mod_unload ARMv7 p2v8`
- Initial taint: `0`
- Test parameter: `read_mask=0`

## Observed resources

| Block | Resource | Range | Size |
|---|---:|---:|---:|
| SMI common AO | 0 | `0x1000e000..0x1000efff` | `0x1000` |
| MMSYS | 0 | `0x14000000..0x14000fff` | `0x1000` |
| SMI common EXT | 1 | `0x14011000..0x14011fff` | `0x1000` |
| LARB0 | 0 | `0x14010000..0x14010fff` | `0x1000` |
| M4U | 0 | `0x10203000..0x10203fff` | `0x1000` |

All four optional MMIO paths reported `MMIO read skipped`.  No mapping or
register read/write was performed.  Module init ended with:

```text
done result=0; no registers were written
```

`insmod` and `rmmod` both returned zero.  The expected out-of-tree taint became
4096.  After unload, `/dev/fb0` and `/dev/dri/renderD128` remained present,
Lima runtime PM was `suspended`, USB gadget remained responsive, and no Oops,
panic, external abort, unhandled fault, GPU fault/hang/timeout, or new watchdog
event was found.

## Evidence

- `z1_read_mask0_board_20260717.log`
- `z1_post_mask0_health_20260717.log`
- `z1_transfer_only_20260717.log`
- `final_build.log`
- `final_static_audit.txt`
- `SHA256SUMS`

The C3 bridge did not receive UART bytes during the pre-test health check.
Because `read_mask=0` cannot reach `of_iomap()` or `readl_relaxed()`, this gate
was completed using the responsive USB gadget shell and post-test health audit.
A nonzero read mask remains blocked until independent UART monitoring is
restored or an equivalent failure-observation channel is established.
