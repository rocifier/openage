# FVD native game startup

The experimental `openage-native` client is a C++ entry point that calls
`run_game` directly. It uses native C++ filesystem mounts and a native
configuration loader. Unlike the dedicated relay, the **game client still
needs Qt, the engine and the development build's Python/Cython toolchain**.

The current Windows portable release still starts through the embedded
Python launcher. Do not replace that launcher or remove Python from the
client package before the adapter split and GUI/online smoke tests pass.

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
startup smoke test.

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

**Not yet a distributable portable release:** this component does not yet
deploy every native Qt/platform DLL or recursively audit Windows imports,
and the complete graphical/network smoke test is pending. The normal
`cmake --install` still installs the legacy developer runtime; select
`--component FVDNative` for Python-free staging.

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

The audit checks every staged EXE/DLL for Python imports, including Qt
plugins when present. Full Qt DLL deployment and graphical verification
are still outstanding.

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
