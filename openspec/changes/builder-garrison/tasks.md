# Tasks

- [x] 1.1 Decide capacity (1), destroyed tower (eject unharmed), building inside (no), tower stats (unchanged).
- [ ] 1.2 Garrison state in the simulation; hide from vision, targeting and collision. Initial authoritative arrival/ejection, no-target/no-damage, vision and snapshot filtering implemented; compile/visual verification still required.
- [ ] 1.3 G mode in the controller; GARRISON command. Controller G-click and U hotkeys, authenticated routing, validated authoritative entry/exit and wire tests implemented; build and live testing required.
- [ ] 1.4 Optional per-tower ungarrison point with the rally point's red arrow (reuse rally point code); SET_UNGARRISON_POINT.
- [ ] 1.5 U shortcut and UNGARRISON command: deterministic free-tile ejection implemented for the selected occupied tower; points and adjacent-tower hopping remain.
- [ ] 1.6 Replication: owner-only garrison state; Hunter sees nothing.
- [ ] 1.7 Tests: garrison, invalid and occupied towers, untargetable inside, no building inside, ungarrison point, hop, eject on destruction.
- [ ] 1.8 Playtest solo and multiplayer.
