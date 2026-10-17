# Active Z1/MT6572 driver goal

Status: **ACTIVE**

## Objective

Continue Z1 mainline bring-up without guessing resources and without combining
independent board gates:

1. finish the MT6323 read-only state gate and VCN18/VCN28-only gate;
2. implement integrated MT6572 CONNSYS in this order:
   chip ID → BTIF → WLAN AHB PIO → AP GDMA → firmware/shared EMI;
3. continue audio → touch → keys → TF/MMC → baseband;
4. keep the kernel/minimal initramfs inside the 6 MiB boot partition and use
   the system partition for version-matched modules, firmware and tools after
   a read-only mount gate is proven.

## Safety and quality rules

- Backup before each source or script edit.
- Archive important build products and SHA256 manifests immediately.
- Keep old external MT6620/SDIO/GPIO connectivity code quarantined.
- Do not guess GPIO, pinmux, clock, reset or firmware ABI.
- Do not flash or open a hardware-facing command while Z1 is powered off.
- Ask the user explicitly when a board flash, normal USB connection or C3
  capture is required.

## Current hardware state

- Cable and power state are dynamic and must be rechecked immediately before
  every board action; do not infer them from this document.
- User reported on 2026-07-20 that Z1 is powered off and its data cables are
  disconnected.  No hardware-facing command is running.  Do not ask for a
  cable connection until a separately validated and explicitly authorized gate
  is ready.
- No flash or serial-capture command is currently running.
- The telemetry supervisor records observed `/dev/ttyACM*`/`ttyUSB*` nodes but
  cannot establish which physical cable is attached.

## Current work

- A requirement-by-requirement current evidence ledger is archived at
  `bridge_backups/products/products_z1_goal_evidence_matrix_20260720_210614`.
  It distinguishes completed board gates from compile-only/static audits and
  records that the overall goal remains active because Wi-Fi/BT hardware gates,
  audio/touch/MMC runtime gates and baseband ABI evidence are still pending.
- MT6323 strict MFD-parent-only board gate: **PASS** on 2026-07-18, with the
  board subsequently restored to the approved layout2m baseline. The integrity
  audit is archived at
  `bridge_backups/products/products_mt6323_mfd_layout2m_board_evidence_integrity_20260720_123800`.
  It verifies the eight historical evidence payload hashes, records the single
  invalid self-hash in the original manifest, and confirms flash readback,
  `mt-pmic-pwrap`/`mt6397` binding, zero-to-zero PMIC/PWRAP IRQ samples, CID
  low byte `0x23`, absence of enabled MFD children, and baseline restore. C3
  UART output was inconclusive; normal USB-gadget evidence is the valid capture
  path. The separately audited one-shot read-only 28-rail regulator-state
  Stage-B capture subsequently passed on 2026-07-20: MT6323 CID `0x23`, all 28
  rails decoded, VCN18/VCN28 observed off, `writes=0`, and cleanup confirmed.
  The accepted layout2m baseline was then restored with flash readback and
  boot/USB verification. Evidence is archived in
  `products_mt6323_regulator_state_stage_b_pass_20260720_144714`,
  `products_layout2m_baseline_restore_20260720_145924`, and
  `products_layout2m_baseline_boot_verify_20260720_1515`.
  An independent VCN source-risk audit also passes and is archived at
  `bridge_backups/products/products_mt6323_vcn_registration_risk_audit_20260720_125610`:
  it pins the source delta to the three expected source files plus generated
  DTS copy, verifies only VCN18/VCN28 are enabled without boot/always-on
  constraints or consumers, and records why static review cannot replace the
  preceding `writes=0` board capture. A host-only VCN18/VCN28
  registration-only image has now been repackaged with a capability-minimized
  static ARM capture ramdisk at
  `mainline_recon/build_mt6323_vcn_registration_only_readonly_ramdisk_candidate_20260720`
  and archived as
  `products_mt6323_vcn_registration_only_readonly_ramdisk_candidate_provenance_hardened_20260720_221000`.
  Candidate SHA256 is
  `6da4604e3f31d0c07594b3140962bb9833dab25182c228ed6ccb99141d291b98`.
  The host package and static ramdisk/PMIC policies pass; USB runtime,
  candidate PMIC trace evidence, and a newly captured deterministic clean-build
  lineage remain pending. The host-only provenance rebuild audit is archived as
  `products_mt6323_vcn_provenance_reproducibility_audit_hardened_20260720_220500`: the
  selected DTB reproduced but three zImage rebuild attempts did not, so the
  historical source/config/output provenance is explicitly a mismatch blocker.
  The offline trace-plan audit is archived as
  `products_mt6323_vcn_pmic_runtime_trace_plan_20260720_190117`; it confirms
  the existing regmap tracepoints but records that function-graph configuration
  and runtime evidence are not yet collected. It is not copied to `out/`, has
  no flash launcher, has not been board-tested, and retains
  `BOARD_IMAGE_AUTHORIZATION=NO` and `VCN_WRITE_AUTHORIZATION=NO`.
- WiFi/BT: integrated resource audit complete. The isolated, DT-disabled AP
  BTIF resource-contract object and Z1 DTB compile audit passes; it is not a
  board candidate and cannot skip VCN/chip-ID prerequisites. The external
  MT6620 SDIO stack remains quarantined. The older chip-ID compile source is
  also deliberately **not reusable as-is**: its pre-layout2m fork lacks the
  VCN prerequisite stack and `regulator_ignore_unused`. The read-only rebase
  readiness audit archived at
  `bridge_backups/products/products_mt6572_connsys_chipid_rebase_readiness_20260720_130729`
  requires a fresh VCN-derived rebase only after the PMIC-state and VCN board
  gates pass. A new VCN-derived compile-only rebase now passes with the
  chip-ID DT node still disabled; its product is
  `products_mt6572_vcn_chipid_rebase_compile_audit_20260720_1542`. It is not a
  board image and cannot skip the VCN registration-only board gate.
- WLAN: the isolated, DT-disabled AHB-HIF PIO resource audit for
  `0x180f0000/0x5c` and level-low SPI 123 passes independent review. It does
  not include GDMA, shared EMI, firmware or runtime power/clock sequencing.
- Audio: the non-FPGA MT6572 AFE and MT6323 register contract now passes
  vendor-hash validation, freestanding ARM compile, independent review and
  immutable product-manifest verification. It adds no DT node or hardware
  access path.
- Touch: direct parsing of the factory boot kernel identifies the selected
  driver as Hynitron CST836U (`hyn_ts`) and the stock log records a failed
  I2C1/`0x15` probe. The old GT9XX reference source is not Z1 proof; the
  speculative Goodix node remains disabled. A current-tree I2C1-controller-
  only image now passes full offline config/source/DTB/link/boot-image
  validation and is archived at
  `bridge_backups/products/products_mt6572_i2c1_controller_only_current_20260718_235041`.
  Its boot-image SHA256 is
  `be336ce8bbc62ac72f248594648c53559a7aa2e22bf8c42879af11af73c9ddfe`.
  It is unflashed and binds no touch client.
- Next board action: only when the user explicitly prepares the Z1 for the
  staged **MT6323 read-only regulator-state gate**, run the strict-MFD flash
  stage with normal USB and C3 disconnected; after verified readback, use the
  ordinary USB gadget only for the audited module capture. Do not connect C3
  for this gate and do not enable VCN18/VCN28. The I2C1-controller-only touch
  gate remains queued after this Wi-Fi/BT prerequisite. The Stage-B runner
  `tools/run_mt6323_regulator_state_capture.sh` is archive-first, requires the
  Z1 Gadget Serial by-id path rather than a generic tty node, never flashes,
  and makes a failed capture explicit stop evidence.
- Keys: the current-tree evidence audit is complete and archived at
  `bridge_backups/products/products_mt6572_kpd_current_tree_audit_20260718_012649`.
  It verifies the vendor 72-slot runtime mapping and the existing current
  `/9,%9` keypad driver/8x9 DT keymap: slot 0 is `KEY_VOLUMEUP` and slot 9 is
  `KEY_VOLUMEDOWN`.  Existing board evidence proves the keypad input device
  registered, but the volume switches are physically unpopulated on this Z1;
  no physical scan event can be generated.  No keypad source or DT change is
  justified, and unidentified pads must not be shorted.
- TF/MMC and system payload: the read-only audit is complete and archived at
  `bridge_backups/products/products_mt6572_mmc_system_payload_audit_20260720_001500`.
  It proves the factory eMMC/MSDC0 layout and the current MMC0 resource
  contract, but records the unresolved current `bus-width = <1>` versus eight
  MC0 data-pin configuration, disabled MT6323 MFD provider, lack of MMC0
  enumeration, and absence of Z1 TF-slot evidence. It creates no candidate,
  mounts no image, and leaves Android/system untouched. The next MMC action is
  only after the VCN18/VCN28 registration board gate: a separately packaged,
  read-only MMC0 enumeration gate with MMC1 still disabled.
- Baseband: the read-only inventory is complete and archived at
  `bridge_backups/products/products_mt6572_baseband_readonly_inventory_20260720_123000`.
  It proves that the preserved factory system image contains an MD1 payload at
  `/etc/firmware/modem_1_2g_n.img` (SHA256
  `5264c9c2b567189bedd49c3ff03ebe13664c2e8e09349db8f6d3d9483b1ee4f6`),
  while the factory scatter has no standalone modem partition. The vendor ABI
  requires CCCI/CCIF, modem reset/watchdog, AP/MD remapping, MD/DSP regions,
  2 MiB-aligned shared memory, EMI/MAU protection, modules and services; the
  current Z1 DTS/DTB/config contains none of that contract. No port candidate
  exists and no modem operation is permitted. Further work is blocked on
  authoritative Z1/MT6572 resource, memory-layout and firmware-ABI evidence.
- System payload: Android remains untouched. A Linux-only 900 MiB replacement
  image is allowed only after a separate MMC enumeration and read-only system
  mount gate; the original factory system image stays as the rollback.
- Supervisor: telemetry-only systemd user service, 5-minute interval. It now
  verifies the latest goal matrix, chip-ID rebase audit, VCN host-candidate
  validation/product manifests, and the expected VCN provenance-mismatch audit
  on every iteration.

The supervisor records hashes, manifests, USB state and active processes. It
cannot wake or invoke the model; actual continuation is driven by the active
Codex turn and task plan.
