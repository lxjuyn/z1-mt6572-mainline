# MT6572 Z1 2 MiB layout-only board gate

Status: **BOARD PASS — 2026-07-17**

This image changes only the Linux physical-memory/decompression layout needed
to keep the proven integrated-CONNSYS carveout outside Linux:

- kernel decompression target: `0x80208000`;
- DT RAM: `0x80200000..0xbfffffff`;
- `0x80000000..0x800fffff` remains outside Linux for CONNSYS;
- `0x80100000..0x801fffff` is intentionally unused because
  `ARM_PATCH_PHYS_VIRT` requires 2 MiB alignment.

Isolation checks:

- the config delta from the known-good Lima/simplefb build is exactly
  `CONFIG_MT6572_Z1_FIXED_ZRELADDR=y`;
- the board-DTS delta is exactly the memory node start/size change;
- `CONFIG_MFD_MT6397` remains disabled;
- the ramdisk in this image is byte-identical to the known-good image;
- no CONNSYS consumer is enabled and no CONNSYS power/register write is added.

Artifact:

- image: `boot_mainline_mt6572_lima_simplefb_layout2m.img`
- size: 5,564,416 bytes
- SHA256: `950a13569a497f5b3365f051be36a42332f263ce537f6883f85b0428ed2d5744`
- 6 MiB partition spare: 727,040 bytes

Packaging validation:

- Android boot header and MTK kernel/ramdisk headers: PASS
- appended DTB and standalone DTB identity: PASS
- fixed-ZRELADDR config: PASS
- DT memory range: PASS
- known-good Lima/simplefb nodes retained: PASS

Board result:

1. strict target write and full-partition readback comparison: PASS;
2. `/proc/iomem`: `80200000-bf3fffff : System RAM`;
3. `0x80000000..0x801fffff` is not reported as System RAM;
4. simplefb, Lima render node, `/dev/ttyGS0` and MUSB UDC: PASS;
5. no panic, Oops, external abort, Lima fault/hang or MUSB/simplefb failure;
6. no CONNSYS consumer or power-on was enabled.

C3 itself enumerated but did not receive UART bytes, so this gate does not
claim continuous early-UART evidence. The normal USB gadget shell provided the
post-boot evidence. MMC block-device enumeration was absent on the previous
mainline baseline as well; it remains a separate driver task, not a layout
regression.

Primary rollback is the board-proven fresh Lima/simplefb image:
`/home/<user>/claude/6572/out/boot_mainline_mt6572_lima_simplefb_fresh.img`,
SHA256
`5a0c7358ec7ac2b4963373f873ba9190abf0bfee0caebedae304dde8fbee9641`.
The factory Android backup `/home/<user>/claude/6572/1/boot.img`, SHA256
`2dd41702e839ec2547ce0dea2bbd3603a5d019786502d939ebba4a6b578eaab5`,
is retained as a secondary recovery image.
