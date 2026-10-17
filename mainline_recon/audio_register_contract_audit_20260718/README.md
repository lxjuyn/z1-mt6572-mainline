# MT6572/MT6323 audio register-contract audit

Status: **OFFLINE HEADER/COMPILER AUDIT ONLY**

This audit normalizes a small, representative contract from the vendor
MT6572 AFE and MT6323 analog/digital audio headers and checks it with:

- exact vendor-source SHA256 inputs;
- source parsing of every selected register value;
- a freestanding ARM C compile with `-Wall -Wextra -Werror`;
- static assertions for the AFE mapping, SRAM and PMIC ranges;
- a forbidden hardware-API scan.

It establishes:

- For the explicitly selected non-FPGA configuration
  (`CONFIG_MT6572_FPGA_CA7=n` and the vendor `AUDIO_MEM_IOREMAP` /
  `AUDIO_MEMORY_SRAM` definitions), MT6572 AFE physical base `0x11140000`;
- for that same configuration, the vendor mapping window `0x10000`;
- for that same configuration, internal SRAM at `0x11141000`, size `0x4000`;
- selected AFE offsets through `0x0570`;
- selected MT6323 PMIC digital and analog audio offsets.

The vendor source also contains an FPGA `AUDIO_BASE` branch. This audit
parses and validates only the non-FPGA branch named above; it must not be
treated as proof for an FPGA build or for a build without
`AUDIO_MEM_IOREMAP`.

`TARGET_CONFIGURATION.txt` is a declared selector for this isolated contract
audit, not the `.config` of a bootable kernel. Therefore this result does not
prove that any kernel image uses the same branch; a future audio implementation
must bind this contract to its own hashed build configuration and provenance.

It does not add a Device Tree node, ASoC component, codec, clock, IRQ, PMIC
write path or boot image. Audio routing, IRQ, clocks/resets and the actual Z1
speaker/microphone topology remain separate blockers.
