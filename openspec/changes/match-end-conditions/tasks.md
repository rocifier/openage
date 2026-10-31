# Tasks

- [x] 1.1 Host-side outcome detection: all Builders dead or the 45-minute simulation-clock timer expires with Builders alive. Source and boundary tests committed; build verification pending.
- [x] 1.2 Collapse a killed Builder's towers, sites and foundations; disconnect-only players keep structures. Source committed; manual death cleanup verification pending.
- [ ] 1.3 Replicate the outcome; stop accepting orders and autonomous combat. Code is present on `FVD`; the two-player victory-condition gameplay check passes (user-reported 2026-10-31). Post-result order/combat checks, recovery, 4/8 players and cross-platform verification remain.
  - [x] 1.3a Terminal result wire format and snapshot/delta/recovery
    invariants implemented in the replication OpenSpec (protocol v17).
    Source is committed; build/test evidence remains pending.
  - [ ] 1.3b Produce the result from the authoritative simulation, stop
    orders, and verify recipient presentation after reconnect.
- [x] 1.4 Timer in the HUD; Hunter wins / Builders win end screens. The HUD
  switches from the Hunter arrival banner to the role-specific synchronized
  match timer; two-player victory-condition gameplay check passes
  (user-reported 2026-10-31). Full role-specific UI, recovered end-screen and
  4/8-player acceptance remain pending.
- [x] 1.5 Decide disconnect and host migration: timer pauses; disconnected counts as dead; towers stay.
- [ ] 1.6 Pause the timer during authority succession; count disconnected Builders as dead.
- [ ] 1.7 Tests for each condition and for the collapse; multiplayer playtest.
  - [x] Two-player victory-condition gameplay smoke check passes
    (user-reported, 2026-10-31).
  - [ ] Cover all winner boundaries, killed-Builder collapse, disconnect,
    rehost timing, postgame commands and 4/8-player live matches.
