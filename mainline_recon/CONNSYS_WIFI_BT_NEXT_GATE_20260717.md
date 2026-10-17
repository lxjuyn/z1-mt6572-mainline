# MT6572 CONNSYS Wi-Fi/BT next gate

## Current conclusion

Z1 uses the MT6572 integrated CONNSYS path:

- SOC WMT control;
- BTIF transport;
- WLAN AHB HIF;
- AP GDMA only after the non-DMA HIF gate.

The external MT6620 SDIO/STP/GPIO stack remains quarantined. It is not a
candidate for Z1.

## Hard prerequisite

The strict MT6323 MFD-only image must pass on the board before any VCN rail or
CONNSYS consumer is enabled. The old-layout image below is INVALID and must
not be reused:

`out/boot_mainline_mt6572_lima_simplefb_mfd_only_strict.img`

SHA256:

`8c53ccf62029a4813ab897b6bc88a87717a3a94aca9e576ff794588ad79d1c85`

It proved the PWRAP/MFD binding and CID low byte `0x23`, but it did not retain
the accepted layout2m memory baseline. The required replacement is:

`out/boot_mainline_mt6572_lima_simplefb_layout2m_mfd_only_strict.img`

SHA256:

`e1439d7474aa49eab88b820519cff8753d8ccc174398b6a099bdaf83253a0871`

The replacement is built, manifest-verified, independently re-audited and
marked **OFFLINE BOARD-CANDIDATE GO**. It still must pass the exact controlled
board gate before any VCN rail is enabled.

Product archive:

`bridge_backups/products/products_mt6572_layout2m_mfd_only_strict_candidate_20260718_164718/`

The current `mt6323-regulator.c` registers the complete regulator table and
reads PMIC state during probe. Existing Kconfig/DTS cannot instantiate only
VCN18 and VCN28. A regulator-enabled image therefore has a larger PMIC blast
radius and must be designed and reviewed as a separate gate.

The unmodified full driver is now explicitly NO-GO as the next image.
Regulator-core DT constraint application can clamp voltages and enable
boot/always-on rails during registration, then disable unused non-always-on
rails after 30 seconds.

A no-flash, read-only 28-rail state module is prepared and statically
validated:

`mainline_recon/mt6323_regulator_state_audit_20260718/`

Module SHA256:

`9d6b7db8dee88e62872f73942abbe6e7057d2d649e3eaf21ca2d15e6e76be680`

It must run only after the exact MFD-only board gate passes. Its output will
drive the VCN18/VCN28-only registration design.

## Chip-ID compile audit

An isolated default-off driver was added only in:

`mainline_recon/fork_linux_connsys_chipid_audit_src_20260717/`

It performs this bounded sequence:

1. Enable VCN18.
2. Enable VCN28.
3. Resume the CONN genpd through runtime PM.
4. Map `0x18070000..0x18070fff`.
5. Read the chip ID at offset `0x8`.
6. Accept only `0x6572` or `0x6582`.
7. Synchronously suspend the domain and disable both rails in reverse order.

It does not request IRQs, clocks or reset lines; it does not map shared EMI,
load firmware, submit DMA or write WLAN/BTIF registers.

Compile results:

- W=1 source/object build: PASS, no source diagnostics.
- Object SHA256:
  `17dd95cb2121a7d09d53f2522f50392fd66e9c943d70a2342621191f6f96b81f`.
- Z1 DTB SHA256:
  `304bb9b577ba9c9cbe6bada70d05c7243ea9985ae93cb82d855518dd5cf7ba45`.
- DT node is `status = "disabled"`.
- No zImage or boot image was produced.

Runtime-PM/genpd attachment and PMIC rail behavior remain untested. This audit
is source evidence, not a board candidate.

## Ordered board gates

1. Corrected layout2m + strict MT6323 MFD-only board gate.
2. Read-only 28-rail snapshot on that same image; no new flash.
3. VCN18/VCN28 registration-only gate with every other rail disabled and
   unused-rail cleanup blocked.
4. Chip-ID-only CONNSYS gate.
5. BTIF transport and IRQ 122.
6. WLAN AHB HIF at `0x180f0000`, IRQ 123, without GDMA.
7. AP GDMA at `0x11000100`.
8. Firmware/shared-EMI ABI and WLAN functional testing.

Each item requires its own image, full boot-partition readback and rollback
decision. Do not combine adjacent gates.
