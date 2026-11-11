# FVD assets and tracked engine API

FVD uses its **own authored graphics, animation atlases, audio and nyan data**. The original Age of Empires asset converter has been deliberately removed; FVD cannot import or convert an original-game installation. Do not install game CD files or invoke legacy `bin/run` commands.

## Source of truth

- `assets/fvd/modpack.toml`: FVD game content package.
- `assets/fvd/graphics/`, `sound/`, `data/`, `qml/`: runtime art, audio, scenarios and FVD UI.
- `assets/fvd/engine_api/`: **versioned, committed** nyan engine API snapshot and SHA3-256 manifest. Update it only through a reviewed source change.
- `assets/qml/`, `assets/shaders/`: shared renderer resources retained by the native game.

Sprite texture paths are relative to the `.sprite` file's directory; texture image paths are relative to the `.texture` file's directory.
Static sprites use `layer 0 mode=off` but still require a `frame` entry for each non-mirrored angle.
The native `openage::gamestate::tests::fvd_scenario` test loads both tower tiers, checks their frames and decodes their referenced images (see [testing](code/testing.md)).

## Runtime and integrity

`build-fvd` installs these tracked assets into the audited native 7Z archive. The engine API is mounted at `share/openage/converted/engine` **inside the package** for compatibility with native nyan imports. That path is a runtime mount, not a generated `assets/converted` source-tree directory.

The FVDNative package verifies the engine API version, SHA3-256 source manifest, release SHA-256 asset hashes and required files. Players need neither Python nor any original game. See [building](building.md), [running](running.md) and [native packaging](../packaging/native_game/README.md).
