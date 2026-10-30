# Tasks

- [ ] 1.1 Host-side outcome detection: all Builders dead, 45-minute timer, 7000 towers.
- [ ] 1.2 Collapse a dead Builder's towers, sites and foundations.
- [ ] 1.3 Replicate the outcome; stop accepting orders.
  - [x] 1.3a Terminal result wire format and snapshot/delta/recovery
    invariants implemented in the replication OpenSpec (protocol v17).
    Source is committed; build/test evidence remains pending.
  - [ ] 1.3b Produce the result from the authoritative simulation, stop
    orders, and verify recipient presentation after reconnect.
- [ ] 1.4 Timer in the HUD; Hunter wins / Builders win end screens.
- [x] 1.5 Decide disconnect and host migration: timer pauses; disconnected counts as dead; towers stay.
- [ ] 1.6 Pause the timer during authority succession; count disconnected Builders as dead.
- [ ] 1.7 Tests for each condition and for the collapse; multiplayer playtest.
