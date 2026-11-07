# Native FVD runtime boundary — historical migration inventory

This originally recorded the **pre-migration** Python/Cython and packaging dependency risks. The baseline below is historical, not a description of current FVD source. See the [remove-legacy-python-surface OpenSpec](../../../openspec/changes/remove-legacy-python-surface/tasks.md) for the change history and [live release gates](FVD_RELEASE_ACCEPTANCE.md) for actual acceptance evidence.

## Current supported boundary

| Component | Current implementation |
| --- | --- |
| Player game | Native `openage-native` C++ executable; `build-fvd game` or `solo` |
| Multiplayer server | Standalone native `openage-relay`, linked without Qt, nyan or Python |
| Engine and tests | C++ `libopenage` plus C++-owned `openage_native_cpp_tests` |
| Runtime data | Tracked `assets/fvd/engine_api` and FVD assets; no converter or generated source cache |
| Packaging | Single FVDNative 7Z archive; strict DLL/ELF/Python/asset integrity audits |
| Python | Optional pure stdlib unit tests and lint tools only, outside CMake/runtime |

No `packaging/openage_launcher.cpp`, `openage.bat`, Python game/relay dispatcher, `libopenage/pyinterface`, Cython bridge, converter or NSIS distribution remains in the supported FVD source. The package uses `share/openage/converted/engine` only as a native nyan runtime mount, not as generated source data.

## Historical failure risks that motivated the change

Previously the Windows launcher embedded Python, game C++ initialization passed through a Python callback, filesystem classes supported Python object adapters, source generation depended on Mako/Cython, and upstream conversion supplied the untracked engine API. The network relay alone could be independently compiled without that stack. Tasks 1–5 moved API assets, tests, C++ entry points and packaging to native ownership and retired those routes.

**Verification distinction:** A source audit or build with disabled Python discovery is not proof that the program builds on a machine with Python physically uninstalled. A Python-free Linux container job and separate Windows clean-machine gate now track that requirement. Likewise a successful package verification is not the same as tested multiplayer/audio/graphical shutdown on two actual client machines.
