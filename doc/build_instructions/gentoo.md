# Gentoo FVD native build (experimental)

The historical upstream `openage` overlay package does **not** describe or install the current FVD native distribution. Configure this fork from source using CMake, a C++20 compiler, Ninja, Qt6, nyan, toml11 and the renderer/audio development libraries. Python, Cython and asset conversion are not native dependencies.

```sh
cmake -S . -B build -G Ninja -DFVD_NATIVE_RELEASE=OFF -DDOWNLOAD_NYAN=YES
cmake --build build --parallel
ctest --test-dir build -R '^fvd_native_(game|relay)_' --output-on-failure
```

The supported native release archive wrappers currently target Windows and Linux. See [building](../building.md).
