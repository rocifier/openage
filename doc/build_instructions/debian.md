# Debian dependencies

Install the native libraries for an FVD source build:

```bash
sudo apt-get update
sudo apt-get install g++ cmake ninja-build flex libeigen3-dev libepoxy-dev \
  libfontconfig1-dev libfreetype-dev libharfbuzz-dev libogg-dev libopus-dev \
  libopusfile-dev libpng-dev libtoml11-dev qt6-declarative-dev \
  qt6-multimedia-dev qml6-module-qtquick-controls
```

Install or let CMake fetch [nyan](https://github.com/SFTtech/nyan).
Once dependencies are available, build native FVD from the repository root:

```bash
bash build-fvd.sh
```

`bash build-fvd.sh test`, `solo` and `relay` also run the native tools
without rebuilding the entire project.

**The native FVD build and distribution require no Python, Cython, Mako or converter.** Optional pure-Python network tests run separately with `bash build-fvd.sh python-test` (Python 3.12+). `uv sync` installs only optional lint/type-check tools.

See [building.md](../building.md) for details.
