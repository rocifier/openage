# macOS native development build

FVD's game, relay and test runner are native C++ targets. The old Python/Cython build, `./configure` frontend and `bin/run` launcher are not supported.

Install Xcode Command Line Tools and Homebrew native dependencies:

```sh
brew install cmake ninja llvm flex make eigen libepoxy freetype fontconfig harfbuzz opus opusfile libogg libpng toml11 qt6
```

Use Homebrew LLVM for C++20 module scanning (AppleClang may be insufficient), then configure the native developer targets:

```sh
LLVM_PREFIX="$(brew --prefix llvm)"
cmake -S . -B bin -G Ninja -DCMAKE_BUILD_TYPE=Release -DDOWNLOAD_NYAN=YES \
  -DFVD_NATIVE_RELEASE=OFF \
  -DCMAKE_C_COMPILER="$LLVM_PREFIX/bin/clang" \
  -DCMAKE_CXX_COMPILER="$LLVM_PREFIX/bin/clang++"
cmake --build bin --parallel "$(sysctl -n hw.logicalcpu)" \
  --target openage_native_game openage_native_relay openage_native_cpp_tests
ctest --test-dir bin -R '^fvd_native_(game|relay)_' --output-on-failure
```

Only Windows and Linux `build-fvd` scripts currently provide a verified player archive workflow. The macOS recipe above builds developer targets; it is not a claim of a tested portable macOS release. Optional Python tests use `python3 -m unittest -v openage.testing.test_fvd_network_benchmark` from the checkout root and require no third-party Python packages.

See [FVD build instructions](../building.md).
