# FVD developer documentation

Fast Villager Defense is a native C++ RTS fork of openage. The supported runtime consists of `openage-native`, `openage-relay`, committed FVD assets and the tracked nyan engine API. **Original Age of Empires game conversion is not supported.**

Start here:

- [Build FVD on Windows or Linux](building.md) — prerequisites, CMake targets and native archive wrappers
- [Launch game, multiplayer and relay](running.md)
- [Test game, relay and optional Python tooling](code/testing.md)
- [Source-tree ownership](project_structure.md) and [native CMake packaging](buildsystem.md)
- [FVD assets and engine API](assets.md)
- [Package/release verification](../packaging/native_game/README.md), [current acceptance status](code/network/FVD_RELEASE_ACCEPTANCE.md)
- [Multiplayer design](code/network/README.md), [Windows multiplayer acceptance](code/network/WINDOWS_MULTIPLAYER_ACCEPTANCE.md)
- [Contributor workflow](contributing.md), [debugging](debug.md) and [IDE configuration](ide/README.md)

Optional Python unit tests are source-only development tools; CMake, game/relay builds and player archives do not require an interpreter, Cython, Mako or Python packages. Historical upstream openage engine documentation may still describe defunct components and should **not** be treated as an FVD build or installation guide.
