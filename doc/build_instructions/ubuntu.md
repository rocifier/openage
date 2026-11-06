# Ubuntu dependencies

For a native FVD development build on Ubuntu, install the native compiler,
CMake/Ninja and engine libraries first:

```bash
sudo apt-get update
sudo apt-get install g++ cmake ninja-build flex libeigen3-dev libepoxy-dev \
  libfontconfig1-dev libfreetype-dev libharfbuzz-dev libogg-dev libopus-dev \
  libopusfile-dev libpng-dev libtoml11-dev qt6-declarative-dev \
  qt6-multimedia-dev qml6-module-qtquick-controls
```

You also need [nyan](https://github.com/SFTtech/nyan/blob/master/doc/building.md);
`bash build-fvd.sh` can ask CMake to download it for an unconfigured checkout.

From the repository root, build, package, and smoke-test native FVD:

```bash
bash build-fvd.sh
```

Then use `bash build-fvd.sh test`, `bash build-fvd.sh solo` or
`bash build-fvd.sh relay` to test, play or host using the native build.

**Python is not required by the packaged player or standalone relay.**
The first engine API export, general source-tree CMake setup and legacy
conversion/tests still require Python 3.12+ and Cython 3.1+.
For those *developer* commands, install an appropriate Python interpreter
and development headers, then run `uv sync` in the repository root to
obtain the packages declared in [`pyproject.toml`](../../pyproject.toml).
Do not install individual NumPy/Pillow/lz4/Pygments/Mako packages merely
to run the packaged native game.

Note: older Ubuntu versions (including 22.04 and 24.04) may ship an
outdated Cython or Python; use a supported Python 3.12+ developer
environment rather than changing system-wide pip packages.

If Linux Mint cannot locate toml11, see the
[existing toml11 workaround](https://github.com/SFTtech/openage/issues/1601).
Linux native archives are not yet guaranteed portable across distributions.
