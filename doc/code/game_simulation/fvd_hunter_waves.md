# FVD Hunter waves and reinforcement choices

FVD reads the Hunter's wave schedule and choice prompts from
`assets/fvd/data/scenario.nyan`. Changes to this file affect the gameplay
asset digest; multiplayer participants must have matching data.

## Main wave rules

`Scenario.hunter_waves` is a set of `HunterWave` objects. Each rule defines:

- `start_seconds`: when the wave becomes active, relative to match start.
- `interval_seconds`: time between spawn events, in simulation seconds.
- `count`: number of units created per event, between 1 and 64.
- `unit`: a nyan `GameEntity` definition for the spawned unit.

The earliest wave starts the Hunter spawn countdown. At each later wave's
start time, the previous main wave is **replaced**, not added to. Its existing
units remain in the world. The new rate takes effect exactly when the new
rule begins; this transition does not depend on the previous spawn interval.
The authoritative host schedules simulation-time events rather than checking
wall-clock timers.

## Supplemental choices

`Scenario.hunter_choices` is a set of `HunterWaveChoice` objects. Each has:

- `id`: unique numeric prompt identity (1–65535).
- `title`, `description`: HUD text.
- `open_seconds`: time when the Hunter sees the prompt.
- `close_seconds`: time when the unselected prompt expires.
- `options`: a set of `HunterWaveOption` objects.

Each option has its own `id` (unique within the prompt), `title`,
`description`, optional `icon` string, `unit`, `count`,
`spawn_seconds`, and `interval_seconds`. A selected option produces
`count` units in total, starting at `spawn_seconds` and spacing subsequent
units by `interval_seconds`. Spawning happens separately from the main wave.

The QML HUD discovers the prompt and its options from nyan metadata through
the presenter. It does not contain specific wave names, button labels,
counts, or deadlines. The Hunter can select exactly one option during the
configured window, and the host validates the selection on the simulation
thread. If the Hunter makes no selection, the supplemental wave is skipped.
Prompts must not overlap, and the last option spawn must precede the match
deadline. Up to sixteen options per prompt are supported by the HUD.

## Adding a wave

```nyan
LateRaid(HunterWave):
    HunterWave.start_seconds = 1200
    HunterWave.interval_seconds = 0.75
    HunterWave.count = 2
    HunterWave.unit = fvd.data.entities.Horse
```

Add `LateRaid` to `Scenario.hunter_waves`. The engine sorts waves by their
start time and rejects duplicate start times.

To add a choice, define a `HunterWaveOption`, include it in a
`HunterWaveChoice.options` set, then include that prompt in
`Scenario.hunter_choices`. The two example choices in `scenario.nyan`
demonstrate fast and sustained reinforcement schedules.

## Current unit support

The scheduler accepts nyan `GameEntity` references but only FVD's
registered `Horse` Hunter combat entity is currently valid. Its existing
combat, control, networking, and rendering code is still Horse-specific.
Additional Hunter unit types require those systems to recognize the new
entity; the schedule and choice UI will not need separate hard-coded cases.
Invalid or unsupported unit references fail at scenario initialization
instead of silently spawning broken entities.
