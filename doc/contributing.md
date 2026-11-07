# Contributing to Fast Villager Defense

FVD is developed against the `FVD` branch of [rocifier/openage](https://github.com/rocifier/openage). It is no longer a general upstream openage converter. Preserve the GPL and contributor attributions in [copying.md](../copying.md).

## Build and test your changes

Build the native game and relay using `.\\build-fvd.cmd build` on Windows or `bash build-fvd.sh build` on Linux. After configuring, use `build-fvd ... test` for the native CTest suite; inspect the C++ registry with `openage_native_cpp_tests --list` and run individual tests using `--exact NAME`. For optional stdlib network-shaper regressions use `build-fvd ... python-test` (Python 3.12+; not used to build or ship the game).

For a release candidate, run `.\\build-fvd.cmd` (Windows) or `bash build-fvd.sh` (Linux) to create and audit the FVDNative 7Z archive. Then verify actual multiplayer, audio and graphical exit paths, including the checklist in [release acceptance](code/network/FVD_RELEASE_ACCEPTANCE.md). The UI can be opened from the most recent archive with `build-fvd game` and the standalone server with `build-fvd relay`.

## Code ownership

Engine, gameplay, network, renderer and audio belong under `libopenage/`; native startup and packaging live under `packaging/`. Versioned runtime data live under `assets/fvd/`; the immutable, checked-in nyan compatibility snapshot is `assets/fvd/engine_api/`. Updating the engine API requires a reviewed source/manifest change. Do not regenerate a source-tree `assets/converted` cache.

Write C++ test coverage for engine changes and register it in `libopenage/testing/native_test_runner.cpp`. Test multiplayer behavior on at least two native game processes. Keep the relay free of Qt, nyan, game assets and Python. Do not add back Python/Cython bindings, a converter, `run.py`, `bin/run`, or generic NSIS/embedded-Python distribution paths.

## Pull requests

Branch from current `FVD`, make focused commits, describe user-visible behavior, link relevant OpenSpec tasks, and attach CTest/CI and manual reproduction evidence. Avoid claiming a clean-machine release gate passed merely because a build configured with Python discovery disabled. Review both Windows and Linux packaging impacts before merging. Code style and licensing guidance live in [code_style](code_style/) and [copying.md](../copying.md).
