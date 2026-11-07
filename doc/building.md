# Building FVD (native C++)

The FVD game, relay and C++ tests are native targets. No Python interpreter, Cython, Mako, NumPy, converter or asset-export step is used when configuring or compiling them.

## Windows — MSVC and vcpkg

Install Visual Studio C++ Build Tools (C++20), CMake, vcpkg native libraries and 7-Zip. See [Windows details](build_instructions/windows_msvc.md).

From the checkout root:

```powershell
.\build-fvd.cmd              # configure, compile, package and smoke-test
.\build-fvd.cmd build        # compile game and relay for development
.\build-fvd.cmd test         # rebuild and run native C++/CTest checks
.\build-fvd.cmd python-test  # optional stdlib-only Python tests
.\build-fvd.cmd game         # open packaged multiplayer lobby
.\build-fvd.cmd solo         # run latest packaged solo game
.\build-fvd.cmd relay        # run latest packaged standalone relay
```

`game`, `solo` and `relay` run the *last packaged* archive and do not rebuild. Run `build-fvd.cmd` after source changes to package them. `-SkipSmoke` skips archive smoke-testing for faster local iterations.

## Linux

Install CMake, Ninja, a C++20 compiler, Qt6, Eigen3, nyan (or enable its CMake fetch), toml11 and the normal native render/audio libraries (Freetype, Fontconfig, HarfBuzz, libepoxy, OpenGL, Opus, Ogg, opusfile, libpng). See [Linux platform instructions](build_instructions/ubuntu.md) and [native packaging](../packaging/native_game/README.md).

```sh
bash build-fvd.sh            # build, package and smoke-test native archive
bash build-fvd.sh build      # incremental developer build
bash build-fvd.sh test       # native game/relay and C++ registry tests
bash build-fvd.sh python-test # optional Python-only regression suite
bash build-fvd.sh game      # open multiplayer lobby from latest archive
bash build-fvd.sh solo --no-audio
bash build-fvd.sh relay --port 40000
```

The native relay alone can be built without Qt or game assets: [standalone relay guide](../packaging/native_relay/README.md).

## CMake and tracked assets

CMake builds `openage_native_game`, `openage_native_relay` and (developer builds) `openage_native_cpp_tests`. Release builds use `-DFVD_NATIVE_RELEASE=ON` and CPack's `FVDNative` component. The authoritative engine API is `assets/fvd/engine_api`, validated at configure time. Its installed path `share/openage/converted/engine` is a **runtime mount point**, not a source-tree conversion cache. Do not generate or write `assets/converted` during builds.

For a source edit, rebuild the corresponding native target before manually launching an executable. The `build-fvd` wrappers build before test/package operations.

## Testing

`build-fvd ... test` builds and runs CTest plus the native C++ test runner. Run an individual test with the built `openage_native_cpp_tests --exact openage::network::tests::messages`, or use `--prefix openage::network::tests::` and `--list`. Asset-dependent tests are opt-in with `--all --have-assets`.

Optional Python is **only** for source-only helpers and network shaper regressions; it needs Python 3.12+ and no third-party runtime packages. `build-fvd ... python-test` runs `python -m unittest -v openage.testing.test_fvd_network_benchmark`. Developers may install `ruff` and `ty` using `uv sync` to run linting.

FVD intentionally has no original-game conversion, embedded Python, Cython binding or legacy `bin/run` command.
