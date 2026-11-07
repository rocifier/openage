# FVD source tree

Fast Villager Defense is a **native C++ game and relay** based on the openage engine. Python exists only for optional source-only tests and development helpers; no Cython bindings, Python game launcher or converter are supported.

- `libopenage/`: C++ game simulation, renderer, sound, networking, native tests and test registration.
- `packaging/`: C++ game/relay entry points, native CPack distribution, archive audits, and dedicated relay build.
- `assets/fvd/`: FVD-authored sprites, audio, metadata and **tracked nyan engine API** (`assets/fvd/engine_api`).
- `assets/qml/`, `assets/shaders/`: native render resources.
- `cfg/`: audio and keybind configuration. Converter configuration is no longer present.
- `buildsystem/`: CMake helpers and optional code quality tools.
- `openage/testing/`: standalone pure-Python network shaping and `unittest` checks, not used by native builds.
- `openspec/`: game features and native migration acceptance criteria.
- `doc/`: runtime, build and developer guides; historical upstream engine notes are retained where useful.

Native executables: `openage-native` (game), `openage-relay` (standalone relay), `openage_native_cpp_tests` (developer tests). See [build instructions](building.md), [running](running.md), and [CMake layout](buildsystem.md).

The installed `converted/engine` path is a runtime API mount point mapped from tracked source files, **not** an output of an original-game asset converter.
