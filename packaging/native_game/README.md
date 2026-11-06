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

## Before replacing the Windows launcher

- Separate Python filesystem, exception and Cython adapters from the
  runtime-linked engine.
- Build, run and audit the native application and all loaded libraries
  without Python.
- Create and validate the versioned release asset manifest.
- Manually verify Windows solo, lobby hosting, guest joining and exit.
- Switch the end-user package and remove the embedded interpreter only
  after the native route passes all acceptance checks.
