# Proposal: simulated end-to-end multiplayer acceptance

## Why

Real 4–8-person graphical sessions are difficult to schedule across
international time zones. Wire unit tests cannot answer whether one slow
recipient impacts healthy players, or whether large combat workloads make the
game feel sluggish. Build repeatable scripted multiplayer experiments first,
then corroborate the results with real human visual acceptance.

## Scope

- Drive authenticated commands and recipient-private WorldState updates through
  the actual relay and TCP workers with five players on one machine.
- Shape separate guest links (delay, jitter, bandwidth, temporary stalls).
- Report per-client command turnaround, state-update delivery latency and bytes.
- Make each experiment reproducible and opt-in; never quietly transform
  synthetic or transport measurements into "actual gameplay FPS" claims.
- Expand to authoritative Hunter/Builder simulation, real projectile generation,
  4/8 clients, CPU and graphics time only after the networking baseline works.

## Non-goals of first increment

No actual gamestate combat, QML rendering, game FPS, or real OS packet loss.
The Python byte proxy is not kernel traffic shaping. Synthetic launch records
stress network serialization but do not prove real arrow-generation performance.

## Starting point

`doc/code/network/FVD_SIMULATED_NETWORK_TESTING.md` describes running and
interpreting the first prototype; it must pass local Windows/Linux validation
before being treated as reliable performance evidence.
