# FVD native game and relay: building and testing

The end-user Fast Villager Defense (FVD) runtime uses native C++ entry points:
`openage-native` for the game and `openage-relay` for the relay. Neither
runtime loads Python or Cython. Python 3.12+, Cython 3.1+ and Python
packages remain **developer/build-time tools** for code generation,
asset conversion, API export and the traditional openage test suite.

## The normal workflow: one command

From an existing configured C++/Qt/CMake development checkout:

**Windows (MSVC/vcpkg):**

```powershell
.\build-fvd.cmd
```

**Linux:**

```bash
bash build-fvd.sh
```

Both wrappers use the tracked engine API snapshot in
`assets/fvd/engine_api/` (version 0.5.0); they never invoke the converter
or create `assets/converted/engine` as a build cache. An absent, incomplete,
outdated or modified API fails validation instead. They incrementally compile
the C++ game and relay, create a **native-only 7Z archive under `dist/`**,
and smoke-test an extracted copy.

On Windows the script discovers 7-Zip and Visual Studio `dumpbin.exe`,
audits every packaged EXE/DLL/Qt plugin and non-system DLL import,
validates SHA-256 assets, tests deliberate asset corruption/restoration,
and observes a solo process for 30 seconds. Normal exit code 0 during the
observation window is a clean exit, not a crash.

On Linux the script audits package contents and ELF dependencies and
checks native CLI, asset integrity, and a bounded headless solo startup.
Linux runtime libraries are currently supplied by a compatible host
distribution; the archive is **not yet independently validated on clean,
different distributions**.

### Launch, test or host without rebuilding

| Action | Windows | Linux |
| --- | --- | --- |
| Package + smoke-test | `.\build-fvd.cmd` | `bash build-fvd.sh` |
| Build native game and relay | `.\build-fvd.cmd build` | `bash build-fvd.sh build` |
| Native tests | `.\build-fvd.cmd test` | `bash build-fvd.sh test` |
| Legacy C++/Python tests | `.\build-fvd.cmd legacy-test` | `bash build-fvd.sh legacy-test` |
| Launch packaged solo game | `.\build-fvd.cmd solo` | `bash build-fvd.sh solo` |
| Run packaged relay | `.\build-fvd.cmd relay` | `bash build-fvd.sh relay` |

Arguments following `solo` or `relay` go directly to the native executable:

```powershell
.\build-fvd.cmd solo --no-audio --window-mode fullscreen
.\build-fvd.cmd relay --listen 127.0.0.1 --port 40000
.\build-fvd.cmd relay -- --help
```

```bash
bash build-fvd.sh solo --no-audio
bash build-fvd.sh relay --listen 127.0.0.1 --port 40000
```

The launch commands use the most recent archive, extracted to `dist/run/`; they do **not** reconfigure or compile.
Run `build` for an incremental native game and relay build without creating or inspecting an archive.
The developer game accepts loose source assets through `--asset-dir assets`; packaged builds retain strict release manifest validation.
Run `test` to reconfigure the developer (non-release) native library, build
the C++-owned test executable and native protocol tests, and execute CTest.
`openage_native_cpp_tests` accepts `--list`, `--all`, `--exact NAME`, and
`--prefix PREFIX`. This initial nine-test registry includes coordinate
operations, native config, network messages, numeric utilities, assertions
and texture parsing. It lives in `libopenage/testing/native_test_runner.cpp`
rather than the Python-generated test dispatcher. More engine tests will be
ported before the old Cython test bridge disappears.
Run `legacy-test` to configure Python adapters, build the developer runner and execute the traditional C++/Python suite.
With no test name it passes `-a` to the legacy runner and executes every test that does not require external game assets.

Optional arguments: skip the extracted-package test with `-SkipSmoke` or `--skip-smoke`.
Windows additionally supports `-ObserveSoloSeconds 0` to disable the
live graphical-process observation.

### Prerequisites

The **build machine** needs compiler/Qt/nyan and the other native
dependencies described in [building.md](../../doc/building.md).
Until subsequent source-tree cleanup tasks land, top-level CMake can still
need the existing Python/Cython developer environment for legacy code
generation and tests, installed with `uv sync` using
[pyproject.toml](../../pyproject.toml). The **shipped native game** does
not need Python, pip, setuptools, or Cython; the standalone native relay
does not even require Qt, nyan or game assets.

On Windows, 7-Zip and the MSVC `dumpbin.exe` are used by smoke tests.
The script loads a local `deps/msvc-env.ps1` if one exists.

For a **relay-only volunteer host**, see
[the minimal standalone relay build](../native_relay/README.md):
it compiles with CMake and a C++20 compiler, without configuring the
full game engine or building Python tools.

## Advanced/manual release steps

These commands are for diagnosing the wrappers, not the normal workflow.
The tracked engine API contains `modpack.toml`, `manifest.toml`,
and 153 nyan files under `assets/fvd/engine_api` (engine version 0.5.0).
The installed runtime mount remains `assets/converted/engine`.
The release manifest is created **during CMake configuration**, and
the player validates every asset SHA-256 before opening the game.

For example, on an already configured Windows checkout from the repo root:

```powershell
cmake -S . -B .bin\msvc-vcpkg -DFVD_WITH_PYTHON_ADAPTERS=OFF -DFVD_NATIVE_RELEASE=ON
cmake --build .bin\msvc-vcpkg --config RelWithDebInfo --target openage_native_game openage_native_relay --parallel 4
cpack --config .bin\msvc-vcpkg\CPackConfig.cmake -C RelWithDebInfo -G 7Z -B dist
```

If the tracked API changes, update the snapshot and its SHA3-256 manifest as a
reviewed source change. CMake configuration checks those hashes and the API
version; packaging checks the staged files, and native startup validates the
release manifest. The legacy API exporter remains temporarily in the tree
for other workflows but is no longer called by either FVD build wrapper.

Coordinate C++ templates were materialized as ordinary source files in
`libopenage/coord/`. Builds no longer invoke Mako for those types.
The old Python test dispatcher and optional legacy test mode still exist
until the remaining native test registrations and fixtures are migrated.

The FVDNative CMake install component stages the native game/relay, C++
engine libraries, Qt/QML runtime, FVD data, shaders, config and tracked
API. Its content audit rejects Python runtime files. Windows packaging
also checks all shipped imports and verifies the actual staged game
assets **before** CPack produces the archive.

## What remains before a public release

The Windows extracted 7Z has passed automated package, import,
integrity/tampering and bounded solo-process smoke tests (2026-11-06).
The final manual acceptance gates remain: actual graphical solo gameplay,
native relay host/guest replication, clean exits and running on a
Windows machine/VM **without Python**. Linux additionally requires
clean-distribution library portability testing. Do not mistake a
successful process smoke test for full gameplay acceptance.

The general openage developer build and existing public/legacy
installer still coexist. The native release candidate does not replace
those until the acceptance gates pass.
