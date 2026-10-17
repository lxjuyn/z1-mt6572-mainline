# MT6572 multimedia read-only audit module

This external module is a staged board-audit tool, not a functional display,
SMI or IOMMU driver.  It is built against the exact Z1 kernel tree.

- `read_mask=0`: inventory DT resources only; no MMIO reads.
- `read_mask=1`: additionally read MMSYS clock-gate status at 0x100/0x110.
- `read_mask=2`: additionally read SMI common EXT configuration registers.
- `read_mask=4`: additionally read LARB0 status.
- `read_mask=8`: additionally read M4U control/configuration state.

Test one bit at a time.  Never jump directly to `read_mask=15` on first board
validation.  The source intentionally contains no register writes, clock/reset,
runtime-PM, IRQ, DMA, component or IOMMU attachment operations.

`deploy_probe.py` is transfer-only by default.  Execution requires an explicit
`--load-mask` argument.  The first board gate is:

```sh
python3 deploy_probe.py --load-mask 0 --unload
```

Do not run a nonzero mask until the preceding isolated gate has passed.

Loading this external module sets the kernel's out-of-tree-module taint bit
(normally changing `/proc/sys/kernel/tainted` from 0 to 4096).  Unloading the
module does not clear that bit; reboot before any experiment that requires a
pristine taint state.
