# Tasks

- [x] 1.1 Decide capacity (1), destroyed tower (eject unharmed), building inside (no), tower stats (unchanged).
- [ ] 1.2 Garrison state in the simulation; hide from vision, targeting and collision. Initial authoritative arrival/ejection, no-target/no-damage, vision and snapshot filtering implemented; compile/visual verification still required.
- [ ] 1.3 G mode in the controller; GARRISON command. Wire command kinds, strict ID/target validation and roundtrip tests implemented (2026-10-31); interactive mode and simulation handler still pending.
- [ ] 1.4 Optional per-tower ungarrison point with the rally point's red arrow (reuse rally point code); SET_UNGARRISON_POINT.
- [ ] 1.5 U shortcut and UNGARRISON command: step straight out without a point, walk to it with one; adjacent-tower hop.
- [ ] 1.6 Replication: owner-only garrison state; Hunter sees nothing.
- [ ] 1.7 Tests: garrison, invalid and occupied towers, untargetable inside, no building inside, ungarrison point, hop, eject on destruction.
- [ ] 1.8 Playtest solo and multiplayer.
