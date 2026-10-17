# Z1/MT6572 active task queue — 2026-07-17

Authoritative state: `CURRENT_STATUS_20260717.md`.
CONNSYS evidence: `MT6572_CONNSYS_AUDIT_20260717.md`.
CONNSYS next gate: `CONNSYS_WIFI_BT_NEXT_GATE_20260717.md`.
Priority matrix: `PRIORITY_DRIVER_GATE_MATRIX_20260718.md`.
Test batches: `DRIVER_TEST_BATCHES_20260717.md`.

## P0 — simplefb board gate (completed)

- [x] Preserve first simplefb failure and fix `/chosen/ranges`.
- [x] Build/archive simplefb v2: `out/boot_mainline_mt6572_simplefb_ranges_kpd_auxadc.img` SHA256 `d28e0203...`.
- [x] Re-audit target/rollback/SHA256; start flash waiter first; flash simplefb v2; capture UART.
- [x] Verify simplefb registration, `/dev/fb0`, 240×320 RGB565/stride 512 and reversible full-frame pattern; user confirmed both random multicolour noise and ordered RGB565 colour bars (white/yellow/cyan/green/magenta/red/blue/black), followed by byte-content-equivalent framebuffer restoration.

## P1 — input/GPU/display

- [x] Correct KPD to 72-slot single-keypad mapping (`slot/9`, `slot%9`).
- [x] Document BusyBox ARM32 input-event decoder.
- [~] Physical key capture is hardware-limited: this Z1 has no volume switches populated, only pads. KPD probe/input0 is confirmed; do not short pads without proven pin/reference evidence.
- [x] Clean-build, package, flash and readback-validate the fresh Lima image: `out/boot_mainline_mt6572_lima_simplefb_fresh.img`, SHA256 `5a0c7358...`.
- [x] Board-probe Lima DRM and render nodes; validate UAPI context/GEM operations and runtime PM.
- [x] Execute Mesa EGL surfaceless GP/PP rendering, 240×320 RGB565 readback and a user-confirmed 6000-frame rotating/scaling/moving triangle. Final GP/PP IRQ counts 6001/6000; MMU IRQs zero; no GPU fault/hang/timeout; framebuffer restored byte-identically.
- [x] Complete the `read_mask=0` multimedia DT-resource inventory gate with an external module: exact MMSYS/SMI/LARB0/M4U resources confirmed, no MMIO mapping/read/write, clean unload and post-health pass.
- [ ] Restore independent C3 UART monitoring, then review and execute `read_mask=1` (MMSYS status only). Keep SMI EXT/LARB0/M4U, OVL/RDMA and DSI/ST7785M as later separate gates; do not conflate current CPU-copy simplefb path with native KMS scanout.

## P1 — integrated MT6572 CONNSYS

- [ ] Priority 1: Wi-Fi/BT. Audit and stage integrated CONNSYS in this exact
  order: strict PMIC MFD-only gate, VCN rail evidence, chip-ID-only, BTIF,
  WLAN AHB HIF, DMA/firmware. Do not use the quarantined external MT6620/SDIO
  branch.
- [x] Correct architecture: integrated CONNSYS + BTIF + WLAN AHB HIF; external MT6620/SDIO modules quarantined.
- [x] Prove Z1 1 MiB carveout and vendor EMI/address/IRQ/power resources.
- [x] Add/compile inert default-off CONN genpd provider with no consumer.
- [x] Compile-audit a 2 MiB-aligned layout-only kernel; fixed output `0x80208000`, no boot image packaged.
- [x] Board-test layout-only kernel: strict flash/readback PASS; Linux System
  RAM is `0x80200000..0xbf3fffff`; simplefb, Lima and USB gadget baseline PASS.
  C3 still does not receive UART bytes, so early-UART continuity remains an
  independent bridge issue. MMC block-device enumeration is absent in both the
  old mainline baseline and this gate, and is not credited as a layout result.
- [x] Compile-audit a default-off chip-ID-only consumer in an isolated source
  tree. It reads only `0x18070008` after VCN18/VCN28 + CONN genpd sequencing;
  W=1 object/DTB build passed. No zImage or boot image was produced.
- [x] The first strict MFD-only board run proved the PWRAP/MFD binding and
  MT6323 CID low byte `0x23`, with no IRQ/fault regression, but the image used
  the old `0x80000000` RAM layout. It is INVALID as a formal gate because it
  did not preserve the accepted layout2m baseline. It has been archived and
  marked INVALID/SUPERSEDED.
- [x] Build, validate, independently re-audit and archive the corrected
  layout2m+MFD-only single-variable candidate:
  `out/boot_mainline_mt6572_lima_simplefb_layout2m_mfd_only_strict.img`,
  SHA256 `e1439d7474...a0871`. Offline board-candidate verdict is GO; hardware
  result remains UNTESTED.
- [x] Run the corrected strict MFD-only board gate, complete the separate
  read-only Stage-B capture, and restore/readback/boot-verify the accepted
  layout2m baseline. Stage-B proved CID `0x23`, 28 rails, VCN18/VCN28 off,
  `writes=0`, and cleanup.
- [x] Build and static-validate a no-flash, read-only 28-rail MT6323 state
  audit module. It references `regmap_read` only and must run only after the
  exact corrected MFD-only gate passes.
- [x] Use the captured rail state to package and independently validate a
  host-only VCN18/VCN28 registration-only candidate. It registers exactly two
  rails with read-only regulator operations and no consumers. The improved
  candidate uses a non-interactive static ARM capture ramdisk and is archived
  as `products_mt6323_vcn_registration_only_readonly_ramdisk_candidate_provenance_hardened_20260720_221000`
  (image SHA256 `6da4604e...d291b98`).
- [x] Complete static promotion review: the PMIC parent write allow/deny
  policy and capability-minimized ramdisk policy pass offline.
- [x] Record the offline PMIC runtime-trace plan at
  `products_mt6323_vcn_pmic_runtime_trace_plan_20260720_190117`.
- [x] Run and archive the host-only provenance rebuild audit:
  `products_mt6323_vcn_provenance_reproducibility_audit_hardened_20260720_220500`.
  The Z1 DTB reproduced but three zImage rebuild attempts mismatched; this is a
  NO-GO blocker, not a completed provenance closure.
- [ ] Complete the function-graph-enabled compile-only rebase, candidate-runtime
  PMIC trace evidence, USB runtime validation of the capture ramdisk, and a new
  fully captured deterministic clean-build/package chain. The VCN candidate
  remains unflashed and explicitly not authorized.
- [x] Rebase the bounded chip-ID audit onto the VCN source and compile it with
  the chip-ID DT node disabled. This remains compile-only evidence.
- [ ] After a separately authorized VCN board PASS, create a chip-ID-only
  board gate; then continue BTIF, AHB HIF, GDMA and firmware/EMI separately.

## P2 — PMIC/basic SoC/I2C

- [x] Prove minimal MT6323 poweroff Kconfig closure and linked symbols.
- [x] Audit PMIC IRQ-domain/regulator risk. Both old-layout MFD images are
  NO-GO for future flashing: `26e8d4...bd42` failed strict child isolation;
  `8c53ccf6...d1c85` passed the hardware probe but failed single-variable gate
  construction because it did not retain layout2m. The corrected layout2m
  candidate `e1439d74...a0871` now has offline GO and awaits its controlled
  board gate.
- [ ] Compile-audit AUXADC only; do not board-enable it with MFD.
- [x] Compile-audit I2C-controller-only in an isolated source tree; W=1 object
  and Z1 DTB passed, speculative Goodix remains disabled, and no image exists.
- [x] Audit MMC controller absence separately from the layout gate: `mmc0` is
  eMMC and depends on MT6323 VEMC3V3/VIO18; `mmc1`/TF remains unproven.
- [ ] IOMMU/SMI/power-domain tests remain separate.

## User priority order

- [~] Priority 2: audio architecture audit completed. Mainline lacks an
  MT6572 AFE platform driver and verified MT6323 codec path; vendor raw
  PWRAP/AFE code is not directly portable. Register/clock/routing extraction
  remains.
- [~] Priority 3: touch controller compile gate completed. First board image
  may enable only I2C1; the vendor `hyn_ts@0x15` probe is identity evidence,
  not enough to enable a client. GT911 stays disabled.
- [x] Priority 4: keys: preserve the verified KPD mapping; PMIC keys only
  after strict MFD proof; physical volume switches are not populated.
- [~] Priority 5: TF/MMC audit completed. `mmc0` is non-removable eMMC;
  one-bit bus width and PMIC rails need a dedicated gate. Z1 TF/mmc1 hardware
  remains unproven.
- [~] Priority 6: baseband inventory completed. Vendor MD1/CCCI/shared-memory
  requirements are identified, but mainline has no MT6572 CCCI/CCIF or Z1
  node and the scatter has no standalone modem partition. Read-only firmware
  layout work remains; modem start/reset is forbidden.

## Safety/loop

- Back up every source/script/image edit and archive every key artifact with SHA256.
- Never flash FAILED/SUPERSEDED/INVALID artifacts.
- The 5-minute `z1_driver_supervisor_loop.sh` is telemetry/guard only; it has
  been updated to verify the latest goal matrix, strict MFD/Stage-B evidence,
  chip-ID rebase compile audit, VCN registration-only host candidate, and the
  expected provenance-rebuild mismatch audit.
  It runs as the persistent user unit `z1-driver-supervisor.service`; it cannot
  invoke or redispatch the model.
- The invalid old-layout MFD image has been replaced by the board-proven
  layout2m baseline. The user has now powered Z1 off and disconnected data
  cables; no flash waiter is active. Do not request reconnection while the VCN
  promotion audit remains NO-GO.
