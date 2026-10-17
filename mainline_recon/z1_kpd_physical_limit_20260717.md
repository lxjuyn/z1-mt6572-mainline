# Z1 keypad physical validation note — 2026-07-17

- Board owner confirmed this Z1 unit does not have volume-up or volume-down switches populated.
- PCB pads are present, but the switches themselves are not soldered.
- Therefore no physical press/release event can be generated for the two matrix-key slots on this unit in its current state.
- Linux KPD probe and `/dev/input/event0` existence were already observed; scan-code validation remains electrically untested.
- Do not short unidentified pads. Only attempt pad injection after the pad pair, active level, pull configuration, and ground/reference have been proven from schematic/layout or continuity measurements.
- The 180-second event capture was cancelled after learning of this hardware limitation.
