# Z1 eMMC Stage A CID diag board result (2026-07-27)

## Image
- `out/boot_z1_emmc_cid_diag_20260727_184905.img`
- SHA256 `9fde80a392de3a102ccc1f23ca9f197610ae840ac51fdfa1718560a91e3b527c`
- Log: `mainline_recon/z1_boot_emmc_cid_diag_20260727_185636.log`
- Kernel `#38 SMP Mon Jul 27 18:46:09 CST 2026`

## Pass
- Boot + logger initrd OK
- pwrap write-deny active
- power defer -517 then rails:
  - VEMC@0x0508 = `0xc000` (EN|QI)
  - VIO18@0x0556 = `0xc000` (EN|QI)
- pinctrl handoff OK (no -524 / Invalid pull)
- clock setup OK: src≈199.875 MHz, mclk=400000, actual=399750
- **CMD0 (GO_IDLE) ok** (no host error)

## Fail
- CMD1 (SEND_OP_COND) returns `resp=00000000` for 100 polls × 3 OCR tries
  - OCR `0x40300000`, `0x403c0000`, `0x40000080`
  - never `MMC_CARD_BUSY`
  - final: `Z1 direct CID failed: -110 (opcode=1)`
- No cmd_error/-ETIMEDOUT on individual CMD1 → controller got CMDRDY with zero R3

## MSDC snapshot (pre-CMD0 == CID-fail)
```
cfg=00007d9b iocon=00000000 ps=80fe0002
sdc_cfg=02100000 sdc_sts=00000000
inten=00000000 int=00000000
patch=403c0446 pb1=ffff4089
```
Decode (12-bit div path / mt2701_compat):
- MSDC_CFG: MODE|CKPDN|CKSTB + CKDIV≈0x7d → ~400 kHz from ~200 MHz src
- MSDC_PS: CDSTS=1, DAT sample `0xfe`, CMD sample bit24=0, WP=1
- SDC_CFG bus width field 0 → 1-bit

## Factory eMMC (from mtkclient, still alive for flash)
`m_emmc_cid = 3447316490014a48cc45c1eb04050aa2`

## Next hypotheses (offline; no flash yet)
1. Pad sample / bus electrical: CMD line sample low in PS; may need drive-strength, SMT, or different R1R0.
2. eMMC still in boot/alternate mode after LK; may need longer idle, double CMD0, or explicit non-boot path.
3. Open-drain vs push-pull at identify (legacy SD/MMC) — mainline mtk-sd may not toggle pad OD.
4. Host may be talking but card OCR path wrong — less likely given all-zero R3 continuously.
5. Keep Stage A safety: no mmcblk, no regulator write, write-deny on.

## Safety still held
No `/dev/mmcblk*`, no regulator enable, pwrap write-deny, freeze after attempt.
