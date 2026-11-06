# FVD sounds

Real recordings in this folder always take priority over fallback sounds.

## Temporary synthesised placeholders

Until every effect has a recording, the **native C++ audio engine**
generates temporary samples in memory for missing effect cues and
lobby/match music. This works for the Python-free game without running a
generator or writing files, on Windows and Linux.

The older Python developer launcher also generates placeholders into a
per-user cache using `openage/game/placeholder_sounds.py`. Cached sounds
still work; the C++ fallback fills only any remaining gaps.

Sound priority is: **real file in this folder → cached placeholder (if
available) → native C++ synthesized fallback**. The game log reports the
number of cues generated natively. `--no-audio` disables all sound.

Formats: Opus (`.opus`) or WAV (`.wav`, 8/16/24-bit or float, mono or stereo,
any sample rate). Add variants with a number, e.g. `hit.opus`, `hit_2.opus`,
`hit_3.opus`; one is picked at random each time.

## Effects

| File stem           | When                                                     | Heard by          |
|---------------------|----------------------------------------------------------|-------------------|
| `hit`               | a horse, Builder or tower loses HP                       | anyone who sees it |
| `death_horse`       | a horse dies                                             | anyone who sees it |
| `death_builder`     | a Builder dies                                           | anyone who sees it |
| `death_tower`       | a tower is destroyed or removed                          | anyone who sees it |
| `foundation_placed` | you place one of your own foundations                    | you only          |
| `tower_built`       | a tower you can see finishes construction                | anyone who sees it |
| `order`             | you give your own units a move/attack/patrol order       | you only          |
| `hunter_unleashed`  | the Hunter's horses start spawning (with a big banner)   | everyone          |
| `flare`             | a Builder flares (the "gling")                           | all Builders      |

World sounds are full volume on screen, fade out just beyond the screen edge,
and pan left/right with their position. Bursts are rate-limited per sound.

## Music

`music/lobby.opus` plays in the lobby and `music/match.opus` during a match
(also `.wav`, and numbered variants). Tracks loop and crossfade over 2 seconds.
Prefer Opus for music: it is decoded while playing instead of all at once.

## Settings

`cfg/audio.oac` sets `AUDIO_MASTER_VOLUME`, `AUDIO_MUSIC_VOLUME` and
`AUDIO_EFFECTS_VOLUME` (0-100). M mutes during a match. `--no-audio` never
opens an audio device.
