# FVD native-runtime migration inventory

This document records the dependency boundary found on the FVD branch
when work on the native runtime started. It is not a claim that either
installed binary is already Python-free.

## Existing entry points

- Packaged Windows game: `packaging/openage_launcher.cpp` launches an
  embedded `python.exe -m openage game`. Windows deployment must replace
  this launcher before the Python-free acceptance criterion is met.
- Relay: `openage/relay/main.py` remains as a developer compatibility path,
  but `packaging/native_relay` builds a dedicated `openage-relay` program.
  It compiles only the FVD transport, relay, protocol, serialization and
  minimal logging/error utilities without linking `libopenage`, Qt, nyan,
  Python or Cython. Portable command-line tests cover argument parsing and
  process startup; the standalone CMake project is release-focused.
- Game: `packaging/native_game_main.cpp` now creates native `assets/` and
  `cfg/` mounts using `util::fslike::MountedDirectories` and calls the
  existing C++ `run_game` directly. The separate `openage-native` executable
  is installed alongside the existing Python launcher until end-to-end
  graphical gameplay and ABI checks pass. `--help`, `--version`, invalid
  options and missing-asset failures have CTest smoke cases.
- Cvar loading: `CVarManager` accepts a native mode that parses existing
  `set`/`load` directives and guards recursive includes, avoiding the
  Python callback. Legacy Python startup keeps its current behavior.

## Native-library boundary still to resolve

- `libopenage/util/path.h` includes `pyinterface/pyobject.h` and
  accepts a Python object. `libopenage/util/path.cpp` creates Python
  filesystem adapters for non-native paths.
- `libopenage/util/fslike/python.cpp` and
  `libopenage/util/filelike/python.cpp` forward filesystem operations
  through Python callbacks. Native `fslike::Directory` already exists.
- `libopenage/pyinterface/` is built into `libopenage`, rather than
  into a separate optional adapter library.
- Python-owned `Path`/`File` constructors and Python filesystem adapter
  implementations are now isolated in dedicated sources. CMake option
  `FVD_WITH_PYTHON_ADAPTERS=OFF` excludes them from `libopenage`; the normal
  default stays ON for the developer/Cython test harness. Linux CI rebuilds
  the game in both modes and checks native symbols/dependencies.
- `FVDNative` is an explicit install component containing C++ binaries,
  FVD resources, nyan and configs, with a Python-file audit. It does not yet
  deploy native Qt DLLs or guarantee complete pre-converted API assets.
- The full-engine CMake configuration still requires Python/Cython for
  developer tools. The Windows package still contains the Python launcher
  and runtime, and the game client still links the full engine/Qt.
  Only the standalone relay currently has the verified Python/Qt-free build.

## Migration gates

1. Link and run the standalone relay on Windows, macOS and Linux, including
   help/version, invalid options, a real connection and SIGINT/SIGTERM.
   The cross-platform standalone workflow installs just the executable,
   checks imports on Linux/macOS and tests signal-driven shutdown on Unix.
2. Inspect the import tables of the executable **and all recursively loaded
   libraries**. Native source alone does not prove no Python ABI dependency.
3. Separate the Python filesystem/exception adapters and native engine root.
4. Add native game bootstrap, prebuilt asset validation, and package-content
   checks before removing the embedded launcher.

The initial parser unit test is independent of Cython or the engine and can
run before the entire engine dependency boundary is migrated.
