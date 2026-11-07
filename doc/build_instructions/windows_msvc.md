# Windows — native FVD (MSVC/vcpkg)

FVD uses the native C++ game, relay and C++ test runner. Python, Cython, Mako, compiled Python extensions and API export are not build or runtime dependencies.

## Prerequisites

Install Visual Studio C++ Build Tools (C++20 toolchain), CMake/CPack, [vcpkg](https://github.com/microsoft/vcpkg), 7-Zip (for archive smoke tests) and the native dependencies. For the usual vcpkg triplet, start with:

```powershell
vcpkg install dirent eigen3 fontconfig freetype harfbuzz libepoxy libogg libpng opus opusfile qtbase qtdeclarative qtmultimedia toml11
```

Select the vcpkg toolchain in your MSVC CMake environment; `deps/msvc-env.ps1` is sourced by `build-fvd.ps1` if available. The required nyan library can be built as a CMake dependency. These are native C++ dependencies, not Python packages.

## Build, package, run and test

```powershell
.\build-fvd.cmd                  # native build, 7Z archive and smoke checks
.\build-fvd.cmd build            # incremental development build
.\build-fvd.cmd test             # C++ test runner and CTest
.\build-fvd.cmd solo --no-audio # launch latest packaged solo game
.\build-fvd.cmd relay --port 40000
.\build-fvd.cmd python-test      # optional Python unittest, no native dependency
```

The game `solo` and relay modes use the **latest packaged archive** in `dist`. Rebuild/package after source changes if you want to play the updated code. Pass `-SkipSmoke` for a quicker development archive. Use `--` to forward ambiguous native arguments to the packaged programs.

The engine API is committed at `assets/fvd/engine_api` and validated at configuration. The installed mount path `share/openage/converted/engine` is not a source-tree conversion cache. Do not run original-game converters; FVD does not support them.

For options and details see [building](../building.md), [running](../running.md), [game packaging](../../packaging/native_game/README.md) and [relay-only build](../../packaging/native_relay/README.md).
