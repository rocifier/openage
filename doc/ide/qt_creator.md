# Qt Creator with native FVD

Qt Creator can open the checkout's root `CMakeLists.txt` as a CMake project. Select a C++20 compiler, Qt 6, nyan and the native render/audio dependencies from [building.md](../building.md).

Configure a **developer build** with `FVD_NATIVE_RELEASE=OFF`. Use the targets `openage_native_game`, `openage_native_relay` and `openage_native_cpp_tests`. Do not choose a deleted `run` launcher or a Python interpreter. Run the built executable `packaging/openage-native` (Windows: `packaging/<Config>/openage-native.exe`) with **working directory set to the source checkout** and arguments `--asset-dir assets` (plus `--solo` for local gameplay).

For an end-user install/relocation check, package and smoke-test using `build-fvd.cmd` on Windows or `build-fvd.sh` on Linux, and launch the extracted archive via `build-fvd game` or `build-fvd solo`. Running a developer target alone does not verify the strict release manifest or its native dependency closure.
