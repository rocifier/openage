# Nix and FVD (experimental)

This is a native-only C++ FVD fork. Historical Nix `openage` flakes and the old Python dispatcher are not release-validated FVD workflows. The supported Windows/Linux native archive instructions are in [building.md](../building.md).

For a Nix development shell, provision a C++20 compiler, CMake, Ninja, Qt6, nyan (or allow the CMake fetch), Eigen3, toml11 and native render/audio libraries. Configure CMake directly:

```sh
cmake -S . -B build -G Ninja -DFVD_NATIVE_RELEASE=OFF -DDOWNLOAD_NYAN=YES
cmake --build build --parallel --target openage_native_game openage_native_relay openage_native_cpp_tests
ctest --test-dir build -R '^fvd_native_(game|relay)_' --output-on-failure
```

Python/Cython and original-game asset conversion are not required. This is a developer build; portable Nix distribution is not yet verified.
