# Windows — native FVD (MSVC/vcpkg)

FVD uses the native C++ game, relay and C++ test runner. Python, Cython, Mako, compiled Python extensions and API export are not build or runtime dependencies.

## Prerequisites

Install Visual Studio C++ Build Tools (C++20 toolchain), CMake/CPack, [vcpkg](https://github.com/microsoft/vcpkg), 7-Zip only for explicit release archive inspection and the native dependencies. For the usual vcpkg triplet, start with:

```powershell
vcpkg install dirent eigen3 fontconfig freetype harfbuzz libepoxy libogg libpng opus opusfile qtbase qtdeclarative qtmultimedia toml11
```

Select the vcpkg toolchain in your MSVC CMake environment; `deps/msvc-env.ps1` is sourced by `build-fvd.ps1` if available. The required nyan library can be built as a CMake dependency. These are native C++ dependencies, not Python packages.

## Build, package, run and test

```powershell
.\build-fvd.cmd                  # incremental local developer build
.\build-fvd.cmd build            # incremental development build
.\build-fvd.cmd test             # C++ test runner and CTest
.\build-fvd.cmd game            # incrementally build and launch local lobby
.\build-fvd.cmd solo --no-audio # incrementally build and launch solo
.\build-fvd.cmd relay --port 40000
.\build-fvd.cmd python-test      # optional Python unittest, no native dependency
.\build-fvd.cmd package          # optional release 7Z, no archive extraction
```

The `game`, `solo` and `relay` modes **build the current native target and execute it from the development build tree** using the tracked source assets. No 7-Zip, archive or extraction is needed for local play. Only `package` creates a distributable 7Z in `dist/`; archive testing/deployment remains a separate task. Use `--` to forward ambiguous native arguments.

The engine API is committed at `assets/fvd/engine_api` and validated at configuration. The installed mount path `share/openage/converted/engine` is not a source-tree conversion cache. Do not run original-game converters; FVD does not support them.

For options and details see [building](../building.md), [running](../running.md), [game packaging](../../packaging/native_game/README.md) and [relay-only build](../../packaging/native_relay/README.md).
