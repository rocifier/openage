# IDE development for native FVD

Configure the checkout root with CMake and a C++20 toolchain. IDEs should build `openage_native_game`, `openage_native_relay` and `openage_native_cpp_tests`, not the removed Python launcher. For a developer game run, provide `--asset-dir assets` and set the working directory to the source checkout; packaged games use strict manifest verification.

- [Visual Studio Code](vscode.md) — native build/test tasks and source navigation.
- [Qt Creator](qt_creator.md) — native CMake game/relay targets.
- [Emacs](emacs.md) — general editing support; any Python language-server configuration is optional and is not required for building FVD.

Use `build-fvd game` for a packaged multiplayer lobby and `build-fvd solo` for local play. The [native build guide](../building.md) describes system dependencies and the [release acceptance checklist](../code/network/FVD_RELEASE_ACCEPTANCE.md) describes required verification.
