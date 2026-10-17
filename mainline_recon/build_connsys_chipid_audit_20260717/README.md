# MT6572 CONNSYS chip-ID compile audit

Scope: isolated source/object/DT validation only.

The isolated source adds a default-off platform driver and disabled Z1 DT
node. The probe sequence enables VCN18 and VCN28, resumes the CONN genpd,
reads only the chip ID at `0x18070008`, then synchronously releases runtime PM
and disables both rails.

The driver has no IRQ, clock, reset, firmware, shared-EMI, DMA or WLAN/BTIF
write path.

Results:

- W=1 source/object build passed without source diagnostics.
- Object SHA256:
  `17dd95cb2121a7d09d53f2522f50392fd66e9c943d70a2342621191f6f96b81f`.
- Z1 DTB SHA256:
  `304bb9b577ba9c9cbe6bada70d05c7243ea9985ae93cb82d855518dd5cf7ba45`.
- The audit DT node is disabled.

The direct target make logs record make-goal experiments; the corrected source
object and full `dtbs` builds passed. Global DT builds can report unrelated
warnings from other boards. The target Z1 DTB and decoded audit node are the
relevant outputs.

No zImage or boot image was produced. Runtime PM/genpd and regulator behavior
remain untested on hardware. Do not enable or package this driver until the
strict MFD-only and separate regulator-registration gates pass.
