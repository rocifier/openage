# Debugging native FVD

No Python launcher is involved. Debug the C++ game executable `openage-native`, relay `openage-relay`, or developer test runner `openage_native_cpp_tests`.

## Linux

```sh
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DDOWNLOAD_NYAN=YES -DFVD_NATIVE_RELEASE=OFF
cmake --build build/debug --target openage_native_game openage_native_relay openage_native_cpp_tests
gdb --args build/debug/packaging/openage-native --asset-dir assets --solo
```

To debug multiplayer, omit `--solo` and start a separate relay (`build/debug/packaging/openage-relay --port 40000`). The developer game can use checked-in source assets; use a packaged archive for integrity/relocation problems. Test failures can be reproduced with `build/debug/packaging/openage_native_cpp_tests --exact openage::network::tests::lobby_capacity`.

## Windows

Use Visual Studio's native debugger against the `openage_native_game` or `openage_native_relay` CMake targets. Pass `--asset-dir assets` while running from the checkout; set the working directory to the repository root. Build with the MSVC `Debug` configuration and native DLL dependencies from vcpkg. The native C++ test runner supports `--exact`, `--prefix` and `--all --have-assets`.

Optional GDB pretty-printers in `etc/openage.gdbinit` are developer conveniences; the native FVD program itself never loads Python. See [build requirements](building.md) and [release verification](code/network/FVD_RELEASE_ACCEPTANCE.md).
