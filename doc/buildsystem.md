# FVD native build and packaging

FVD uses CMake for the C++ game (`openage_native_game` / `openage-native`), standalone relay (`openage_native_relay` / `openage-relay`), and developer test runner (`openage_native_cpp_tests`). Source-tree CMake does **not** find or run Python, Cython, Mako or an original-game converter.

## Source and dependency owners

`CMakeLists.txt` selects build features, `libopenage/CMakeLists.txt` compiles native engine code, `packaging/native_relay/NativeRelay.cmake` defines the independently buildable network service without Qt/nyan, and `packaging/CMakeLists.txt` registers the game, tests and CPack. Coordinate C++ sources and the C++ test registry are committed, not generated from Python.

The versioned engine API lives under `assets/fvd/engine_api`; CMake validates its SHA3-256 manifest at configure time. There is no converter/bootstrap step or generated source-tree `assets/converted`. The installed `share/openage/converted/engine` is a native nyan **runtime mount** pointing at committed contents.

## Supported package

CPack emits only the `FVDNative` component using `7Z`. On Linux the archive name remains `fvd.7z`; Windows uses the version/platform `-portable` suffix. `packaging/native_game/NativePackage.cmake` owns all FVD assets, native game/relay executables, config, Qt/QML resources and native library staging. Generic `openage.exe`, batch launchers, NSIS, Python embedding and all-component installers are unsupported.

The independent `AuditNativePackage.cmake` validates the installed engine version and SHA3 nyan manifest, verifies **every staged SHA-256 asset**, and rejects Python or generic-launcher artifacts. Windows adds Qt deployment using native tools/PowerShell and a complete `dumpbin` DLL import audit. Linux checks ELF dependencies on an extracted archive. Native and optional Python tests run separately.

## Verification and environments

- Development: `build-fvd build` then `build-fvd test`.
- Player package: `build-fvd` (no arguments) to build, package and smoke-test.
- Multiplayer UI: `build-fvd game`; solo: `build-fvd solo`; server: `build-fvd relay`.
- Optional pure-Python test: `build-fvd python-test`.

See [build instructions](building.md), [testing](code/testing.md), [source tree](project_structure.md), and [live release acceptance](code/network/FVD_RELEASE_ACCEPTANCE.md). The Python-absent Linux CI container is a separate verification gate; Windows clean-machine acceptance and human two-client/audio/exit tests remain platform-specific.
