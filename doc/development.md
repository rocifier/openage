# Developing Fast Villager Defense

FVD is an independent C++ game fork; the current focus is reliable multiplayer, builder/hunter gameplay, graphics, sound and a lightweight native game/relay distribution. Proposals and staged work are maintained in [OpenSpec](../openspec/).

Start with [contributing](contributing.md), [native build steps](building.md), [testing](code/testing.md) and [source ownership](project_structure.md). The game/relay are the supported executables; use `build-fvd game` for the lobby, `build-fvd solo` for local gameplay and `build-fvd relay` for a host service. Test changes through CTest, then real two-client gameplay.

Preserve compact PRs, verify C++ network and audio behavior, and update FVD-owned `assets/fvd` files directly. The tracked `engine_api` snapshot is compatibility-critical: change it with an explicit version/manifest review. Automatic conversion from original Age of Empires installations, upstream Python launchers and generic installer compatibility have been deliberately retired. Optional pure Python tests and linting remain developer-only.

For release readiness, see [FVD acceptance matrix](code/network/FVD_RELEASE_ACCEPTANCE.md). Automated tests cannot replace actual clean-machine, audible effect or graphical multiplayer verification.
