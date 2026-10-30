# Complete gameplay replication checklist

- [x] 1.0 Audit current wire state and distinguish global world state from
  recipient-private scores; note that constructions, reservations, orders,
  entity health, deaths and timed arrows already use snapshots/deltas.
- [x] 1.1 Serialize bounded, time-ordered future position keys from the
  authoritative Position curve, deliver through full snapshots and deltas,
  and use them for guest render playback. Bump protocol major to reject
  incompatible older builds. **Windows automation and real two-player
  playback pass; Linux remains follow-up.**
- [x] 1.2 Add wire roundtrip, rejection, changed-curve delta and
  timed-key expiration regressions. **Windows test pass.**
- [x] 1.3a Reduce per-entity update traffic: compare each new position
  against the previously transmitted authoritative movement curve,
  suppress an unchanged moving entity, and advance the guest's position
  locally using that same curve. Genuine corrections, health changes and
  rerouted paths still transmit. Regression simulates 512 horses across
  2/4/8-player rosters and asserts compact deltas, exact positions and
  correction transmission. Future-key capture now starts at the first
  future key rather than rescanning old curve history. **Windows test pass.**
- [x] 1.3b Verify wall-clock performance and presentation smoothness in an
  actual two-player match. **User acceptance, 2026-10-29:** two-player
  performance was excellent. Four/eight-player load, dense-combat metrics
  and artificial-latency measurements move to alpha-scale follow-up.
- [ ] 1.4 Verify synchronized entity orientation and appropriate sprite
  animation changes/phase using scheduled angle/animation curves, where
  animations exist; remove any guest-only guessed behavior.
  - [x] 1.4a Capture authoritative sampled facing and bounded future
    instantaneous turn keys from the simulation's segmented angle curves.
    Carry them through snapshots, motion-suppressed deltas and guest
    RenderEntity sprite direction. Add wire order/rejection, timed expiry
    and retask correction regressions; protocol v11 rejects older peers.
    **Windows automation passes; Linux and 4/8-client visual load remain.**
  - [x] 1.4b Wire Idle/Move animation identity and authoritative start
    timestamp through host snapshots and guest deltas, including sprite
    selection and consistent loop phase after recovery. Demo asset set uses
    existing 16-angle Orc idle/walk spritesheets as a temporary Builder.
    **Windows automation and two-player attack/building presentation pass;
    death presentation and Linux remain.**
  - [ ] 1.4c Validate frame-by-frame parity, proper direction mapping and
    animation/resource budget on real hardware.
    **User acceptance, 2026-10-29:** attack and building animations pass in
    a real two-player match; death animations fail and keep this item open.
    Four/eight-player animation load remains alpha-scale follow-up.
    Additional automated regression: consecutive ATTACK volleys with an
    unchanged action ID must carry their new start timestamp through a
    delta and preserve it after recovery. **Windows test pass.**
- [x] 1.5a Retain uniquely identified arrow launches for a bounded
  3-second post-impact history; deliver them in normal recipient deltas
  even when the 0.2-second flight ends between captures. Guests dedupe
  IDs and replay a late-received visual locally without applying damage.
  Regression covers missed-between-captures, no duplicate event and
  expiry; the subsequent 1.5b batched-delivery work permits old launch
  records in recipient deltas for late replay. **Windows test pass.**
- [ ] 1.5b Reliably deliver timed projectile events over the existing
  ordered TCP stream when more than 2048 launches occur between recipient
  captures, and verify host-authoritative damage, hit/death visuals,
  recovery and multiplayer latency. Use per-recipient sent cursors and
  bounded batches; **do not add application-level projectile ACKs**.
  TCP handles retransmission and in-order delivery within a live
  connection. On disconnection, recover authoritative world state via
  snapshot rather than replaying expired cosmetic shots. Do not mark
  1.5 complete until the stress tests and cross-client visual
  verification pass.
  - [x] Schedule a Hunter projectile at attack start with the same
    authoritative 0.45s impact instant as the existing 75%-cycle hit.
    Reuse the unique, time-indexed arrow launch stream (owner 1 = Hunter)
    and late-recipient replay; render distinct Hunter streaks and small
    cosmetic impact sparks on host and guest. Restart the attack animation
    phase on each volley. Client effects never apply damage.
    **Windows tests and real two-player attack presentation pass.**
  - [x] Retain unsent projectile IDs beyond the old 2048-event
    presentation window, expose per-recipient sent cursors on the relay
    worker, capture oldest outstanding 2048-event batches, and prune
    expired history only after all active recipients have been sent each
    launch. Preserve a late batch through one decoded delta for cosmetic
    playback. Added a 4096-event sequential-batch regression.
    **Windows test passes; live congestion and Linux stress remain.**
  - [x] Render bounded hit flashes from authoritative health decreases and
    removal/death bursts on host and guests. Match-local presentation now
    captures HP for parity, and the diff has unit coverage. No client-side
    projectile prediction modifies health.
    **Attack presentation passes; death animation presentation fails.**
  - [ ] Test TCP backpressure and stalled recipients, including batches
    larger than 2048 launches, skipped intermediate captures, packet
    latency/jitter, abrupt disconnects and resynchronization from a full
    authoritative snapshot. Verify no visual-event loss on an intact
    stream, no duplicate presentation, no speculative HP changes, and
    correct cancellation, impact and death behavior on 2/4/8 actual
    clients (including Windows/Linux). An asset-backed Hunter regression
    now schedules a hit, replaces the order before impact, and checks
    that no HP loss occurs. **Windows test pass.** Expired cosmetic projectiles need
    not be replayed after reconnect; health, entities and orders must
    recover correctly. The 65,536 unsent-record memory guard currently
    fails explicitly on extreme stalls rather than silently discarding
    events; validate that behavior and the all-Builders-disconnected
    cleanup path. TCP write completion is a transport handoff, not proof
    the remote application rendered a shot, and no extra ACK protocol
    is required for this visual-only stream.
- [x] 1.6a Restore match-sized starting Builder pruning for both legacy
  two-peer Hunter-creator matches and multi-host matches. The lobby reports
  match_player_count=2 through the legacy assignment/bootstrap, the presenter
  configures pruning regardless of transport mode, and the simulation now
  refuses snapshots before participant admission or if the initial world
  contains the wrong number of Builders. Added the `fvd_network_builder_count`
  asset-backed 2/3/8-player regression and lobby status checks.
  **Windows tests pass; Linux remains follow-up.**
- [ ] 1.6b Display all Builder construction phases, tower ownership,
  reservations and planned/queued foundations consistently for Hunter
  and Builders, including cancellation and retasking.
  - [x] Transmit actual phase start/end timestamps (wire protocol v9), use
    host-authored timed progress in the guest HUD, and test wire roundtrip,
    construction-only deltas and loopback playback. **Windows tests pass.**
  - [x] Derive uniform host/guest site overlays from authoritative Towers,
    Foundations, active orders and Builder-owned reserved tiles. Show owner
    labels and queued/planned/building/complete phases, correct guest queue
    totals, and test cancellation/retasking and loopback phases.
    **Windows tests and building animation presentation pass; broader overlay
    acceptance remains.**
  - [ ] Visually verify all 2/4/8-player screens and real camera movement,
    conflicting reservations, cancellation and retasking on Windows/Linux.
- [x] 1.7 Replicate the first Hunter spawn deadline through snapshots and
  deltas, evaluate the guest countdown on its synchronized presentation
  clock, and derive active order lines and target highlights from the same
  authoritative world state. Protocol v16 rejects clients without the new
  timed field. The later rally-point feature uses this authoritative pipeline.
  Wire, delta, presentation, loopback and spawn regressions pass on Windows.
  **Two-player presentation passes; Linux and 4/8-player load remain.**
- [ ] 1.8 Every new gameplay feature must add its wire/presentation
  representation and unit, loopback, multi-client and visual acceptance
  checks in the same task—not as later network polish.
- [x] 1.9 Validate live recovery and reconnection cleanup for the two-player
  alpha target. **User acceptance, 2026-10-29:** reconnect works and a real
  host handover preserved the match. Four/eight-player, cross-platform and
  extended failure-path stress remain alpha follow-up rather than blockers.
- [x] 1.9a Implement an independent TCP/ConnectionSession owner for each
  Builder after authenticated multi-host bootstrap. Each worker advances
  recipient-private delta/projectile cursors, sends command decisions and
  recovery snapshots, and reads that Builder's commands and heartbeats.
  The Hunter coordinator handles disconnect bookkeeping and queues notices,
  never sending frames on another Builder's socket. Add 4/8-client
  integration regressions for rapid captures, concurrent recovery requests,
  post-recovery commands, and existing disconnect/handoff checks.
  **Implementation committed on a separate PR; compilation pending.**
- [ ] 1.9b Verify true slow-recipient isolation with the calibrated PR #7
  TCP proxy, particularly Builder 5 at 256 kbps and a 3s stall versus
  Builders 2–4 at 2 Mbps. Require healthy clients' command P95, state-age
  P95/max, private revisions and pending orders to remain near the
  no-slow-client control; confirm correct Builder departure notices,
  queue coalescing, recovery and worker teardown on Windows and Linux.
- [ ] 1.10 Add match end-state and victory feedback to the same replicated
  state/event pipeline when win/loss rules are implemented.

## Remaining gaps from the current audit

- Guest positions advance on authoritative future motion curves without a new
  per-entity record each capture. Two-player performance passes; Linux and
  4/8-player load remain unverified.
- Entity facing plus attack and building animations passed real two-player
  presentation testing. Death animation presentation still fails.
- Timed arrow launch events remain available briefly after impact, including
  late guest visuals. Attack presentation passes; high-volume live congestion,
  death animation presentation and 4/8-player visual verification remain.
  Attack orders have authoritative target/destination overlays; construction
  states and private scores have wire records.
- Network state still uses one recipient-oriented world capture at a time.
  Ensure update cadence and performance do not deteriorate with players.

## Verification evidence / next run

The [FVD prototype verification matrix](../../../doc/code/network/FVD_PROTOTYPE_VERIFICATION.md)
maps every outstanding OpenSpec acceptance item to its existing or new
automated test and the remaining real-client manual checks.

**2026-10-29:** A Windows MSVC RelWithDebInfo build completed and all 95
registered tests passed with assets (`run.exe test -a --have-assets`), and
`checkmerge` passed. Real two-player acceptance reports excellent performance,
working reconnect and host handover, and passing attack/building animations.
Death animations fail. Linux execution and four/eight-player graphical load
testing remain alpha follow-up.
