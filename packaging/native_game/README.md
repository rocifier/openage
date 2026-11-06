# FVD native game startup

## Windows: one-command native release

From an existing configured MSVC/vcpkg checkout, run at the repository root:

```powershell
.\build-fvd.cmd
```

This handles the complete native release workflow automatically:

- Reuse the exported nyan engine API when already available; otherwise build the developer exporter once and generate it (Python/Cython are **build-time tools only**).
- Configure and incrementally build `openage_native_game` and `openage_native_relay` with Python adapters disabled.
- Create the strict Python-free 7Z archive under `dist\`.
- Locate 7-Zip and Visual Studio `dumpbin.exe`, extract the archive to a temporary folder, audit DLL imports and Python-free contents, verify SHA-256 assets/tamper detection, and observe a solo native process for 30 seconds.

The script also loads a local `deps/msvc-env.ps1` if present, so you do not have to source it separately. On subsequent runs, an existing valid API export is reused and the CMake build is incremental: **there is no need to rebuild the developer/Cython targets each time**. The build still requires MSVC, CMake, vcpkg dependencies and 7-Zip on the developer's Windows machine; **none of the developer's Python dependencies ship in the player archive**.

Useful optional switches:

```powershell
.\build-fvd.cmd -ReexportApi         # Refresh a changed nyan API (may rebuild developer tools)
.\build-fvd.cmd -SkipSmoke           # Just create the archive, without extracted verification
.\build-fvd.cmd -ObserveSoloSeconds 0 # Audit archive without launching the graphical solo process
.\build-fvd.cmd -Jobs 8              # Adjust build parallelism
```

**Manual acceptance remains:** test actual graphical solo gameplay, native relay host/guest replication and clean exits on a Windows installation with no Python. The 30-second smoke check proves the process survives and has no Python DLLs; it is not a substitute for gameplay testing.

## Developer/advanced commands

The `openage-native` client is a C++ entry point that calls `run_game`
directly, with native filesystem mounts and configuration. The **native
release runtime** needs Qt and the engine, but does **not** ship Python or
Cython; developer builds can continue using them.

The public Windows distribution remains on its legacy launcher until
manual graphical, multiplayer and clean-machine acceptance passes. This
does not affect the verified native-only 7Z release candidate.

## Build and test

Using a normal configured openage source build:

```sh
cmake --build bin --target openage_native_game
ctest --test-dir bin -R fvd_native_game_ --output-on-failure
```

Run the native C++ client from the repository root using converted assets:

```sh
bin/packaging/openage-native --asset-dir ./assets --cfg-dir ./cfg --solo
```

For a network lobby, omit `--solo`. `--help` lists available native
options. `--headless` is useful for diagnostics, but is not yet a bounded
startup smoke test. `--verify-assets` validates the installed FVD modpack,
exported engine API and every SHA-256 entry in the native release manifest,
then exits **without opening the renderer or writing user configuration**.
Strict-release builds reject a missing manifest.

Before launching, the asset directory must include the FVD modpack at
`fvd/modpack.toml` and the exported nyan API with `converted/engine/modpack.toml`
and `converted/engine/manifest.toml`. The API must have version `0.5.0`,
as defined in `openage/convert/service/init/api_export_required.py`.
Conversion/export remains a **developer or release-build** operation and
is never attempted by `openage-native`.

The game finds `share/openage` relative to the executable in a packaged
layout unless `--asset-dir` overrides that location. It writes configuration
to the platform user configuration directory (or `--cfg-dir`) and copies
packaged default `audio.oac` and `keybinds.oac` if missing.

## Native-only build and package-content audit

The developer configuration has `FVD_WITH_PYTHON_ADAPTERS=ON` by default.
Release staging can exclude Python-backed filesystem implementations:

```sh
cmake -S . -B build -DFVD_WITH_PYTHON_ADAPTERS=OFF
cmake --build build --target openage_native_game openage_native_relay
cmake --install build --component FVDNative --prefix build/fvd-native-stage
cmake -DFVD_NATIVE_STAGE=build/fvd-native-stage -P packaging/native_game/AuditNativePackage.cmake
```

This installs only a selected C++ game/relay, engine, nyan, FVD media,
shaders, and default config; it rejects Python executables/libraries and
`.py`/`.pyc`/`.pyd` files in the resulting stage. It does **not**
package conversion tools or the embedded Windows Python launcher.

A playable release stage also needs an exported engine nyan API, which can
be supplied at CMake configuration using
`-DFVD_NATIVE_API_ASSET_DIR=<path-to-exported-engine-modpack>`.
That directory must contain `modpack.toml` and `manifest.toml`. Python
may be used to **generate** the assets, but is not used to load them.

The Windows extracted 7Z has passed its automated package/CLI/integrity/DLL
and bounded solo-process audits (2026-11-06). Full manual gameplay/multiplayer
and clean-machine acceptance remains open. The normal `cmake --install`
still installs the legacy developer runtime; select `--component FVDNative`
for Python-free staging.

## Strict native release staging

Use `-DFVD_NATIVE_RELEASE=ON -DFVD_WITH_PYTHON_ADAPTERS=OFF` when
configuring a release, and pass
`-DFVD_NATIVE_API_ASSET_DIR=<exported engine modpack directory>`.
The developer can export the API first with
`<build>/run convert-export-api assets/converted`. Release
configuration generates `fvd-native-manifest.toml` with SHA-256 hashes
for FVD media and the pre-exported nyan API. Native startup verifies
the manifest; the player never runs the converter.

For a staged release, run the CMake audit with
`-DFVD_NATIVE_REQUIRE_MANIFEST=ON`. On Windows, source
`deps/msvc-env.ps1` and also run:

```powershell
.\packaging\native_game\AuditWindowsImports.ps1 -Stage .\build\fvd-native-stage
```

The audit checks every staged EXE/DLL for Python imports and verifies
that all non-system DLL dependencies are supplied inside the package,
including Qt plugins. Graphical verification remains outstanding.

## Windows 7Z release candidate (native-only)

From an existing configured Windows MSVC/vcpkg development checkout, export the
engine nyan API **once** using your developer runner (this step may use Python):

```powershell
. .\deps\msvc-env.ps1
.\.bin\msvc-vcpkg\RelWithDebInfo\run.exe convert-export-api .\assets\converted
```

Then reconfigure the same CMake build for the native player, rebuild the
native targets and ask CPack for **only** the `FVDNative` component:

```powershell
cmake -S . -B .bin\msvc-vcpkg -DFVD_NATIVE_RELEASE=ON -DFVD_WITH_PYTHON_ADAPTERS=OFF -DFVD_NATIVE_API_ASSET_DIR="$((Resolve-Path .\assets\converted\engine).Path)"
cmake --build .bin\msvc-vcpkg --config RelWithDebInfo --target openage_native_game openage_native_relay
cpack --config .bin\msvc-vcpkg\CPackConfig.cmake -C RelWithDebInfo -G 7Z
```

The Windows `FVDNative` install component stages Qt/QML plugins, vcpkg
and MSVC runtime DLLs, native FVD game/relay, graphics/audio/config and the
pre-exported API, without calling the legacy embedded-Python installer.
The archive still needs extraction and real Windows solo/network testing
before it can replace the current public package.

### Check the extracted archive (not just the install directory)

The test script needs 7-Zip and a Visual Studio developer PowerShell for
`dumpbin.exe`. Supply the actual 7Z built by CPack:

```powershell
. .\deps\msvc-env.ps1
.\packaging\native_game\SmokeTestWindowsArchive.ps1 -Archive "<path-to-native-7z>" -KeepExtracted
```

It extracts the release to a fresh temporary directory, finds the native
launcher, audits **every shipped EXE/DLL and its non-system DLL dependencies**
and rejects Python files/directories. Then it runs `--help`, `--version`,
invalid-input, missing-asset, and successful `--verify-assets` smoke tests.
It also deliberately corrupts a packaged asset, checks for checksum rejection
and restores it. `-KeepExtracted` prints the extracted folder for manual
playtesting; omit it to remove the temporary files.

To additionally launch the extracted native solo game and inspect its **live**
loaded Windows DLL modules for Python, use `-ObserveSoloSeconds 30` (or
another duration up to 600 seconds). This requires a working graphical
desktop. The process is stopped automatically afterward; watch the window
to determine whether it actually reached gameplay.

**Still manual:** on a machine without Python installed, launch
`bin/openage-native.exe --solo`, start actual gameplay, test a hosted lobby
and a guest through `bin/openage-relay.exe`, exercise replication and exit
cleanly. Observe the running process modules for Python DLLs, not just
static DLL imports. Do not mark OpenSpec acceptance complete until these
passes are recorded.

To inspect the staged component separately, run:

```powershell
cmake --install .bin\msvc-vcpkg --config RelWithDebInfo --component FVDNative --prefix .bin\fvd-native-stage
.\packaging\native_game\AuditWindowsImports.ps1 -Stage .bin\fvd-native-stage
```

## Before replacing the Windows launcher

- Separate Python filesystem, exception and Cython adapters from the
  runtime-linked engine.
- Build, run and audit the native application and all loaded libraries
  without Python.
- Create and validate the versioned release asset manifest.
- Manually verify Windows solo, lobby hosting, guest joining and exit.
- Switch the end-user package and remove the embedded interpreter only
  after the native route passes all acceptance checks.
