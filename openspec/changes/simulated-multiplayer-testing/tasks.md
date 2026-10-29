# Tasks — staged simulated multiplayer test infrastructure

## Stage A: first useful five-client measurement tool
- [x] A1 Add a 5-client opt-in test using real relay room and authenticated
  `RelayLobbyClient` workers, without starting Qt rendering.
- [x] A2 Time scripted command-result round trips and recipient-specific world
  update delivery; report overall and per-guest p50/p95/max.
- [x] A3 Add a Python stdlib TCP byte proxy per guest for RTT/jitter,
  approximate bandwidth shaping, a selected guest stall and JSON reports.
- [x] A4 Support configurable synthetic arrow-record loads, and document that
  no actual Hunter combat is being simulated.
- [ ] A5 Compile/run on Windows and Linux; run baseline and fixed-delay
  sanity checks; verify observed RTTs match the injected profiles within
  documented host scheduling tolerance. Tests have not run here.
- [ ] A6 Repeat 5-client tests with one 3s stalled guest; verify healthy
  client latency does not deteriorate excessively, or file a game fix.
- [ ] A7 Ensure benchmark relay shutdown, socket proxy cleanup and reruns
  succeed in loops with no stale sessions or flaky timeout tests.

## Stage B: actual simulation workload
- [ ] B1 Run deterministic asset-backed Builder construction queues,
  Hunter spawns/movement/attacks and real arrow fire with real network workers.
- [ ] B2 Instrument measured projectile rate, capture serialization time,
  actual bytes/recipient/sec, CPU and memory; sweep 2/5/8 players and
  light/medium/extreme tower loads.
- [ ] B3 Assert state parity, command ownership, death/HP/kill agreement,
  fog privacy, and snapshot recovery under shaped links.
- [ ] B4 Rework sequential recipient sends if a single stalled TCP client
  measurably delays healthy players; add regression for fairness.

## Stage C: visual and failure acceptance
- [ ] C1 Measure real command-to-visible-result and animation consistency in
  automated graphical clients, with frame timing and screenshots/logs.
- [ ] C2 Add OS-level packet loss/retransmission tests where available,
  disconnect/reconnect, Hunter authority succession and aborted handoff.
- [ ] C3 Collect CI artifacts and compare measurements across Windows/Linux;
  compare predictions to a small number of international human playtests.

Implementation checkmarks do not mean the harness passed or that the full
`complete-gameplay-replication` acceptance spec is complete.
