# FVD native build system

FVD uses CMake for the C++ game (`openage_native_game`), C++ relay (`openage_native_relay`), native test runner (`openage_native_cpp_tests`), CTest and an audited FVDNative archive. Use [build-fvd](building.md) to configure/build the common Windows and Linux targets.

## Native sources

The source tree includes committed coordinate C++ files, a C++-owned test registry and the tracked nyan API at `assets/fvd/engine_api`. CMake validates this data but does not export or regenerate it. No build target invokes Cython, Mako, Python code generation, or the original-game converter.

## Builds and packages

`CMakeLists.txt` configures the native engine and tests. `libopenage/CMakeLists.txt` owns engine source/dep definitions; `packaging/CMakeLists.txt` owns the game, relay, tests and CPack entry point. The independent relay may be configured from `packaging/native_relay` without Qt or nyan. `FVD_NATIVE_RELEASE=ON` excludes developer test objects from the release engine library.

`packaging/native_game/NativePackage.cmake` installs tracked FVD assets, maps the API to the existing runtime mount `converted/engine`, hashes the source inputs and audits the resulting native package. Source `assets/converted` is *not* a build-generated cache.

## Python boundary

Python is not a native build dependency. The optional `python-test` command runs `unittest` directly from the source tree; `ruff` and `ty` are optional checks. There is no Python dispatcher, Cython bridge, Python installer or `bin/run` target.
