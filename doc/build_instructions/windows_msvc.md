# Building on Windows (MSVC)

This page covers the **native Fast Villager Defense (FVD) game and relay** and
the optional Python-based openage developer tools. The packaged native game
and relay do **not** require Python or Cython on the player's computer.

## Windows development machine setup

Install:

- [Visual Studio C++ Build Tools](https://aka.ms/vs/17/release/vs_BuildTools.exe)
  (MSVC x64 compiler and Windows SDK).
- [CMake](https://cmake.org/download/), including CPack.
- [vcpkg](https://github.com/microsoft/vcpkg) and the required native
  libraries; configure CMake using the vcpkg toolchain.
- [7-Zip](https://www.7-zip.org/) for extracted-archive smoke testing
  (normally at `C:\Program Files\7-Zip\7z.exe`).
- Flex/win_flex as needed to compile
  [nyan](https://github.com/SFTtech/nyan/blob/master/doc/building.md#windows).

For a vcpkg-based build, the primary native dependencies are:

```powershell
vcpkg install dirent eigen3 fontconfig freetype harfbuzz libepoxy libogg libpng opus opusfile qtbase qtdeclarative qtmultimedia toml11
```

Use your configured Windows triplet (for example, `x64-windows`).
Qt must support PNG decoding; when opting out of defaults, enable
`qtbase[png]`. Optional Vulkan support uses `vcpkg install vulkan`.

**Python installation is not required to run the native FVD archive.**
However, the *full source-tree developer build* and first engine nyan API
export still use Python 3.12+, Cython 3.1+ and other build-time tools.
For that workflow, install a normal 64-bit Python interpreter and use
`uv sync` from the checkout root to obtain dependencies from
[`pyproject.toml`](../../pyproject.toml). You do not need to manually
install old pip package lists, the Python embeddable distribution, or
special Python debug binaries for native FVD packaging.

## Build and package native FVD (recommended)

Once the CMake/MSVC/vcpkg development checkout is configured, run from
the repository root:

```powershell
.\build-fvd.cmd
```

This wraps API export (only when missing), native-only CMake configuration,
incremental game/relay compilation, packaging to `dist\*.7z` and
extracted-archive tests, including DLL/Python import audits and a bounded
solo-process check. It loads `deps/msvc-env.ps1` automatically **if that
local environment script exists**.

No repeated CMake or CPack flags are needed. On subsequent builds, the
exported API is reused unless explicitly refreshed.

### Run and test the packaged native binaries

```powershell
.\build-fvd.cmd test
.\build-fvd.cmd solo
.\build-fvd.cmd solo --no-audio --window-mode fullscreen
.\build-fvd.cmd relay
.\build-fvd.cmd relay --listen 127.0.0.1 --port 40000
```

The `solo` and `relay` modes reuse the latest archive under `dist/` without
recompiling anything. Add `--help` after a mode to see native command-line
options. For example, `.\build-fvd.cmd relay -- --help`.

`test` runs native game/relay CTest cases and validates an available
archive. To refresh a changed exported API, use
`.\build-fvd.cmd -ReexportApi`. To package without smoke testing, use
`.\build-fvd.cmd -SkipSmoke`.

The relay is a standalone C++ service and does not require Python, Qt,
nyan, game media or the rendering engine. Volunteer hosts can build just
the relay using the
[standalone relay guide](../../packaging/native_relay/README.md).

**Release status:** the native Windows extracted archive has passed
automated CLI, integrity, DLL/import and bounded solo checks. Manual
graphical, two-client multiplayer, clean exit and clean Windows machine
verification are still acceptance requirements. The normal public player
distribution is not replaced yet.

## Optional: general openage developer build

Use this only when you need the traditional Python/Cython developer runner,
conversion tools or the full source-tree test suite. Python/Cython remain
build-time/developer requirements here; they are not player requirements.

Set up the Python environment:

```powershell
uv sync
```

Configure the existing vcpkg toolchain for MSVC, adapting the path to your
installation:

```powershell
cmake -S . -B .bin\msvc-vcpkg -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake" -DFVD_WITH_PYTHON_ADAPTERS=ON -DFVD_NATIVE_RELEASE=OFF
cmake --build .bin\msvc-vcpkg --config RelWithDebInfo --parallel 4
```

Set `-DPython3_EXECUTABLE=...` to the venv interpreter if CMake
does not select it automatically. Additional nyan setup may need
`-DDOWNLOAD_NYAN=YES` and `-DFLEX_EXECUTABLE=...`.

The Python-enabled `run.exe` is for legacy developer commands. Do not
use `run.exe relay` or an embedded-Python launcher for a Python-free
distribution; use `build-fvd.cmd relay` instead.

For advanced native packaging and troubleshooting, see
[the FVD packaging guide](../../packaging/native_game/README.md).
The historical generic `cpack`/NSIS packaging route may still include
developer Python components; it is **not** the FVD-native release route.
