# Building the project

There are two workflows in this repository: the **native Fast Villager Defense
(FVD) game/relay release**, and the **general openage developer build**. They
have different Python requirements. Choose the one you need instead of
installing all developer tools just to play or host FVD.

## FVD native build: quickest path

The shipped native FVD game and relay contain **no Python interpreter, Python
modules, or Cython extensions**. Python/Cython can still be used *on the build
machine* for CMake code generation, the first engine API export and the legacy
developer tools. A release candidate does not automatically mean that all
graphical/multiplayer or clean-machine acceptance tests have passed.

### Windows

Prerequisites: an existing configured MSVC/vcpkg checkout, CMake and
[7-Zip](https://www.7-zip.org/), plus the native libraries described in the
[Windows setup instructions](build_instructions/windows_msvc.md). The script
automatically loads `deps/msvc-env.ps1` if it exists.

From the repository root:

```powershell
.\build-fvd.cmd
```

The command reuses a compatible exported engine API (or exports it once using
developer tools), incrementally builds the Python-free game and relay, creates
`dist\*.7z` and audits an extracted archive. The Windows archive has passed its
CLI, asset-integrity, DLL-import and bounded solo-process smoke tests. Manual
graphical/multiplayer and Python-free clean-machine tests are still required
before replacing the public distribution.

### Linux

Install CMake, Ninja, a C++20 compiler, native Qt/nyan/audio/render development
libraries and the dependencies applicable to your distribution below. A fresh
build may use Python/Cython *during compilation/API export*, but those are not
shipped with the native game.

```bash
bash build-fvd.sh
```

The command generates a Linux native release candidate in `dist/` and runs
extracted-archive and headless startup checks. **Linux packages currently
depend on compatible host-provided libraries**; they are not yet a tested
universal portable distribution.

### Build, test, play or host

The same six commands work on both systems:

| Purpose | Windows | Linux |
| --- | --- | --- |
| Build, package and smoke-test | `.\build-fvd.cmd` | `bash build-fvd.sh` |
| Build native game and relay | `.\build-fvd.cmd build` | `bash build-fvd.sh build` |
| Run native tests | `.\build-fvd.cmd test` | `bash build-fvd.sh test` |
| Run legacy C++/Python tests | `.\build-fvd.cmd legacy-test` | `bash build-fvd.sh legacy-test` |
| Launch packaged solo game | `.\build-fvd.cmd solo` | `bash build-fvd.sh solo` |
| Run packaged relay | `.\build-fvd.cmd relay` | `bash build-fvd.sh relay` |

Game/relay options can follow the mode and are forwarded to the executable:

```powershell
.\build-fvd.cmd solo --no-audio --window-mode fullscreen
.\build-fvd.cmd relay --listen 0.0.0.0 --port 32456
```

```bash
bash build-fvd.sh solo --no-audio
bash build-fvd.sh relay --listen 0.0.0.0 --port 32456
```

The `build` command configures and builds the native game and relay for development without creating or inspecting an archive.
Its game executable accepts loose source assets through `--asset-dir assets`, while `package` retains release manifest enforcement.
The `solo` and `relay` commands reuse the latest archive in `dist/` without rebuilding.
The `test` command runs the native game/relay CTest cases.
The `legacy-test` command switches the build directory to the Python-enabled developer configuration, builds it and runs the traditional C++/Python suite.
A test name can follow the command to run one test, for example `.\build-fvd.cmd legacy-test network::tests::session`.

Optional developer switches include `-ReexportApi` on Windows or
`--reexport-api` on Linux to refresh a changed API, and `-SkipSmoke` /
`--skip-smoke` to package without smoke testing.

For details and advanced/manual commands, see
[the native FVD build instructions](../packaging/native_game/README.md).
For volunteer relay-only hosts, the
[standalone relay instructions](../packaging/native_relay/README.md)
build with **CMake and C++20 only**: no Python, Qt, nyan or game assets.

## Dependencies and development tools

### Needed to build the native game

- **Compiler and build tools:** a C++20-capable compiler, CMake (3.16 or
  newer), and a suitable build generator (Ninja on fresh Linux builds).
  On Windows use Visual Studio C++ build tools and vcpkg.
- **Native engine libraries:** Qt 6.2+ (Core, Quick, QuickControls,
  Multimedia), nyan, Eigen3, libepoxy, OpenGL 3.3+, libpng, FreeType,
  Fontconfig, HarfBuzz, Opus, Ogg, opusfile and toml11. System fonts and
  platform-native runtime libraries are also required. Vulkan, ncurses,
  backtrace/profiling helpers and other features may be optional.
- **nyan:** build it separately or let CMake fetch it
  (`-DDOWNLOAD_NYAN=YES`); see [nyan](https://github.com/SFTtech/nyan).
  Its build prerequisites may include Flex.
- **FVD assets:** the release needs the FVD modpack and the exported
  version-compatible engine nyan API; the one-command scripts prepare this
  automatically when developer tools are available.

A running, properly packaged **native FVD player or relay does not require
Python, Cython or any pip-installed packages**.

### Only needed for Python-backed developer/build-time workflows

The legacy openage Python entry point, conversion/codegen and the first FVD
API export still use **Python 3.12+ and Cython 3.1+** plus Python packages.
The authoritative, platform-aware package list is maintained in
[`pyproject.toml`](../pyproject.toml), including Mako, setuptools, NumPy,
Pillow, lz4, Pygments and tomli-w, with Windows-only pyreadline3.
Do not install the whole list separately just to run a native FVD archive.

For this developer workflow, create the environment once:

```bash
uv sync
```

On Unix, point the legacy developer CMake configuration at
`.venv/bin/python` when required:

```bash
./configure --download-nyan -- -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
```

On Windows the virtual environment interpreter is `.venv\Scripts\python.exe`.
Tools such as Ruff, ty, clang-tidy and documentation generators are for
developer checks and are **not** native player runtime dependencies.

### Platform dependency installation

These pages describe how to install compiler and native libraries.
Their Python package instructions, where present, apply to **developer and
conversion workflows**, not the shipped native player.

- [Ubuntu](build_instructions/ubuntu.md)
- [Debian](build_instructions/debian.md)
- [Fedora](build_instructions/fedora.md)
- [openSUSE](build_instructions/opensuse.md)
- [macOS (general openage development)](build_instructions/macos.md)
- [Arch Linux](build_instructions/arch_linux.md)
- [FreeBSD](build_instructions/freebsd.md)
- [Gentoo](build_instructions/gentoo.md)
- [Nix/NixOS](build_instructions/nix.md)
- [Microsoft Windows](build_instructions/windows_msvc.md)

## General openage source-tree development

The broader openage project still consists of the C++ engine, Python
developer package and Cython bindings. Its traditional build remains
available, independently of the FVD native release:

```bash
./configure --download-nyan
make
make test
```

Use `make run` or `cd bin && ./run` to launch the traditional developer
entry point. Conversion and tests through `run` still depend on the
Python-enabled developer configuration. For details of CMake internals see
[`doc/buildsystem.md`](buildsystem.md).

### Packaging and release acceptance

The default `build-fvd` scripts create *native FVD release candidates* and
do not publish or replace the project's existing package. Verify actual
graphics, solo play, lobby/relay multiplayer and clean exit on an environment
without Python before declaring the native player release complete.

For generic openage distribution packagers, the traditional CMake install
path still exists. Use `cmake --install` and your package manager's staging
mechanism when deliberately packaging the generic developer build. The
`FVDNative` CMake component is distinct and excludes Python.

## Troubleshooting

- **Missing native library/header:** install the corresponding platform
  development package and reconfigure. On Linux, a 7Z produced on one
  distribution is not guaranteed ABI-compatible with a different one.
- **Python/Cython import error during build:** this concerns the
  Python-enabled developer/asset-export step. It does not indicate that
  the finished FVD game needs Python. Run a compatible Python developer
  build or reuse an already exported engine API.
- **CMake cannot find a dependency:** inspect the cache with `ccmake` /
  `cmake-gui` and set the appropriate package path.
- **Changed native-release flags:** CMake may need to recompile part of
  `libopenage` when toggling between developer adapters and native mode.
  The one-command scripts handle the configuration automatically.
- **Tests fail:** run `build-fvd ... test` for native game/relay tests or
  `build-fvd ... legacy-test` for the traditional C++/Python suite.

For additional help see [troubleshooting](troubleshooting.md).
