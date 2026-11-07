# Building FVD (native C++)

FVD game, relay and C++ tests are native; no Python, Cython or original-game conversion is required.

## Windows (MSVC/vcpkg)

From the checkout root, with the dependencies from [Windows setup](build_instructions/windows_msvc.md):

```powershell
.\build-fvd.cmd                   # incremental local game + relay build (default)
.\build-fvd.cmd solo              # build latest game, then play solo
.\build-fvd.cmd game              # build latest game, then open lobby
.\build-fvd.cmd relay --port 40000 # build latest relay, then host
.\build-fvd.cmd test              # build native tests and run CTest
.\build-fvd.cmd python-test       # optional Python-only tests
.\build-fvd.cmd package           # explicitly create a 7Z release archive
```

Local game launches use the actual build-tree executable in `.bin/msvc-vcpkg/packaging/RelWithDebInfo` and checked-in `assets/`. The wrapper uses the configured vcpkg Qt runtime and synchronizes local engine DLLs. No archive or 7-Zip is needed for local play.

## Linux

See [Ubuntu setup](build_instructions/ubuntu.md) for native dependencies.

```bash
bash build-fvd.sh                 # incremental game + relay build
bash build-fvd.sh solo            # build and run local game
bash build-fvd.sh game            # build and open local lobby
bash build-fvd.sh relay --port 40000
bash build-fvd.sh test
bash build-fvd.sh python-test     # optional Python 3.12+ unittest suite
bash build-fvd.sh package         # explicitly create a release 7Z
```

Linux launches run from the configured CMake build tree (`build/packaging` or `bin/packaging`) using the source `assets/` directory.

## Fast development versus packaging

Every gameplay, relay or test command builds the required CMake targets *incrementally* before running. If the build is already up to date, CMake should have little or nothing to compile. The wrappers skip explicit reconfiguration when the cached developer configuration is still valid; CMake build dependencies still trigger regeneration as required.

`package` is a separate **opt-in release step**: it configures `FVD_NATIVE_RELEASE=ON`, refreshes the release asset manifest, builds game and relay, and writes the distributable 7Z to `dist/`. It does **not** extract or launch that archive. Extract and verify releases separately on player machines or the relay server; see [release acceptance](code/network/FVD_RELEASE_ACCEPTANCE.md).

The authoritative engine API is checked in under `assets/fvd/engine_api`. The [standalone relay-only build](../packaging/native_relay/README.md) can be used without the game's Qt dependencies.
