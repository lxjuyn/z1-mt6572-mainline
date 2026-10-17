# VCN registration-only capability-closed capture ramdisk — host prototype

Status: **STATIC RAMDISK PASS / USB-RUNTIME UNTESTED / NOT A BOARD IMAGE**

This is a future replacement for the open diagnostic ramdisk used by the
host-only VCN candidate.  It is intentionally a standalone host artifact: it
is not copied into `out/`, is not packed into a boot image, and has no flash
launcher.

## Design boundary

- PID 1 is a dedicated static ARM binary, not a shell script.
- There is no `getty`, `/bin/sh`, applet symlink, module loader, `devmem`,
  `nandwrite`, flash utility, or user command channel in the packed CPIO tree.
- The only retained BusyBox binary is mode `0700`; PID 1 executes exactly
  `busybox mdev -s` to populate `/dev`, with fixed argv and no interactive
  process.
- PID 1 mounts proc/sysfs/tmpfs, configures the known Android USB gadget ACM
  attributes directly through sysfs, emits bounded static diagnostics to
  `/dev/ttyGS0` when it appears, and then sleeps as non-interactive PID 1.

The BusyBox executable still contains compiled applets internally; capability
closure here is an entry-point policy, not a claim that the binary was rebuilt
with those applets removed.  A future independent review must accept this
fixed-argv/mode-0700 boundary or require a separately built minimal BusyBox.

## Required next validation

Before this ramdisk can replace the current candidate's ramdisk, complete:

1. host packaging + boot-size validation with the exact VCN zImage/DTB;
2. independent review of the fixed-argv BusyBox exception;
3. a separate board authorization and runtime USB ACM/log-capture validation;
4. candidate-runtime PMIC write instrumentation evidence.

None of these conditions is currently authorized.
