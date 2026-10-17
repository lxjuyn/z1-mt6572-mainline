# MT6323 MFD-only strict board result — layout2m candidate

Date: 2026-07-18  
Target: `boot_mainline_mt6572_lima_simplefb_layout2m_mfd_only_strict.img`  
Target SHA256:
`e1439d7474aa49eab88b820519cff8753d8ccc174398b6a099bdaf83253a0871`

## Result

**MT6323_MFD_ONLY_STRICT_BOARD_GATE=PASS**

The candidate was written to `bootimg` on the Z1 and read back successfully.
The read-back prefix of length 5,564,416 bytes matched the target exactly.
The board then booted to the Linux framebuffer console and enumerated the
normal USB gadget serial endpoint.

The complete evidence bundle is:

`mainline_recon/board_gate_evidence_mt6572_mfd_only_20260718_1930/`

Evidence bundle SHA256SUMS:

`6112a77ce5869a7d125a9fa160591ecf79e49ae9696663e050d0fcef9b2ab9b6`

## Runtime evidence

- Kernel: `7.0.0-rc7`
- RAM: `80200000-bf3fffff : System RAM`
- `/dev/fb0`: present
- `/dev/dri/renderD128`: present
- `/dev/ttyGS0`: present
- UDC `musb-hdrc.3.auto`: present
- PWRAP driver: `mt-pmic-pwrap`
- MT6323 parent driver: `mt6397`
- MT6323 child platform devices: none
- PMIC/PWRAP IRQ counters: `0 -> 0` over three seconds
- Generic kernel fault scan: empty
- PMIC/PWRAP/Lima/MUSB error scan: empty
- CID module transfer SHA256:
  `cc7aea0e899f6a087ab97c8f9d784a1e3aa964269c9458fb230fe5b006c114a0`
- CID read: `raw=0x2023`, `cid=0x23`
- CID module unload: `RMMOD_RC=0`
- CID module absent from `/proc/modules` after unload

The ordinary USB shell input path required byte-paced command typing. Fast
multi-kilobyte shell writes overran the small getty input path and caused two
collection attempts to time out. The slow helper was then used successfully;
this is a host-side collection constraint, not a kernel fault.

## C3 UART limitation

The C3 UART capture did not produce valid UART data. The captured stream was
effectively all `0x00`. The C3 was found to have been wired incorrectly and
was disconnected before continuing. This does not invalidate the PMIC/MFD
runtime evidence collected over the Z1's normal USB gadget, but the C3 UART
log itself is marked inconclusive.

## Safety and end state

This gate tested only the MFD parent. It did not enable regulators, CONNSYS,
audio, touch, keys, TF/MMC, baseband, suspend, or charging.

The board must now be restored to the approved layout2m baseline before
continuing with the regulator/CONNSYS gates:

`out/boot_mainline_mt6572_lima_simplefb_layout2m.img`

The restore operation must use the factory image
`1/boot.img` as its rollback image and must complete a full read-back check.
