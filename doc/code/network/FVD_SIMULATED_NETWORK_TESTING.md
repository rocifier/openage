# FVD simulated multiplayer network tests — first vertical slice

This is the **first iteration** of a headless network benchmark, not a claim
that five complete graphical FVD games have been exercised. It reuses the real
`RelayLobbyClient`, public-room relay, authenticated command/result handling,
and recipient-private `WorldState` delta pipelines. Five clients = one
authority plus four independent guest workers, running in one test process
through real loopback TCP sockets.

**Benchmark topology:** the five-client case uses a deterministic
role-preserving Hunter rehost: the authoritative Hunter connects directly to
the relay, and each of four Builders uses exactly one dedicated proxy port.
The benchmark asserts this mapping and logs
`FVD_BENCH_TOPOLOGY hunter=direct builders=proxied`. This matters:
early versions used a random Hunter role draw, which could put the authority
behind the supposedly slow Builder link. Results from that version are **not
valid for testing slow-Builder isolation** and must be rerun after rebuilding
the C++ test.

The **scripted test** sends Move command intents and returns accepted
authoritative command results; it also offers synthetic world updates (with
optional batches of timed projectile launch records). **Since the independent
scheduler update, command generation and authoritative world updates continue
on separate wall-clock schedules even if a guest fails to respond.** No
Builder's acknowledgement gates the next update or command. This fixes the
old benchmark's accidental lockstep, not the game's transport implementation. It does **not** run
Hunter/Builder combat, pathfinding or QML rendering. This gives repeatable
transport measurements, **not** proof that five humans will see smooth graphics
or that actual dense combat generates the tested projectile load.

## Running

With the current FVD C++ build installed, from the repository root:

The Python byte shaper clamps all scheduled sleep intervals to zero or above,
even if a deadline elapses between checking the clock and sleeping. Any
unexpected proxy forwarding worker exception marks the JSON result as failed
and is included in `proxy_errors`; a crashed shaper can no longer silently
produce a green benchmark report. Run the pure-Python regression with
`python -m unittest openage.testing.test_fvd_network_benchmark`.

```sh
# Opt-in no-impairment loopback microbenchmark
bin/run test network::tests::lobby_latency

# 5-player experiment: add ~220ms RTT, +/-40ms total jitter,
# and 2 Mbps per-direction to each remote participant.
python3 -m openage.testing.fvd_network_benchmark \
  --runner bin/run --rtt-ms 220 --jitter-ms 40 --kbps 2000 \
  --rounds 20 --update-ms 500 --command-ms 500 \
  --json /tmp/fvd-network-220ms.json

# Heavy serialization + one struggling guest. Still *synthetic* projectiles.
python3 -m openage.testing.fvd_network_benchmark \
  --runner bin/run --rtt-ms 220 --jitter-ms 40 --kbps 2000 \
  --slow-client 4 --slow-kbps 256 --stall-after-s 6.5 --stall-s 3 \
  --arrows 1024 --rounds 20 --update-ms 500 --command-ms 500 \
  --json /tmp/fvd-network-stall.json
```

Windows: from PowerShell in the openage repository, use a native launcher:

```powershell
Test-Path .\.bin\msvc-vcpkg\run.exe
python -m openage.testing.fvd_network_benchmark --runner .\.bin\msvc-vcpkg\run.exe --rtt-ms 220 --jitter-ms 40 --kbps 2000 --rounds 20 --json fvd-network-results.json
```

If the first line prints `False`, locate the build with
`Get-ChildItem -Recurse -Filter run.exe -File` and supply its path to
`--runner`. The benchmark now resolves `bin/run` to `bin/run.exe` on
Windows automatically when that file exists. Do not attempt to launch the
Unix-only `bin/run` directly through Windows subprocess. No Linux `tc` or
elevated permissions are needed.

The Python launcher starts **four ordinary TCP byte-stream proxies** in
threads, one for each guest, between those workers and the real local relay.
It passes the relay and proxy ports into the opt-in C++ test using environment
variables. A fixed seed makes requested jitter schedules reproducible, but
OS scheduling and actual duration are **not** deterministic.

### Metrics

The test logs `FVD_BENCH_SCHEDULE mode=independent`, then prints an
aggregate `FVD_BENCH_RESULT` line and separate `FVD_BENCH_CLIENT` lines.
`--rounds` is the number of world-update scheduling intervals. For example,
`--rounds 20 --update-ms 500` generates a **10-second continuously scheduled
workload**, plus 1.5 seconds of measurement drain. `--command-ms` independently
sets each Builder's command rate; by default both schedules run at 500 ms.
Heavy-update pressure therefore coexists with command traffic throughout.

Measured p50/p95/max command and world-delivery latency are computed for
**received** messages, not for unsent, overwritten or still-pending work.
Reporting only received-message latency risks survivorship bias when a slow
client stops receiving; inspect outstanding and state-age metrics too.

- **Command turnaround**: wall-clock time from the guest's
  `send_guest_command()` request to receiving the authoritative command
  result via the full proxy/relay/session path, **including the harness
  Hunter's polling/acknowledgement loop**. It does **not** include the real
  gamestate accepting or rendering the move.
- **World delivery**: wall-clock time from a scripted
  `provide_host_update()` call to that guest's ReplicationTracker accepting
  the observed revision. Intermediate offered captures may be **superseded**
  before transmission and are counted as `world_skipped`, not lost network
  packets. A slow guest can lag behind without preventing the next host offer.
  This measurement excludes simulation capture, frame rendering and reaction.
- **State age**: sampled every ~50 ms per guest, measuring wall-clock time
  since the captured world update currently applied by that guest was offered.
  It increases during stalls even when no new world update is received.
  A guest that has not received any benchmark update yet is measured relative
  to the benchmark start. Inspect `state_age_p95_ms` and
  `state_age_max_ms` to detect unhealthy stalls.
- **Outstanding commands and state revision**: each client reports
  `command_offered`, `command_samples`, `command_pending`,
  `world_offered`, `world_samples`, `world_skipped`,
  `latest_offered_revision`, `latest_applied_revision` and `alive`.
  An unfinished slow peer is *not* silently counted as up-to-date.
- **Traffic**: proxy byte counts in both directions. These are actual TCP
  payload bytes observed by the proxy, not authoritative simulation traffic
  statistics.

The Python launcher emits `FVD_NETWORK_REPORT` JSON including aggregate and
**per-guest** p50/p95/max, per-link bytes, shaping configuration, execution
status and a warning about the non-graphical coverage. It can also save JSON.

The benchmark does **not** assert absolute latency thresholds: saturated
test machines should be reported, not automatically declared incorrect.
Its `pass` field indicates that the test ran and its required protocol and
identity assertions passed; **it does not mean that every scheduled update
arrived or that every Builder was responsive**. Inspect the per-client
pending/backlog/age metrics. The five-second role reveal and original capacity
handshake/command smoke checks run before the timed workload. The test skips
the original post-workload disconnect smoke checks because a deliberately slow
recipient may still be catching up; those are still covered by `lobby_capacity`.
It performs explicit client/relay cleanup at the end.

## Profiles to collect

| Profile | RTT | Jitter | Guest capacity | Purpose |
|---|---:|---:|---:|---|
| Baseline | 0ms | 0ms | unlimited | CPU/relay floor |
| Regional | 100ms | +/-20ms total | 10Mbps | fast links |
| International | 220ms | +/-40ms total | 2Mbps | prospective NZ-to-overseas play |
| Difficult | 320ms | +/-80ms total | 256kbps | congested/remote players |
| Slow-recipient isolation | 220ms | +/-40ms total | 2Mbps except one at 256kbps with 3s stall | whether one client delays the others |

**Interpretation warning:** use `--slow-client 4` to throttle the
Builder attached to proxy 4 (player 5). After the topology fix, the Hunter is
always direct. Compare each Builder's metrics with an identical no-slow-client
run, not just the aggregate p95.

Run each profile several times in isolation and compare the same
`--rounds`/`--arrows`/`--update-ms`/`--command-ms`/seed. **Old results from
the barrier-based benchmark are not directly comparable to the new continuous
scheduler. Rebuild the C++ executable as well as updating the Python script.** **Test the slow-recipient case both with
`--arrows 0` and with `--arrows 1024`** so TCP backpressure has data to
act on. A 3-second stop is intentionally below FVD's 15-second timeout.

Caveats: the proxy shapes **TCP bytes after the sender's kernel**, not IP
packets, congestion control, dropped packets or physical upload queueing.
No packet loss is injected in this phase; TCP loss/delay requires a later
system-level network emulator or an extended proxy. The per-direction
bandwidth cap is a pacing approximation. Jitter is applied to forwarding
chunks and may vary with how TCP coalesces writes.

## High-priority next increments

1. **Measurement correctness:** validate the C++ test and the proxy on
   Windows + Linux. Check a baseline and a fixed-RTT result; keep latency
   measurements labelled *transport* rather than visual responsiveness.
   Introduce controlled process/CPU measurements and repeatable percentiles.
2. **Independent slow-client isolation:** per-guest turnaround and delivery
   latency are now exported. Compare each healthy guest to its own baseline,
   repeat fixed-seed runs, and add fair thresholds and dedicated regressions
   for a stalled recipient. An aggregate p95 alone is insufficient.
3. **Actual gameplay workload:** run asset-backed FVD simulation alongside the
   relay instead of scripted acceptances/private snapshots. Generate
   deterministic construction and Hunter combat loads and measure real shots
   per second, capture CPU, serialization time and corrections.
4. **Rendering:** run graphical guests with frame-time and animation-phase
   instrumentation; compare command-to-visible-result as well as TCP RTT.
5. **Failure paths:** loss/retransmission, repeated reconnect, authority
   succession and four/eight-player workloads. Keep disconnection vs a
   short stall distinct.

### OpenSpec evidence boundary

This is an additive tool for the outstanding
`complete-gameplay-replication` **1.3b / 1.5b / 1.9** follow-ups.
It does not complete hardware, rendering, real-combat or recovery
acceptance tasks. Record measured CI/desktop output before checking boxes.

## Follow-on OpenSpec

See `openspec/changes/simulated-multiplayer-testing/tasks.md` for the staged
implementation, validation, real-simulation and graphical acceptance work.
