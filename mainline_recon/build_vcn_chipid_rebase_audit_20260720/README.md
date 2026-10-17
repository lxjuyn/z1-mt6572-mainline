# MT6572 VCN + CONNSYS chip-ID rebase — compile-only

Status: **COMPILE-ONLY / DT-DISABLED / NOT BOARD ELIGIBLE**

This tree rebases the old, narrow MT6572 integrated-CONNSYS chip-ID driver on
top of the already-audited layout2m VCN18/VCN28 registration source fork.  It
exists only to prove that the old default-off driver, its dependencies and its
disabled Device Tree node can coexist with the newer regulator prerequisite
stack.

The DT node remains `status = "disabled"`.  No boot image, packaged
`zImage_appdtb`, flash script or hardware launcher is produced by this audit.
Inherited `zImage`/`vmlinux` artifacts copied with the build directory are
explicitly purged and recorded in `INHERITED_BOOT_ARTIFACT_PURGE.txt`.

The driver is intentionally limited to a later, independently authorized
sequence: request VCN18 and VCN28, resume only the CONN power domain, read the
chip-ID register at `0x18070008`, then unwind runtime PM and both rails.  It
does not request an IRQ, clock, reset, firmware, DMA, BTIF, WLAN HIF or shared
EMI resource, and it has no direct MMIO write path.

This compile result does **not** authorize regulator writes or a board test.
The already-passed read-only MT6323 state capture is evidence for a separate
VCN-registration gate; that gate has not been designed, flashed or run.
