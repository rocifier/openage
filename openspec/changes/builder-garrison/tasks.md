# Tasks

- [x] 1.1 Decide capacity (1), destroyed tower (eject unharmed), building inside (no), tower stats (unchanged).
- [ ] 1.2 Garrison state in the simulation; hide from vision, targeting and collision. Initial authoritative arrival/ejection, no-target/no-damage, vision and snapshot filtering implemented; compile/visual verification still required.
- [ ] 1.3 G mode in the controller; GARRISON command. Controller G-click and U hotkeys, authenticated routing, validated authoritative entry/exit and wire tests implemented; build and live testing required.
- [ ] 1.4 Optional per-tower ungarrison point with the rally point's red arrow (reuse rally point code); SET_UNGARRISON_POINT. Authoritative state, private marker and right-click command implemented; compile and manual acceptance pending.
- [ ] 1.5 U shortcut and UNGARRISON command: deterministic free-tile ejection, closest-reachable point movement and atomic adjacent-tower hopping implemented; compile and playtest pending.
- [ ] 1.6 Replication: owner-only garrison state; Hunter sees nothing. Concealed Builder absent in world snapshots, with private per-tower occupant field, private markers and owner HUD label; compile/network recovery testing pending.
- [ ] 1.7 Tests: garrison, invalid and occupied towers, untargetable inside, no building inside, ungarrison point, hop, eject on destruction. Added ownership, recipient privacy, marker, protocol roundtrip and delta tests; hop and broader stress tests plus actual execution remain.
- [ ] 1.8 Playtest solo and multiplayer.
