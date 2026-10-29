# FVD simulated multiplayer network tests — first vertical slice

This is the **first iteration** of a headless network benchmark, not a claim
that five complete graphical FVD games have been exercised. It reuses the real
`RelayLobbyClient`, public-room relay, authenticated command/result handling,
and recipient-private `WorldState` delta pipelines. Five clients = one
authority plus four independent guest workers, running in one test process
through real loopback TCP sockets.

The **scripted test** sends Move command intents and returns accepted
authoritative command results; it also offers synthetic world updates (with
optional batches of timed projectile launch records). It does **not** run
Hunter/Builder combat, pathfinding or QML rendering. This gives repeatable
transport measurements, **not** proof that five humans will see smooth graphics
or that actual dense combat generates the tested projectile load.

## Running

With the current FVD C++ build installed, from the repository root:

```sh
# Opt-in no-impairment loopback microbenchmark
bin/run test network::tests::lobby_latency

# 5-player experiment: add ~220ms RTT, +/-40ms total jitter,
# and 2 Mbps per-direction to each remote participant.
python3 -m openage.testing.fvd_network_benchmark \
  --runner bin/run --rtt-ms 220 --jitter-ms 40 --kbps 2000 \
  --rounds 20 --json /tmp/fvd-network-220ms.json

# Heavy serialization + one struggling guest. Still *synthetic* projectiles.
python3 -m openage.testing.fvd_network_benchmark \
  --runner bin/run --rtt-ms 220 --jitter-ms 40 --kbps 2000 \
  --slow-client 4 --slow-kbps 256 --stall-after-s 6.5 --stall-s 3 \
  --arrows 1024 --rounds 20 --json /tmp/fvd-network-stall.json
```

Windows: substitute the locally configured launcher (for example,
`bin/run.exe`) and use `py -3 -m openage.testing.fvd_network_benchmark`.
No Linux `tc` or elevated permissions are needed.

The Python launcher starts **four ordinary TCP byte-stream proxies** in
threads, one for each guest, between those workers and the real local relay.
It passes the relay and proxy ports into the opt-in C++ test using environment
variables. A fixed seed makes requested jitter schedules reproducible, but
OS scheduling and actual duration are **not** deterministic.

### Metrics

The test prints one machine-readable `FVD_BENCH_RESULT` line with the
number of clients, rounds, command samples, command p50/p95/max latency,
and recipient-update p50/p95/max latency.

- **Command turnaround**: wall-clock time from the guest's
  `send_guest_command()` request to receiving the authoritative command
  result via the full proxy/relay/session path, **including the harness
  Hunter's polling/acknowledgement loop**. It does **not** include the real
  gamestate accepting or rendering the move.
- **World delivery**: wall-clock time from a scripted
  `provide_host_update()` call to that guest's ReplicationTracker accepting
  the expected revision. It does **not** include simulation capture time,
  frame rendering or human reaction time.
- **Traffic**: proxy byte counts in both directions. These are actual TCP
  payload bytes observed by the proxy, not authoritative simulation traffic
  statistics.

The Python launcher emits `FVD_NETWORK_REPORT` JSON including the two
latency distributions, per-link bytes, shaping configuration, execution
status and a warning about the non-graphical coverage. It can also save JSON.

The benchmark does **not** assert absolute latency thresholds: saturated
test machines should be reported, not automatically declared incorrect.
Functional checks still fail if command results, snapshots, ownership or
revisions are lost or inconsistent. The five-second role reveal and existing
capacity smoke checks run before timed rounds.

## Profiles to collect

| Profile | RTT | Jitter | Guest capacity | Purpose |
|---|---:|---:|---:|---|
| Baseline | 0ms | 0ms | unlimited | CPU/relay floor |
| Regional | 100ms | +/-20ms total | 10Mbps | fast links |
| International | 220ms | +/-40ms total | 2Mbps | prospective NZ-to-overseas play |
| Difficult | 320ms | +/-80ms total | 256kbps | congested/remote players |
| Slow-recipient isolation | 220ms | +/-40ms total | 2Mbps except one at 256kbps with 3s stall | whether one client delays the others |

Run each profile several times in isolation and compare the same
`--rounds`/`--arrows`/seed. **Test the slow-recipient case both with
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
2. **Independent slow-client isolation:** export **per-guest** turnaround and
   update age, not just aggregate p95, so we can explicitly assert whether
   healthy guests are delayed by a saturated guest. A slow client may
   otherwise distort aggregate results without identifying its neighbours.
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
