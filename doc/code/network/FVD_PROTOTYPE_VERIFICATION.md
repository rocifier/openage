# FVD prototype verification matrix

**Last updated:** 2026-10-31, branch `FVD`. **Evidence status:** the Windows
MSVC RelWithDebInfo build and all 95 registered tests passed with FVD assets
on 2026-10-30, and `checkmerge` passed. Subsequent user-reported two-player
acceptance covers death animations, projectile-delivery stress, construction
overlays and victory-condition gameplay, in addition to the earlier multiplayer
smoke tests. Exact projectile stress parameters were not supplied. Linux and
four/eight-player graphical load and failure-path testing remain follow-up.

## Automated preflight — Linux and Windows

First build the same current `FVD` commit and ensure the converted FVD
assets are installed. From the repository root, run:

```sh
./run test -a --have-assets
```

The last independently reported Linux result was **82/86**, prior to
subsequent fixes. The fresh result must be recorded before marking these
items verified. On Windows, use the configured `bin/run` launcher
(e.g. `bin/run test -a --have-assets`).

If the full run fails, rerun the relevant named tests, with `-a --have-assets`
for asset-backed tests:

```sh
bin/run test network::tests::world_delta
bin/run test network::tests::messages
bin/run test network::tests::replication
bin/run test network::tests::lobby_capacity
bin/run test network::tests::lobby_relay_client
bin/run test network::tests::fvd_loopback
bin/run test network::tests::fvd_multi_session -a --have-assets
bin/run test gamestate::tests::fvd_hunter_spawning -a --have-assets
bin/run test gamestate::tests::fvd_hunter_focus_attack -a --have-assets
bin/run test gamestate::tests::fvd_hunter_combat -a --have-assets
bin/run test gamestate::tests::fvd_scenario -a --have-assets
```

Use the installed launcher path appropriate to each machine. Repeat
`fvd_scenario` and `lobby_capacity` multiple times to expose intermittent
selection and startup failures; a single pass cannot rule out flakiness.
Run `make tests` and `make checkmerge` where that build workflow is
configured.

### Verification mapping

| OpenSpec scope | Automated evidence currently present | Status |
|---|---|---|
| Movement curves / sparse traffic (1.1–1.3a) | `world_delta` (512 horses, 2/4/8 player rosters, changed routes, packet size), `messages`, `replication` | Windows automation and real two-player performance pass; Linux and 4/8 graphical load remain |
| Facing and animation (1.4a–b) | `messages`, `world_delta`, `fvd_animation_phase` (includes same-action ATTACK restart and recovery), `fvd_scenario` (death atlas direction, scale and timing) | Attack/building/death animations pass user-reported two-player testing (2026-10-31); 4/8 players and Linux remain |
| Projectile sequencing / late playback (1.5a–b) | `world_delta` (4,096 launches in two 2,048 batches), `messages`, `fvd_hunter_combat`, `fvd_hunter_focus_attack` (cancel after shot launch; no HP loss), `fvd_loopback` | User-reported two-player projectile stress PASS; exact launch count/latency not documented, other TCP adversity and 4/8 players remain |
| Construction and HUD state (1.6a–b) | `fvd_network_builder_count`, `fvd_construction`, `fvd_build_queue`, `fvd_build_line_order`, `fvd_retask_foundation`, `fvd_presentation_snapshot`, `replication` | User-reported two-player construction overlays PASS; 4/8-client visual parity and Linux remain |
| Hunter spawns, targeting and orders (1.7) | `messages`, `world_delta`, `replication` (authoritative order-marker resolution), `fvd_loopback`, `fvd_hunter_spawning`, `fvd_hunter_attack_move_regressions`, `fvd_hunter_focus_attack`, `fvd_hunter_stances` | Windows pass; target highlighting needs graphical inspection |
| 2–8 player protocol / recovery (1.9) | `lobby_capacity`, `lobby_relay_client`, `fvd_multi_session`, `fvd_loopback`, `replication`, `world_delta`, `authority_succession` | Windows automation plus real two-player reconnect/host handover pass; Linux and 4/8 graphical stress remain |
| HUD input / cancel (build-menu 1.6) | `tower_placement_mode`, `hud_mouse_capture`, construction tests | Windows pass; mouse/QML behavior requires game |
| Tower kill credit (combat 1.9) | `fvd_hunter_combat`, `replication` | Windows pass; selected-tower kill label needs visual check |
| Endgame (1.10) | v17 snapshot/delta/recovery outcome tests; host winner rules for death/45-minute timer (no tower-count win); postgame command rejection; synchronized role-specific timer and QML winner/loser result overlay | Code present on `FVD`, user-reported two-player victory-condition PASS; build/edge cases, collapse, recovery, 4/8-client and Linux verification remain |

**Important boundaries:** `lobby_capacity` runs real relay-client workers
but not graphical game processes. The 512-entity regression tests encoded
size/parity, **not** wall-clock bandwidth, CPU or jitter. Transport tests
cannot demonstrate visual synchronization, actual camera behavior, or
Windows/Linux compatibility.

## Manual acceptance — user / collaborator

The two-player alpha session is accepted. The remaining sessions extend load,
adversity and cross-platform coverage; use the same commit on every machine
and save game/relay logs:

1. **Solo UI and construction** — click and drag-select Builders (especially
   small boxes and different zoom/camera positions); verify Build Tower
   button, B/Cancel/Escape, Shift-drag/queued tower foundations, progress,
   cancellations and per-Tower versus total kill display.
2. **Two real game clients — accepted for alpha (2026-10-29)** — Hunter/Builder movement, 4-tile ranged
   attacks, correct horse facing and animation restart, tower and Hunter
   projectiles, confirmed HP change and authoritative death state, construction
   ownership/labels while cameras move, private kill totals. Attack and
   building and death animation presentation pass (user-reported by
   2026-10-31). Two-player construction overlays and projectile stress
   also pass; detailed 4/8-player acceptance remains.
3. **Network adversity** — add packet delay/jitter/throughput limits or
   pause one recipient; verify backpressure recovery, no duplicate shots,
   no divergent damage, an interrupted/retasked Hunter attack, snapshot
   recovery, and explicit behavior if the 65,536 unsent-event guard fires.
4. **Four and eight graphical game processes — alpha follow-up** — exact role assignment,
   simultaneous contested tower reservations, attack/movement concurrency,
   spawn cadence and performance (host CPU, recipient bandwidth, frame
   latency). Repeat Hunter authority handoff when lobby creator becomes
   Builder; disconnect a Builder, then the Hunter.
5. **Cross-platform and repeated starts** — one Windows/Linux pair,
   repeated fresh matches and F10 reset, no stale lobbies, stuck role
   overlays or lost commands; record the Linux ten-start rehost
   reproduction result.

See [multiplayer acceptance](WINDOWS_MULTIPLAYER_ACCEPTANCE.md) and
[build/HUD interactive checks](../../../openspec/changes/build-menu-hud/verification.md)
for the step-by-step details. Physical-client tests are **not replaced**
by loopback or worker-level harnesses.

## User manual smoke-check results — 2026-10-29

These are **user-reported** gameplay observations. The user accepted the real
two-player topology for alpha while retaining broader 4/8-player,
cross-platform and adversity coverage as follow-up.

| Smoke check | Result | Scope / follow-up |
|---|---|---|
| 1. Multiplayer synchronisation | **PASS** | User reports excellent real two-player performance. Four/eight-player load remains alpha follow-up. |
| 2. Tower construction queues | **PASS** | User confirmed the instructed hands-on Builder FIFO Shift-drag and network queue smoke check; does not independently certify 2/4/8-client coverage or all edge cases. |
| 3. Hunter combat and stances | **PASS** | Hunter-only AttackMove and the corrected Patrol/stance interactions pass real gameplay testing. |
| 4. Disconnects and host migration | **PASS** | Reconnect and a real host handover preserved the match; wider stress remains alpha follow-up. |
| 5. Building destruction and rendering | **PASS** | User confirmed the instructed hands-on destruction / depth / shadow smoke check; not a substitute for full graphical parity and recovery tests. |
| 6. Animation parity | **PASS (2 players)** | Attack, building and directional death animations accepted in two-player testing; 4/8-player and Linux validation remain. |

### Additional user-reported acceptance — 2026-10-31

| Two-player smoke check | Result | Follow-up |
|---|---|---|
| Directional death animations | **PASS** | 4/8-player frame/performance and Linux parity not tested |
| Projectile-delivery stress | **PASS** | Exact event count, shaped-link parameters and complete TCP backpressure/recovery matrix not documented; 4/8 players untested |
| Construction overlays | **PASS** | 4/8-player conflict/camera coverage and Linux still open |
| Victory conditions | **PASS** | Post-result command/recovery, killed-Builder collapse, timer/rehost boundaries and 4/8-player cases still require separate checks |

The Windows build and all 95 automated tests plus `checkmerge` passed on
2026-10-30, before some later victory-condition commits. Four/eight-player
graphical load, cross-platform behavior and remaining network-adversity
testing remain alpha follow-up.

## Outstanding implementation versus verification

**Implemented, awaiting comprehensive acceptance:** 1.10 authoritative
victory/defeat replication and result feedback are present on `FVD`, with
user-reported two-player victory gameplay passing. The accepted Hunter-owned
authority topology makes the legacy mid-match Builder-authority checkpoint
fallback unnecessary unless later alpha evidence restores it as a requirement.

**Still verification:** 4/8-player CPU/bandwidth/jitter; high-load death
sprite phase/performance; detailed >2,048-projectile backpressure and recovery;
construction overlays under 4/8-player conflict/camera scenarios;
endgame collapse and recovered results; Linux/cross-platform behavior;
repeated startup and extended failure-path stress during alpha.

## Replication hardening follow-up — 2026-10-30 Windows execution

- The presenter now evaluates each authoritative combat state transition even
  when the simulation timestamp has not advanced; a zero-HP retained entity
  produces a death cue rather than a nonlethal flash.
- The `world_delta` test adds same-tick and zero-HP deaths, fog disappearance,
  6,144 sequential projectile IDs in three 2,048-event batches, confirmed HP
  parity and resynchronization from a fresh authoritative snapshot.
- The MSVC RelWithDebInfo build, `network::tests::world_delta`, all 95
  asset-backed tests and `checkmerge` pass.
- Actual directional death animation playback is accepted in the
  user-reported two-player test on 2026-10-31.
- This synthetic backlog is not a live TCP backpressure or bandwidth test.
  The separately reported two-player projectile stress pass does not certify
  all backpressure and recovery variants. Keep 4/8-client visual and network
  adversity acceptance open.
