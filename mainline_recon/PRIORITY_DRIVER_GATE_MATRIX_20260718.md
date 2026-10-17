# Z1 priority driver gate matrix - 2026-07-18

## 1. Wi-Fi / Bluetooth

Status: architecture/resources proven; strict MT6323 MFD-parent board gate
passed and baseline restore is verified; chip-ID-only source compiles.

The MFD evidence integrity archive is
`products_mt6323_mfd_layout2m_board_evidence_integrity_20260720_123800`.
Its historical bundle has one invalid self-hash line, but all eight payload
files independently verify; the MFD pass is therefore usable evidence. C3 UART
was inconclusive, so the normal USB-gadget capture is the accepted path.

Next gate: first capture the current PMIC rail state through the separately
audited, strictly read-only module procedure in
`MT6323_REGULATOR_STATE_BOARD_GATE_20260720.md`; only then package and
independently review a VCN18/VCN28-registration-only board candidate.
The independent static review
`products_mt6323_vcn_registration_risk_audit_20260720_125610` additionally
pins the VCN source delta and the no-write callback/DT constraints, but does
not substitute for the required hardware state capture.
Regulator registration, CONNSYS chip ID, BTIF, WLAN AHB,
GDMA and firmware remain separate gates.

Do not use the external MT6620 SDIO branch.

## 2. Audio

Status: not boot-ready.

The MT6323 DTS exposes an `mediatek,mt6397-codec` MFD child, but the mainline
tree has no MT6572 AFE platform driver and no verified MT6323 audio codec
implementation. The vendor 3.4 audio stack uses raw PWRAP access, raw AFE
register programming and removed clock-manager APIs, so it is not a direct
port.

Next offline work: extract the exact MT6572 AFE register map, clock/reset
sequence, PMIC analog register semantics and Z1 machine routing. No audio image
should be built before those contracts are explicit.

## 3. Touch

Status: current-tree I2C-controller-only candidate compiled, structurally
validated and archived; it remains unflashed. The factory kernel directly
contains the `hyn_ts` driver ID
`HYN_CST836U_DWZN_MT6572_4.4_V1.0_20181106`, and the stock log attempts it at
I2C1 address `0x15`, but that probe fails. This identifies the factory driver
target, not a successfully responding physical controller.

Candidate:
`products_mt6572_i2c1_controller_only_current_20260718_235041`, boot image
SHA256
`be336ce8bbc62ac72f248594648c53559a7aa2e22bf8c42879af11af73c9ddfe`.

Next gate: boot this image with `I2C_MT65XX=y` and every I2C client disabled,
then capture a complete log proving adapter registration and absence of I2C
timeout, DMA, clock and IRQ faults. The vendor log only shows a failed
`hyn_ts` probe at address `0x15`; the mismatched GT911 reference node at
`0x5d` remains disabled. After the adapter-only gate, use only a
protocol-justified read at `0x15`, not a broad address scan.

## 4. Keys

Status: existing driver and keymap are evidence-complete; no change is needed.

The archived audit
`products_mt6572_kpd_current_tree_audit_20260718_012649` proves that the
vendor runtime keymap has slot 0 = `KEY_VOLUMEUP` and slot 9 =
`KEY_VOLUMEDOWN`; the current driver calculates `row = slot / 9` and
`column = slot % 9`, while the current DTB supplies matching 8x9 entries.
The vendor boot/download-only `KPD_DL_KEY3 = 8` macro is not a runtime Linux
keymap entry.

The board boot log proves `mt6779-keypad` registered as an input device. The
device has no populated volume switches, so a physical press/release scan
event cannot be tested on this unit. Do not short the pads without schematic
or continuity evidence. PMIC keys remain behind the strict MFD gate.

Next gate: none for the existing matrix keypad. Only a separately proven
switch/pad modification could justify a targeted event capture for slots 0 and
9.

## 5. TF / MMC

Status: the current resource contract and factory partition layout are audited;
Linux enumeration remains unproven.

The archived read-only audit
`products_mt6572_mmc_system_payload_audit_20260720_001500` pins the factory
scatter and `system.img`: MT6572 uses eMMC with `MSDC_0` boot channel, a 6 MiB
boot partition, and a 900 MiB system partition. The preserved factory
`system.img` SHA256 is
`0aad817b3f87e7b328378c0b147da1ea048a87f9aa2ecfa338178be7176c62f7`;
it has not been mounted or modified by this work.

`mmc0` is the non-removable eMMC controller at `0x11120000`; the current DTS
declares a one-bit bus despite configuring clock, command, data0..data7, and
reset pins. It depends on MT6323 VEMC3V3 and VIO18, while the current candidate
configuration keeps `MFD_MT6397` disabled. This is an unresolved resource
contract, not authorization to change bus width.

`mmc1` remains disabled and there is no Z1-specific TF slot, card-detect or
rail evidence. Do not equate `mmc1` with Wi-Fi or claim TF support yet.

Next gate: only after PMIC/MFD rail behavior is proven, package a separate
read-only MMC0 enumeration gate with `mmc1` still disabled. Capture regulators,
clock, pinctrl, IRQ, eMMC identification and partition log. Do not change bus
width until exact Z1 wiring is proven. TF/microSD needs independent board
connector/card-detect/rail evidence.

## 6. Baseband

Status: read-only factory/vendor/current-tree inventory complete; no mainline
MT6572 CCCI/CCIF contract or Z1 DT node exists.

The archived inventory
`products_mt6572_baseband_readonly_inventory_20260720_123000` proves that the
factory system image contains `/etc/firmware/modem_1_2g_n.img` (2,399,148 B,
SHA256 `5264c9c2b567189bedd49c3ff03ebe13664c2e8e09349db8f6d3d9483b1ee4f6`).
The scatter has no standalone modem partition, so the image payload is a fact,
but it does not prove a portable load ABI or safe runtime sequence.

Vendor configuration enables MD1 and requires a CCCI stack, CCIF, modem/DSP
memory, 2 MiB-aligned shared memory, AP/MD remap, EMI/MAU/MPU programming,
firmware handling and multiple user-space services. Vendor numeric addresses
are reference ABI evidence only and must not be copied into mainline DTS. The
current Z1 DTS/DTB/config contains no CCCI/CCIF/modem node or reservation.

Next work: blocked pending authoritative hardware documentation or independently
reproducible factory boot-memory evidence proving CCIF/IRQ/clock/reset, all
MD/DSP/shared-memory regions, remap and security policy, firmware format and
PMIC sequencing. Do not start/reset the modem, program remap/MPU registers,
load firmware, or create a baseband boot candidate before that evidence and an
independent review exist.
