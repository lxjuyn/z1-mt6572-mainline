# MT6572 CONNSYS chip-ID rebase-readiness audit — 2026-07-20

Status: **STATIC REBASE PLAN PASS / NO IMAGE / NO BOARD AUTHORIZATION**

The older chip-ID compile audit is useful source evidence, but its source fork
predates the accepted 2 MiB (`layout2m`) baseline and the VCN-only regulator
work.  It must **not** be packaged or flashed as-is.

This audit proves the reason and the allowed future integration boundary:

1. The old chip-ID source uses the pre-layout RAM declaration at `0x80000000`
   and lacks `regulator_ignore_unused`; the current VCN source uses
   `0x80200000` and retains that boot argument.
2. The old chip-ID configuration does not contain the MFD/MT6323 regulator
   enablement required by a VCN consumer, while the VCN audit configuration
   does.
3. The current VCN source already has the disabled/default-off SoC CONN power
   domain and its required PM/genpd/SCPSYS configuration.  The old chip-ID
   DTSI node must be re-applied only to a new VCN-derived source fork, and
   must remain disabled until its own board gate.
4. The old audit driver deliberately calls regulator enable/disable and
   runtime-PM domain resume.  These are *intentional hardware actions* for the
   later chip-ID gate, not permitted in the preceding read-only PMIC-state or
   VCN-registration gates.

After a clean regulator-state capture **and** a separately successful VCN
registration board gate, create a new source fork from the exact VCN candidate,
reapply/review the chip-ID source and disabled DT node, compile it, produce a
new hash-bound boot candidate and test it independently.  BTIF/WLAN must not
be combined with that candidate.
