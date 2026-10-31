# FVD Windows multiplayer acceptance — OpenSpec 1.8

This checklist is for **real Windows game processes**. Local C++/TCP
regressions are necessary, but cannot establish that two PCs share the same
rendered world and manual Hunter/Builder controls. Do not mark 1.8 complete
until the observations below have been recorded.

## Lobby input persistence (manual smoke test)

FVD stores the editable **player name, relay address, relay port, game name,
maximum player count, and room ID** as per-user Qt INI preferences
(`openage/fvd.ini` in the platform's standard configuration directory).
They are saved on user edits, not only when hosting or joining. The optional
private-alpha access token is deliberately **never stored in plaintext**
and must be re-entered each launch.

1. Open FVD, edit all six non-sensitive fields, and exit without hosting.
2. Relaunch and confirm the entered values appear **in the text boxes**, not
   merely in the underlying connection settings.
3. Host or join once and verify that the visible values are the ones submitted.
4. Restart again: the values should persist. The access-token box must be
   blank, and a separate OS user should retain independent preferences.

The normal defaults are retained on a fresh installation.

## Automated preflight (one Windows development build)

Build the FVD branch, then run:

```powershell
bin/run test openage::network::tests::lobby_capacity
bin/run test openage::network::tests::lobby_relay_client
bin/run test openage::network::tests::fvd_multi_session
bin/run test openage::network::tests::relay_multi_host_lobby
$LASTEXITCODE
```

The new `lobby_capacity` test executes 4 and 8 **actual RelayLobbyClient
workers** under both ordinary lobby start and a forced Hunter-owned handoff
with an original Builder creator. For each it checks private world snapshots
and kill totals, concurrent queued Builder orders and private results,
recipient-specific world deltas, Builder disconnection and surviving orders,
and terminal Hunter disconnection. This is a network/protocol harness:
accepted/rejected contested-tile decisions are injected by the test,
not computed by the full tower-reservation simulation.

`fvd_multi_session` additionally submits concurrent Builder movement through
the authoritative game simulation and tests disconnect continuation.
`lobby_relay_client` exercises the complete two/three-player worker flow.
The runner uses zero-based progress and prints failures only: returning to
PowerShell with exit code 0 indicates success.

## Asynchronous per-Builder Hunter streams (new, requires validation)

After initial world bootstrap completes, the Hunter starts one independent
C++ transport worker per connected Builder (at most seven remote Builders).
Each worker is the **sole owner** of that Builder's authenticated
`ConnectionSession` and TCP send/receive path. Its ordered outbound flow
prioritizes participant disconnect notices and command results before a
coalesced private world update; its host-side delta baseline, projectile
delivery cursor and snapshot-recovery replies remain recipient-specific.

The Hunter's coordinator only processes worker-reported departures and queues
notifications for other workers. It **never calls `send_frame()` on any live
Builder's streaming socket**. A blocked 15-second TCP send can stall that
recipient but must not stall healthy recipients or the Hunter simulation.

### Windows validation of this change

1. Build this PR's C++ branch (the Python proxy changes belong to
   independent draft PR #7). Run `run.exe test network::tests::lobby_capacity`,
   including its new 4/8-player rapid private-capture, simultaneous recovery
   snapshot, and post-recovery command checks.
2. Run the other existing network regressions
   `network::tests::lobby_relay_client`,
   `network::tests::fvd_multi_session` and
   `network::tests::world_delta`. Re-run `lobby_capacity` multiple
   times to detect teardown or worker lifetime races.
3. Combine the **PR #7 benchmark** with this branch in a local test branch
   or rebuilt workspace. Repeat the same
   `--kbps 2000 --rtt-ms 220 --jitter-ms 40 --arrows 1024`
   five-player baseline, then
   `--slow-client 4 --slow-kbps 256 --stall-after-s 6.5 --stall-s 3`,
   with `--proxy-buffer-kb 64 --socket-buffer-kb 32`.
   Confirm Builders 2–4 continue receiving fresh world captures and
   command results despite Builder 5's backpressure. Inspect per-client
   `state_age_p95_ms`, `command_p95_ms`, pending commands,
   revisions, and proxy bytes—not only the aggregate `pass` field.
4. Verify that closing Builder 5 still notifies the other Builders,
   a surviving Builder can order units, and closing the Hunter terminates
   all recipient workers and clients without stuck or orphaned sockets.

### Windows evidence received — 2026-10-30

User-reported `network::tests::lobby_capacity` on the combined PR #7 + PR #8
Windows MSVC build **exited 0**, including four- and eight-player scenarios
with Hunter rehost and intentional disconnects. The test still needs repeat
runs and independent Linux verification.

A user-reported PR #7 five-player run using the async C++ worker build
(`--rtt-ms 220 --jitter-ms 40 --kbps 2000 --arrows 1024 --rounds 20
--update-ms 500 --command-ms 500 --slow-client 4 --slow-kbps 256
--stall-after-s 6.5 --stall-s 3`; 64-KiB proxy queue, 32-KiB proxy sockets)
returned `pass: true`. Builders 2–4 had:

| Metric, three healthy Builders | Previous sequential build, slow Builder 5 | Async build, slow Builder 5 |
|---|---:|---:|
| Command P95 | ~5,812–5,843ms | ~495–496ms |
| State age P95 | ~5,749ms | ~1,397ms |
| Commands received | 54 / 60 | 60 / 60 |
| World updates observed | 24 / 60 | 60 / 60 |
| World captures skipped | 30 total | 0 total |
| Downstream bytes / healthy Builder | ~725KB | ~1.81MB |

The async healthy Builders' command P95 (~495ms) was also essentially
unchanged versus the previous all-healthy baseline (~497ms). Builder 5
remained intentionally congested: 18 of 20 command results pending,
2 of 20 world captures observed, state-age P95 ~10.39 seconds.
These results **demonstrate recipient isolation in this synthetic scripted
scenario**, not real combat or render smoothness. The reported state-age
P95 includes the benchmark's 1.5-second post-workload drain.

**Still open before merge/acceptance:** repeated tests, Linux build, actual
gameplay projectile burst/recovery and disconnect under saturation, and
explicit fresh snapshot consistency after a slow client catches up. Keep
the PR draft until those checks are reviewed.

The previous sequential code allowed a stalled recipient's send to delay
every other recipient's next pass.

## Two Windows PCs (required)

1. Run the **same FVD branch build and gameplay assets** on both PCs.
   Launch a persistent relay on PC A:
   ```powershell
   bin/run relay --listen 0.0.0.0 --port 32456 --max-connections 128
   ```
   Allow **inbound TCP 32456** in PC A's Windows Firewall on the trusted
   network. PC B must reach PC A at its LAN IPv4 address. On PC A the
   game may use `127.0.0.1`; on PC B it must use **PC A's LAN IP**, not
   `127.0.0.1`. The relay must remain running in its own terminal.
2. Start `bin/run game` on both PCs. In each lobby enter its appropriate
   relay address, port 32456, and a different player name. On either PC,
   choose **Host Game** for 2 players. On the other, use **Refresh Games**
   and **Join Room**. When both names appear, the lobby creator presses
   **Start Game**.
3. Confirm that both screens show the same one-Hunter/one-Builder lottery,
   then dismiss the role overlay automatically after about five seconds.
   The Hunter must own authority *even when the lobby creator is Builder*.
   Both must see the same initial map and Builder/Hunter entities.
4. Have the Builder move, then issue a tower build (B, then click), and
   observe both screens. Check the translucent footprint reservation and
   the same `P2 PLANNED` / `P2 BUILDING` / `P2 TOWER` site labels on
   Hunter and Builder, including while moving the camera. Shift-click
   additional sites and confirm they appear as `P2 QUEUED` on both screens
   and the Builder HUD shows the correct count. Cancel or retask the
   Builder and confirm every canceled site label and reserved footprint
   disappears on both screens, with no stale marker after recovery.
    At simulation time 20 seconds a Hunter horse
   should appear, with later horses at one-second intervals. As the
   Hunter, select multiple horses and use A then click to AttackMove;
   confirm movement, tower arrows, damage and deaths converge on both
   PCs. The Hunter must not be able to build; Builders must not control
   Hunter horses. Private kill counts must match the owning Builder.
5. Restart each executable or reset locally with the temporary developer
   F10 leave shortcut. Repeat until the **original lobby creator draws
   Builder**, then start the match and confirm
   `handoff_committed epoch=2` and `authority_active epoch=2` in the
   Hunter's game log. F10 is *not* an agreed rematch protocol.
6. Close the Builder game. The Hunter's game must keep advancing.
   In a two-human match there are no other Builders, so use the
   four-player test below to verify continuation of remote Builder orders.
   Close the Hunter: everyone else must leave the active match or show a
   connection error, never advance an independent simulation.

## Four and eight game processes (required)

For each of **four** and **eight** human participants, use the same relay
(or compatible reachable IP connections). Set the creator lobby's maximum
player count before hosting. Each guest must join the **same** room.

- Verify exactly one Hunter and N-1 unique Builder slots after Start.
  Each screen must agree on roles. Verify all connected players reach the
  world and have only their own private kill count.
- Two or more Builders should simultaneously select their own units and
  queue tower construction at the **same open tile**. Verify **at most one**
  construction reservation appears, that rejected/pending orders resolve,
  and all player screens converge. Check each reservation and site label
  shows the correct owner on Hunter and every Builder screen; distinguish
  queued foundations from the Builder's current planned/building site.
- Other Builders should queue distinct towers and move while the Hunter
  selects several horses and AttackMoves. Verify ownership, arrows, health,
  death and kill totals agree across game processes.
- If the original creator draws Builder, wait for the Hunter-owned handoff
  to finish. Then close that original creator's game. The Hunter and other
  Builders must continue, and surviving Builders must still get command
  acknowledgements and state deltas. Repeat with a different Builder.
- Close the Hunter last. Every surviving Builder must stop gameplay;
  none may continue authoritatively. Check that relay channels clean up.
- Run several new matches, including local F10 leave/reset between them.
  Verify no stale rooms, cross-match orders, duplicate entity IDs, or
  stuck role overlay. Reopen game processes if local reset has problems;
  record that as a separate failure, not a successful rematch.

## Diagnostics and pass criteria

Save relevant **game** and **relay** log excerpts with room ID and player
counts. Game logs use `[FVD/authority]` for `role_draw`,
`handoff_requested`, `handoff_proof_accepted`,
`handoff_committed`, and `authority_active`.
The relay reports `[FVD/relay] pair_connected`,
`forwarding_started` and `forwarding_ended`. It cannot know which
participant is Hunter. Secrets and tickets are never logged.

A test is **PASS** only when its visual/gameplay conditions and network
checks both succeed. Unexpected `session_error`, stalled `assigned`
or `bootstrap`, missing `MATCH_READY`, stalled world revisions or
stale orders are **FAIL**. An intentional test disconnect may generate a
WARN without being a failure. Record OS/build identifier, participant
count, original host's assigned role, test result and relevant log lines.

**Status:** Automated 4/8-worker coverage has been implemented, but requires
Windows execution. The user previously verified the live two/three-client
paths and original Builder-lobby-creator disconnect. The real two-physical-PC
and live 4/8-process acceptance above remain outstanding.
