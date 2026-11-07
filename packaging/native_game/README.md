# FVD native game: local development and release packaging

`openage-native` (game) and `openage-relay` (relay) are native C++ binaries, using tracked data in `assets/fvd/engine_api`. The normal workflows require no Python or Cython.

## Development: compile and run current sources, without packaging

| Purpose | Windows | Linux |
| --- | --- | --- |
| Build game + relay | `.\build-fvd.cmd` | `bash build-fvd.sh` |
| Play solo | `.\build-fvd.cmd solo` | `bash build-fvd.sh solo` |
| Open multiplayer lobby | `.\build-fvd.cmd game` | `bash build-fvd.sh game` |
| Start native relay | `.\build-fvd.cmd relay` | `bash build-fvd.sh relay` |
| Native tests | `.\build-fvd.cmd test` | `bash build-fvd.sh test` |
| Optional Python tests | `.\build-fvd.cmd python-test` | `bash build-fvd.sh python-test` |
| Release archive | `.\build-fvd.cmd package` | `bash build-fvd.sh package` |

Each native launch builds only its needed targets **incrementally**, then executes the local CMake build-tree binary (Windows: `.bin/msvc-vcpkg/packaging/RelWithDebInfo`; Linux: `build/packaging` or `bin/packaging`). The game uses the live checkout's `assets/` through `--asset-dir`; extra flags are forwarded.

```powershell
.\build-fvd.cmd solo --no-audio --window-mode windowed
.\build-fvd.cmd game --help
.\build-fvd.cmd relay --listen 127.0.0.1 --port 40000
.\build-fvd.cmd test --exact openage::network::tests::lobby_capacity
```

Windows automatically synchronizes local engine/nyan DLLs and points Qt at the configured vcpkg developer runtime. The wrappers avoid unnecessary explicit CMake configuration when the cached developer build is valid; the build system detects CMake source changes. **None of these developer operations creates, searches for, or extracts an archive.**

The native C++ test registry supports `--list`, `--all`, `--all --have-assets`, `--exact NAME` and `--prefix PREFIX`. Optional `python-test` runs a separate stdlib-only unittest module.

## Packaging: explicitly requested, never used to play locally

```powershell
.\build-fvd.cmd package
```

```bash
bash build-fvd.sh package
```

The `package` command configures strict `FVD_NATIVE_RELEASE=ON`, recompiles native targets as needed, and runs CPack to create the **FVDNative** release archive under `dist/`. During release staging it audits bundled native libraries, Qt/QML dependencies, API version, SHA3 engine manifest and SHA-256 asset integrity. Windows additionally audits DLL imports; Linux audits ELF dependencies.

**The packaging command never extracts or launches the archive.** Existing standalone `SmokeTestWindowsArchive.ps1` and `SmokeTestLinuxArchive.sh` remain available for explicit release acceptance, on a clean player machine or deployment/relay environment. Those separate scripts do extract a temporary test copy when intentionally invoked. Manual audio, graphics, two-player and clean-machine checks are still required. Linux archives are not automatically portable across distributions.

## Manual release configuration

For an already configured Windows checkout, the equivalent steps are:

```powershell
cmake -S . -B .bin\msvc-vcpkg -DFVD_NATIVE_RELEASE=ON
cmake --build .bin\msvc-vcpkg --config RelWithDebInfo --target openage_native_game openage_native_relay --parallel 4
cpack --config .bin\msvc-vcpkg\CPackConfig.cmake -C RelWithDebInfo -G 7Z -B dist
```

The source-engine API is committed in `assets/fvd/engine_api`; the installed `converted/engine` path is a runtime mount, not a converter output cache. See [building](../../doc/building.md), [running](../../doc/running.md) and [release acceptance](../../doc/code/network/FVD_RELEASE_ACCEPTANCE.md).
