# Running FVD

The supported executables are **`openage-native`** (game) and **`openage-relay`** (standalone multiplayer relay). The old Python `bin/run` dispatcher and original-game conversion modes have been retired.

## Packaged game and relay

Windows:

```powershell
.\build-fvd.cmd           # first create a fresh archive
.\build-fvd.cmd solo --no-audio
.\build-fvd.cmd relay --listen 0.0.0.0 --port 40000
```

Linux:

```sh
bash build-fvd.sh        # first create a fresh archive
bash build-fvd.sh solo --no-audio
bash build-fvd.sh relay --listen 0.0.0.0 --port 40000
```

These modes reuse the latest verified archive in `dist/`; they do not automatically compile source changes. To play current edits, build/package again first. For available flags, invoke the native game or relay with `--help` (for example `build-fvd.cmd relay -- --help`).

## Development and tests

```sh
bash build-fvd.sh build
bash build-fvd.sh test
bash build-fvd.sh python-test   # optional, source-only Python unittest
```

The native C++ runner `openage_native_cpp_tests` supports `--list`, `--exact NAME`, `--prefix PREFIX` and `--all [--have-assets]`. It is built by the test command and is not in the player archive.

The authoritative FVD data and tracked engine API are under `assets/fvd/`. Players need no original AoE installation, conversion step or generated source-tree asset cache. For controls and gameplay see [FVD asset guide](../assets/fvd/README.md).
