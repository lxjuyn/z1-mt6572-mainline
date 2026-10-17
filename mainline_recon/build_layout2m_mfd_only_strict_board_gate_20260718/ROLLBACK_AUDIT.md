# Rollback audit — layout2m + strict MT6323 MFD-only

Date: 2026-07-18

## Images

Candidate target:

- path in build:
  `mainline_recon/build_layout2m_mfd_only_strict_board_gate_20260718/boot_mainline_mt6572_lima_simplefb_layout2m_mfd_only_strict.img`
- path used by flash helper:
  `out/boot_mainline_mt6572_lima_simplefb_layout2m_mfd_only_strict.img`
- size: 5,564,416 bytes
- SHA256:
  `e1439d7474aa49eab88b820519cff8753d8ccc174398b6a099bdaf83253a0871`
- the `out/` target exists and is bound by
  `EXTERNAL_DEPENDENCY_SHA256SUMS`.

Primary rollback, board-approved `layout2m`:

- path: `out/boot_mainline_mt6572_lima_simplefb_layout2m.img`
- SHA256:
  `950a13569a497f5b3365f051be36a42332f263ce537f6883f85b0428ed2d5744`
- verified product manifest:
  `bridge_backups/products/products_mt6572_layout2m_board_pass_20260717_031800/SHA256SUMS`
- last restore flash/readback log:
  `mainline_recon/z1_layout2m_restore_flash_20260718_124315.log`
- restore log SHA256:
  `029a31d5ec9035873b5e6ba558a7b48ee7f4872d02026ed06770661cfb8eae1a`

Secondary known boot image:

- path: `out/boot_mainline_mt6572_lima_simplefb_fresh.img`
- SHA256:
  `5a0c7358ec7ac2b4963373f873ba9190abf0bfee0caebedae304dde8fbee9641`
- this is not the primary rollback because it is not the accepted layout2m
  baseline.

Factory fallback:

- path: `1/boot.img`
- SHA256:
  `2dd41702e839ec2547ce0dea2bbd3603a5d019786502d939ebba4a6b578eaab5`

## Flash tool audit

Tool:

- path: `tools/flash_with_retry.sh`
- SHA256:
  `78d4cc37dcb919ce5aa5db50f3b3f0c00120de164e7f6690ca8766925aa9da30`

Audited behavior:

- verifies the full candidate and external-dependency manifests before
  starting the MTK waiter;
- reads the complete target partition after each successful write;
- rejects a readback shorter than the expected image;
- compares the exact expected-length prefix byte-for-byte;
- on write or target-readback failure, writes the supplied rollback image and
  verifies it by the same complete-read/prefix comparison;
- stops immediately with exit code 3 if rollback write/readback fails;
- each MTK write attempt may wait up to 120 seconds; the board-gate launcher
  uses 20 attempts so the physical connection window is deliberately long.

Automatic rollback covers flash-stage failures only. It does not detect a
boot-stage regression after a byte-exact successful target write.

## Manual boot-failure rollback

If the candidate flashes successfully but fails a board acceptance check:

1. power Z1 off;
2. disconnect normal USB;
3. disconnect C3;
4. leave the MTK download cable unplugged;
5. start:

   `tools/flash_with_retry.sh bootimg out/boot_mainline_mt6572_lima_simplefb_layout2m.img 1/boot.img 20`

6. only after the waiter is active, connect the battery, insert the download
   cable and hold power for 3-5 seconds;
7. require full partition readback and an exact prefix match;
8. remove the download cable, reconnect C3, boot and verify the known
   `layout2m` Linux screen and normal USB.

## Battery and operator gate

No flash or boot gate may begin while the battery is low. The offline
candidate can be archived and audited without hardware, but starting
`z1_flash_board_gate.sh` requires a charged battery and an explicit operator
confirmation.
