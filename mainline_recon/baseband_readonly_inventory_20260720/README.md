# Z1/MT6572 baseband read-only inventory — 2026-07-20

## Scope

This audit identifies the vendor baseband dependency chain and compares it to
the exact current Z1 mainline DTB/config and preserved factory system image.
It is **read-only**: it never loads CCCI, maps a modem register, starts/reset a
modem, mounts the factory image, writes a partition, or creates a boot image.

## Result

The factory system image contains a 2G MD1 payload at
`/etc/firmware/modem_1_2g_n.img` and CCCI user-space helpers. The scatter has
no standalone modem partition, so the system-image payload is important
evidence, but it is not enough to establish a runnable mainline firmware ABI.

The vendor integration is not a small serial driver. Its platform code assumes
CCIF, modem reset/watchdog, AP/MD memory remapping, MD/DSP image areas,
2 MiB-aligned shared memory, EMI/MAU protection, platform IRQs, signed-image
handling, CCCI kernel modules, and Android services. None of those resources
has an MT6572 CCCI/CCIF node or reserved-memory contract in the current Z1
DTB/config.

Therefore there is no safe baseband port candidate. Do not copy the vendor
driver into the mainline tree, use the vendor numeric addresses, start the
modem, change memory reservations, or load the factory payload.

## Safe next gate

Before any code change, obtain authoritative MT6572/Z1 hardware documentation
or an independently reproducible vendor boot-memory layout proving all of:

1. AP CCIF and MD CCIF register ranges, interrupts, clocks and reset ownership;
2. MD/DSP/CCIF/MDIF regions and the exact shared-memory location/size/alignment;
3. IOMMU/EMI/MAU/MPU security permissions and ordering;
4. exact modem firmware format, authentication policy and load path;
5. electrical PMIC/rail/reset sequencing; and
6. a factory rollback image plus an isolated recovery procedure.

Only then is a DT-disabled, non-starting resource-description compile audit
conceivable. Board execution must remain after an independent review.

`VALIDATION=PASS` validates this evidence boundary only; it does not validate
or enable telephony.
