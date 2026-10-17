# Independent board-gate plan — not yet authorized

This document describes the review needed before any flash command may be
started.  It is not a flash instruction and does not authorize hardware work.

## Promotion prerequisites

1. Re-run `validate_vcn_registration_only_candidate.py` and require every
   check plus `HOST_PACKAGE_RESULT=PASS`.
2. Verify the product archive manifest and bind the candidate, board-proven
   baseline, and factory rollback SHA256 values from `PACKAGE_PROVENANCE.txt`.
3. Independently confirm the VCN audit regulator operations expose no
   enable/disable/set callbacks and the device tree has no VCN consumers.
4. Prepare a flash/readback script with a long MTK-port wait, exact partition
   size check, automatic readback SHA256, and automatic board-proven-baseline
   rollback on any failure.
5. Only after the script is already waiting may the user be asked to power
   down, disconnect C3/ordinary USB, and attach the MTK download cable.

## First-board success criteria

- Kernel reaches the existing Linux/simplefb/USB diagnostic baseline.
- MT6323 MFD probes and only VCN18/VCN28 registration-audit rails are added.
- No regulator enable/disable/voltage/mode write occurs.
- VCN18 and VCN28 remain off unless a later separately authorized gate is run.
- No CONNSYS chip-ID, BTIF, WLAN HIF, GDMA, firmware, or EMI action occurs.
- USB Gadget Serial and framebuffer remain functional.
- Full serial/dmesg evidence and flashed-partition readback are archived.

## Failure action

On boot failure, unexpected PMIC write, rail transition, probe storm, or USB
regression, stop the gate and restore the exact board-proven baseline.  Do not
advance to chip-ID or any connectivity power-on step.
