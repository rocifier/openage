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

Then use `bash build-fvd.sh test`, `bash build-fvd.sh game` (multiplayer lobby),
`bash build-fvd.sh solo` or `bash build-fvd.sh relay` to test, play or host.

**The native FVD build and distribution require no Python, Cython, Mako or converter.** Optional pure-Python network tests run separately with `bash build-fvd.sh python-test` (Python 3.12+). `uv sync` installs only optional lint/type-check tools.

If Linux Mint cannot locate toml11, see the
[existing toml11 workaround](https://github.com/SFTtech/openage/issues/1601).
Linux native archives are not yet guaranteed portable across distributions.
