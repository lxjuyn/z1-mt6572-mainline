# Z1/MT6572 goal evidence matrix — 2026-07-20

Status: **GOAL ACTIVE — NOT COMPLETE**

This is a current evidence ledger, not a replacement for any historical test
record.  It distinguishes a verified board result from a compile/static result
and names the next required evidence.  The historical documents and archives
are immutable; some retain old `UNTESTED_BOARD_GATE` markers even where later
runtime evidence proves the same family of configuration.  The result files
and integrity audit cited below decide the current state.

## Authorization gates

- `BOARD_IMAGE_AUTHORIZATION=NO`
- `VCN_WRITE_AUTHORIZATION=NO`
- The 2026-07-20 MT6323 regulator-state Stage-B evidence is a **read-only board
  PASS** (`writes=0`) and the approved layout2m baseline was restored/booted
  afterward.  It is prerequisite evidence only; it does not authorize a new
  board image, regulator registration image, VCN write, chip-ID node enablement,
  BTIF enablement or WLAN enablement.
- `build_vcn_chipid_rebase_audit_20260720` and
  `products_mt6572_vcn_chipid_rebase_compile_audit_20260720_1542` are
  **compile-only / DT-disabled / not board-eligible** evidence.  Inherited
  bootable artifacts were purged.
- The improved host-only VCN registration candidate is archived at
  `products_mt6323_vcn_registration_only_readonly_ramdisk_candidate_provenance_hardened_20260720_221000`.
  Its hardened manifest now covers the packaging log and external dependency
  manifest, and its exact rebuild recipe is bound to the separately archived
  command record. These are host-side integrity improvements only.
  Its package validation and static ramdisk capability policy pass, but the
  independent promotion state remains **NO-GO**: USB runtime is untested,
  candidate PMIC trace evidence is required, and the completed host-only
  source/config rebuild audit recorded zImage mismatches. The selected Z1 DTB
  reproduced, but no zImage did; the retained historical output also has
  inconsistent generated version metadata. The mismatch audit is archived at
  `products_mt6323_vcn_provenance_reproducibility_audit_hardened_20260720_220500`.
  The offline PMIC trace-plan audit is archived at
  `products_mt6323_vcn_pmic_runtime_trace_plan_20260720_190117`; it records
  the existing regmap tracepoints and the still-unenabled function-graph
  configuration required for a future compile-only rebase. The exact minimal
  two-symbol configuration delta, isolated `O=` path, forbidden build/image
  commands and future log contract are now statically validated in
  `products_mt6323_pmic_function_graph_compile_rebase_plan_repaired_20260720_235955`.
  That plan has **not** run `olddefconfig` or any build. It is planning evidence
  only and does not authorize a board image.
  It remains unflashed and unauthorized.

## Requirement-to-evidence audit

| Objective area | Current evidence | Verified scope | Remaining evidence / status |
| --- | --- | --- | --- |
| Display | `products_simplefb_colorbars_pass_20260717_201735` and current status log | 240×320 RGB565 color bands were visually confirmed in order and framebuffer content/fbcon were restored. | **BOARD PASS** for simplefb console/handoff; native display pipeline remains a separate future task. |
| GPU | `products_lima_board_probe_uapi_pass_20260717_222618`, `products_lima_realtime_animation_pass_20260717_000824` | Lima UAPI and Mesa EGL surfaceless command submission passed; the board displayed a live 6000-frame rotating/scaling/moving triangle; no recorded GPU fault/hang/timeout. | **BOARD PASS** for current Lima/Mali-400 path. |
| 2 MiB layout / USB baseline | `products_mt6572_layout2m_board_pass_20260717_031800`, `products_layout2m_baseline_restore_20260720_145924`, and `products_layout2m_baseline_boot_verify_20260720_1515` | layout2m RAM placement, simplefb, Lima and USB gadget baseline were observed; after the MT6323 Stage-B capture, the approved layout2m boot image was restored with write/readback PASS and then boot-verified over ordinary USB with fb0, render node and ttyGS0 present. | **BOARD PASS**; the device is back on the verified layout2m baseline. Legacy archive markers are historical, not a claim that the runtime pass is absent. |
| MT6323 MFD parent | `products_mt6323_mfd_layout2m_board_evidence_integrity_20260720_123800` | strict parent bind, CID low byte `0x23`, no enabled children, full flash readback and later layout2m restore are independently integrity-checked. | **BOARD PASS** for the strict-MFD parent probe; it does not itself authorize regulator state changes. |
| Wi-Fi/BT PMIC prerequisite | regulator-state module lineage (`products_mt6323_regulator_state_board_gate_preflight_20260720_124809`, `products_mt6323_regulator_state_capture_runner_20260720_131308`) plus Stage-B PASS product `products_mt6323_regulator_state_stage_b_pass_20260720_144714` | The archive-first runner used the ordinary Z1 USB Gadget Serial path, rejected `ttyUSB*`, transferred the module, loaded it with `insmod rc=0`, parsed a fresh 28-rail/4-control-row capture, observed raw CID `0x2023` / CID `0x23`, `sw_on=12`, `qi_on=12`, `writes=0`, and cleanup removed the module. C3/USB-to-TTL was disconnected. | **BOARD PASS** for the read-only MT6323 regulator-state prerequisite only. No VCN registration/write or board image is authorized by this evidence. |
| VCN18/VCN28 registration | compile product `products_mt6323_vcn_register_compile_audit_20260718_1943`; independent `products_mt6323_vcn_registration_risk_audit_20260720_125610`; Stage-B PASS `products_mt6323_regulator_state_stage_b_pass_20260720_144714`; static PMIC policy `products_mt6323_vcn_pmic_write_policy_static_audit_20260720_171154`; static capability-closed ramdisk `products_mt6323_vcn_readonly_capture_ramdisk_static_20260720_181431`; improved host candidate `products_mt6323_vcn_registration_only_readonly_ramdisk_candidate_provenance_hardened_20260720_221000`; PMIC trace plan `products_mt6323_vcn_pmic_runtime_trace_plan_20260720_190117`; minimal function-graph compile-only plan `products_mt6323_pmic_function_graph_compile_rebase_plan_repaired_20260720_235955`; rebuild mismatch audit `products_mt6323_vcn_provenance_reproducibility_audit_hardened_20260720_220500` | exact source delta, two selected rails, no selected boot/always-on constraint/consumer, read-only callbacks and regulator-core handling are statically verified. The improved 5,765,120-byte host image uses a non-interactive static ARM PID1 ramdisk; image SHA256 is `6da4604e3f31d0c07594b3140962bb9833dab25182c228ed6ccb99141d291b98`. Static ramdisk, PMIC policy, snapshot binding, the offline trace plan, and its exact compile-only configuration plan pass. Three host-only rebuild attempts reproduced the selected DTB but not the historical zImage; generated initramfs/version metadata in the retained historical output are insufficient for reproducible provenance. | **HOST PACKAGE PASS / BOARD-GATE PROMOTION NO-GO:** not copied to `out`, unflashed/unrun, and remains `BOARD_IMAGE_AUTHORIZATION=NO`, `VCN_WRITE_AUTHORIZATION=NO`. USB runtime, candidate PMIC write trace evidence, execution and audit of the function-graph compile-only rebase, and a newly captured fully deterministic clean build/package chain remain pending. |
| CONNSYS chip ID → BTIF → WLAN AHB | products `products_mt6572_connsys_chipid_compile_audit_20260718_115501`, `products_mt6572_btif_resource_audit_compile_robust_20260718_211430`, `products_mt6572_wlan_ahb_resource_audit_compile_robust_20260718_214545`, `products_mt6572_connsys_chipid_rebase_readiness_20260720_130729`, audit `build_vcn_chipid_rebase_audit_20260720`, and product `products_mt6572_vcn_chipid_rebase_compile_audit_20260720_1542` | isolated objects/DT resource contracts compile and have immutable manifests. The readiness audit proves the older chip-ID/BTIF/WLAN source lineage used pre-layout2m assumptions. The 2026-07-20 VCN+chip-ID rebase audit proves the old bounded chip-ID audit can coexist on the VCN registration source fork with the audit DT node still `status = "disabled"`; object/DTB compile passed and inherited boot artifacts were purged. | **COMPILE PASS ONLY / NOT BOARD ELIGIBLE:** after a separately authorized VCN registration-only board gate, create a separate chip-ID-only board candidate. BTIF and WLAN remain later independent rebases/gates. No external-MT6620/SDIO stack is authorized. |
| Audio | `products_mt6572_audio_register_contract_audit_robust_20260718_222817` and `products_mt6323_audio_resource_abi_gap_audit_static_20260720_233000` | vendor register-contract/source compile audit, independent review and forbidden hardware-API scan pass; the new resource/ABI audit binds vendor AFE/PMIC/clock/char-device evidence and verifies the absence of a Z1 sound graph, MT6572 ASoC driver and MT6323 codec MFD cell. | **STATIC PASS / HARDWARE NO-GO:** no DT/audio device or hardware register access was introduced. IRQ, clock/reset, DMA, endpoint topology, codec binding, safe gain/ramp sequencing and vendor ABI semantics remain unproven; runtime audio remains pending after Wi-Fi/BT priority work. |
| Touch | `products_mt6572_i2c1_controller_only_current_20260718_235041` | current-tree I2C1 controller-only image is packaged and verified; it binds no touch client. Factory identity evidence names CST836U at I2C1/`0x15`, but factory probe failed. | **PENDING HARDWARE:** controller-only boot/log gate after Wi-Fi/BT PMIC prerequisite chain; no broad I2C scan. |
| Matrix keypad | `products_mt6572_kpd_current_tree_audit_20260718_012649` | vendor 72-slot mapping and current `/9,%9` / 8×9 DT keymap agree; input device registration has board-log evidence. | **SOURCE/CONFIG COMPLETE:** physical volume switches are unpopulated, so press-event testing is unavailable and pads must not be shorted. |
| eMMC / TF / system payload | `products_mt6572_mmc_system_payload_audit_20260720_001500` | factory eMMC/system layout and current MMC resource contract are preserved/read-only audited. | **PENDING:** after the Wi-Fi/BT power and chip-ID gates, run a separate read-only MMC0 enumeration. No bus-width or TF claim is justified. Android system remains untouched. |
| Baseband | `products_mt6572_baseband_readonly_inventory_20260720_123000` | factory MD1 payload and vendor ABI dependencies are inventoried/read-only. | **EVIDENCE BLOCKED:** no authoritative Z1 CCIF/clock/reset/IRQ, memory/remap/security or firmware-load ABI proof; no modem code/runtime action is authorized. |
| Self-supervision | `z1-driver-supervisor.service`, `tools/z1_driver_supervisor_loop.sh` | service is active and checks artifacts/manifests at five-minute intervals. | **ACTIVE:** telemetry only; it cannot start hardware actions or wake an interactive assistant. |

## Ordered next steps

Completed on 2026-07-20: the strict-MFD Stage-B read-only regulator-state
capture passed, and the approved layout2m baseline was restored and booted.
Those completed steps remain evidence inputs, not authorization to proceed on
hardware automatically.

1. Keep the device on the restored/verified layout2m baseline unless a later
   board image receives explicit, separate authorization.
2. The VCN18/VCN28 registration-only host candidate now has a static
   capability-minimized capture ramdisk and passes host packaging validation,
   but its board-gate promotion is **NO-GO**. The completed rebuild audit
   disproved reproducibility of the retained historical zImage, so do not
   treat the old source/config pair as a promotable clean-build lineage.
   Complete three offline closures first: the PMIC runtime trace plan's
   function-graph-enabled compile-only rebase plus runtime trace
   instrumentation, USB runtime validation of the capture ramdisk, and a new
   fully captured deterministic clean-build/package chain. Until an explicit
   later authorization exists: `BOARD_IMAGE_AUTHORIZATION=NO` and
   `VCN_WRITE_AUTHORIZATION=NO`.
3. Only after an explicitly authorized VCN registration-only board gate passes,
   design a separate chip-ID-only board candidate.  The current VCN+chip-ID
   rebase product remains disabled-DT compile evidence and must not be flashed.
4. Continue separate BTIF, WLAN AHB, audio, touch, MMC and baseband gates
   without combining them.

The full objective cannot be marked complete from the current evidence: the
VCN registration board gate, chip-ID board gate, Wi-Fi/BT runtime chain,
runtime audio/touch/MMC gates and the required baseband hardware ABI evidence
remain unresolved.
