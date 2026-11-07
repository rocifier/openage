# FreeBSD native FVD (experimental)

The old openage Python/Cython and original-game conversion instructions are no longer applicable. FVD requires a C++20 compiler, CMake, Ninja, Qt6, nyan, Eigen3, toml11 and native render/audio libraries. FreeBSD packaging has not yet passed the Windows/Linux release acceptance gates.

Start by installing matching native packages (names may vary with FreeBSD releases):

```sh
sudo pkg install cmake ninja llvm eigen3 harfbuzz freetype2 fontconfig opus opusfile png qt6 toml11
```

Configure the native targets directly using `cmake -S . -B build -G Ninja -DFVD_NATIVE_RELEASE=OFF -DDOWNLOAD_NYAN=YES` and build with `cmake --build build`. No Python or `./configure` wrapper is needed. See [building](../building.md).
