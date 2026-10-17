# Stage-B transport prefix fix — 2026-07-20

The first normal-USB Stage-B run passed all runtime identity checks
(`7.0.0-rc7`, framebuffer, Lima render node, gadget serial, PWRAP and
`mt6397`) but stopped at the deliberately silent transfer-reset command. No
module transfer and no `insmod` had been attempted.

The remote shell prints its `~ # ` prompt without a trailing newline. The old
host helper sent a silent command followed by `echo MARKER`; the resulting
`~ # MARKER` line correctly failed the helper's standalone-marker parser.

The repaired helper prefixes every command with `echo;`, which produces a
newline before the marker. Two direct serial tests were run over the ordinary
Z1 Gadget Serial path:

1. a shell-only marker diagnosis, confirming no audit module is present;
2. the repaired helper calling `busybox true`, confirming `wait_line()`
   receives an unambiguous standalone completion marker.

Neither test maps PMIC registers, changes a regulator, transfers a module or
calls `insmod`. The next run must repeat the entire audited capture and must
still require the normal `writes=0`/28-rail/cleanup acceptance criteria.
