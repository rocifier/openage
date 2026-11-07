# Fast Villager Defense (FVD)

FVD is a standalone multiplayer RTS fork of openage, with native C++ game and relay executables. The original openage engine and its contributors are credited in [copying.md](copying.md).

**The supported FVD build does not require Python, Cython, Mako or an original Age of Empires installation.** It uses checked-in FVD assets and the versioned nyan engine API in `assets/fvd/engine_api`. The original-game converter, legacy Python launcher and generic openage game are no longer supported.

## Build and play

**Windows (MSVC/vcpkg):**

```powershell
.\build-fvd.cmd             # build, create native archive and smoke-test
.\build-fvd.cmd build       # incremental native development build
.\build-fvd.cmd test        # native C++ tests
.\build-fvd.cmd game        # multiplayer lobby from latest native archive
.\build-fvd.cmd solo        # play the latest packaged archive
.\build-fvd.cmd relay       # host relay from the archive
```

**Linux:**

```sh
bash build-fvd.sh           # build and verify native archive
bash build-fvd.sh build     # development game and relay
bash build-fvd.sh test      # C++/CTest regression tests
bash build-fvd.sh game      # multiplayer lobby
bash build-fvd.sh solo      # play the packaged game
bash build-fvd.sh relay     # start the packaged relay
```

See [native build requirements](doc/building.md), [runtime commands](doc/running.md), [Windows setup](doc/build_instructions/windows_msvc.md), and [assets and controls](assets/fvd/README.md).

### Tests and developer tools

C++ tests run through `openage_native_cpp_tests` (built by `build-fvd ... test`). Optional pure Python tests for network shaping run separately:

```sh
bash build-fvd.sh python-test
# or on Windows: .\build-fvd.cmd python-test
```

The Python environment is **not** used by the native build or shipped with FVD. FVD does not convert assets from original-game installations; add source sprites, sound, metadata and nyan data directly to `assets/fvd`.

## License

FVD inherits openage's GPL-3.0-or-later licensing and contributor notices. See [copying.md](copying.md) and [legal](legal).
