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
| `arrow_shot`        | an arrow is launched                                     | anyone who sees it |
| `tower_arrow_shot`  | a Builder or Hunter tower launches an arrow              | anyone who sees it |
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

Both native arrow cues have a whistle that rises as the arrow approaches and falls as it passes.
A short, bright rush of air peaks just after the whistle turns and fades into a softer wake.
The cavalry archer's shot is generally quicker and higher than the tower's.
Each has seven variations, ranging from short, airy passes to longer whistles, with some shots quieter than others.

## Procedural reference analysis

The native fallbacks are original mathematical synthesis, not embedded or sampled recordings.
The following CC0 clips were analysed as references for event timing, spectral balance and decay only.
They are not distributed with FVD and real recordings in this directory still override every generated cue.

| Cue | Reference | Characteristics reproduced procedurally |
|-----|-----------|-----------------------------------------|
| `hit` | [Sword Hit by qubodup](https://freesound.org/people/qubodup/sounds/442769/) | Near-instant bright transient, then a short inharmonic tail. |
| `death_horse` | [horse's whinny by Kubuzz](https://freesound.org/people/Kubuzz/sounds/347036/) | Breathy onset, formant-rich rise, vibrato and falling call. |
| `death_builder` | [Male Screams [1] by marc3122](https://freesound.org/people/marc3122/sounds/556976/) | Brief irregular voiced/noisy burst and a separate body impact. |
| `death_tower` | [Bricks/Stones/Rocks/Gravel Falling by iwanPlays](https://freesound.org/people/iwanPlays/sounds/567249/) | Low initial rumble followed by diminishing, irregular debris impacts. |
| `foundation_placed` | [Wood Impact by michorvath](https://freesound.org/people/michorvath/sounds/269717/) | A compact cluster of midrange wooden knocks. |
| `tower_built` | [Fanfare by ricniclas](https://freesound.org/people/ricniclas/sounds/449069/) | A short bright construction strike and ascending fanfare. |
| `order` | [UI click by Ranner](https://freesound.org/people/Ranner/sounds/488534/) | Low confirmation pulse followed by a higher, brief tick. |
| `hunter_unleashed` | [war horn.wav by adharca](https://freesound.org/people/adharca/sounds/539956/) | Breathy two-note brass call with a low supporting swell. |
| `flare` | [Bell Chime Alert by plasterbrain](https://freesound.org/people/plasterbrain/sounds/419493/) | Quick inharmonic chime with decaying high partials. |

The clips were selected for clear CC0 licensing and strong community ratings when this synthesis was written.
No source waveform, derivative recording or third-party asset is present in the game.

## Audition arrow sounds

Build the developer test runner, then run the listening demo directly from the repository root:

```powershell
cmake --build .bin\msvc-vcpkg --config RelWithDebInfo --target openage_native_cpp_tests -j 4
& .\.bin\msvc-vcpkg\packaging\RelWithDebInfo\openage_native_cpp_tests.exe --exact openage::audio::tests::fvd_arrow_sound_demo
```

It plays three isolated cavalry shots and a short cavalry volley, then repeats that sequence for towers.
It uses the game's audio loader, cue volumes, variant selection and mixer, without music, impacts or camera attenuation.
Recordings in this folder still override synthesis; the older placeholder cache is excluded so synth edits are audible immediately after rebuilding.
This listening demo is opt-in and is skipped by `--all`, including `--all --have-assets`.
See [native test commands](../../../doc/code/testing.md) for the Linux runner.

## Audition other effect sounds

Run the same executable with `--exact openage::audio::tests::fvd_effect_sound_demo`.
It plays one generated variant of every non-arrow effect with enough silence to hear its tail.
Like the arrow demo, this is opt-in and needs a working audio output device.

## Music

`music/lobby.opus` plays in the lobby and `music/match.opus` during a match
(also `.wav`, and numbered variants). Tracks loop and crossfade over 2 seconds.
Prefer Opus for music: it is decoded while playing instead of all at once.

## Settings

`cfg/audio.oac` sets `AUDIO_MASTER_VOLUME`, `AUDIO_MUSIC_VOLUME` and
`AUDIO_EFFECTS_VOLUME` (0-100). M mutes during a match. `--no-audio` never
opens an audio device.
