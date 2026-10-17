# MT6323 layout2m MFD board-evidence integrity audit — 2026-07-20

## Purpose

The layout2m strict-MFD board evidence from 2026-07-18 is substantive, but
its original `SHA256SUMS` mistakenly hashes `./SHA256SUMS` as the empty-file
digest. A manifest cannot validly hash itself this way, so `sha256sum -c`
must fail for that single line.

This audit leaves the historical bundle untouched. It verifies every payload
file in that bundle against the historical listed digest, records the one
self-reference defect, and emits a non-self-referential source-content
manifest. It also validates the flash, runtime, CID and baseline-restore facts
that are relevant to deciding whether the MFD parent board gate passed.

## Result boundary

`VALIDATION=PASS` means:

- all nine historical evidence payload files match their recorded hashes;
- the only old-manifest failure is its impossible self-hash line;
- the logs prove the strict layout2m MFD parent gate and subsequent baseline
  restore as described in the result document.

It does not make the historical C3 UART capture usable: that capture is
explicitly inconclusive. It does not prove regulator registration, CONNSYS,
audio, touch, MMC or baseband.

## Reproduction

```sh
python3 extract_and_validate.py
(cd .. && sha256sum -c mfd_layout2m_evidence_integrity_audit_20260720/SOURCE_BUNDLE_PAYLOAD_SHA256SUMS)
sha256sum -c OUTPUT_SHA256SUMS
```
