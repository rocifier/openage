# FVD native game and relay: building and testing

The end-user Fast Villager Defense (FVD) runtime uses native C++ entry points:
`openage-native` for the game and `openage-relay` for the relay. Neither
runtime loads Python or Cython. The supported native game, relay and C++
test builds do not discover or invoke Python, Mako, Cython or Python development
libraries. Python is optional **only** for separate stdlib developer tests.

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
| Pure Python unit tests | `.\build-fvd.cmd python-test` | `bash build-fvd.sh python-test` |
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
Run `test` to configure the native developer library, compile the C++
test runner, and execute the native CTest suite. C++ test registration now
lives entirely in `libopenage/testing/native_test_runner.cpp` (120 retained
C++ tests, demos and benchmarks), not the Python test list or Cython.
`openage_native_cpp_tests` accepts `--list`, `--all`,
`--all --have-assets`, `--exact NAME`, and `--prefix PREFIX`.
The default `--all` skips asset-bound tests, interactive demos,
performance benchmarks and internal process worker entry points.
You can forward native flags through `build-fvd test --prefix openage::network::tests::`
after the normal CTest run.

Use `python-test` to run stdlib-only tests through `python -m unittest`,
without configuring CMake or compiling extensions. To choose another pure
Python unittest module, supply its dotted module name after `python-test`.
Network process integration tests spawn native C++ workers directly; the obsolete Python/Cython test runner is no longer built.

Optional arguments: skip the extracted-package test with `-SkipSmoke` or `--skip-smoke`.
Windows additionally supports `-ObserveSoloSeconds 0` to disable the
live graphical-process observation.

### Prerequisites

The **build machine** needs compiler/Qt/nyan and the other native
dependencies described in [building.md](../../doc/building.md).
No Python interpreter, Python packages, Python headers, Mako or Cython are
required for the supported CMake build. Optional standalone Python tests
require Python 3.12+; they are run explicitly via `python-test`. The
**shipped native game** does not need Python, pip, setuptools or Cython; the standalone native relay
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
cmake -S . -B .bin\msvc-vcpkg -DFVD_NATIVE_RELEASE=ON
cmake --build .bin\msvc-vcpkg --config RelWithDebInfo --target openage_native_game openage_native_relay --parallel 4
cpack --config .bin\msvc-vcpkg\CPackConfig.cmake -C RelWithDebInfo -G 7Z -B dist
```

If the tracked API changes, update the snapshot and its SHA3-256 manifest as a
reviewed source change. CMake configuration checks those hashes and the API
version; packaging checks the staged files, and native startup validates the
release manifest. The converter's old API exporter remains in Python source until Task 4,
but is no longer used by any native build or test path.

Coordinate C++ templates were materialized as ordinary source files in
`libopenage/coord/`. The complete Python C++ code generator, .pyx/.pxd
sources, adapter implementation and CMake extension builders were removed.
The Python-generated C++ test dispatcher, testlist and Cython test bridge
have been removed. The retained native test registry now covers FVD gameplay,
network replication, rendering, audio and utility tests; any remaining
Python-only test modules are invoked separately.

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

The native runtime and its C++ tests are the supported build path. The
old generic packaging configuration remains to be removed in Task 5.
