# openSUSE dependencies

Install the native C++ toolchain and FVD engine development libraries:

```bash
sudo zypper install --no-recommends gcc-c++ cmake ninja flex eigen3-devel \
  fontconfig-devel libfreetype-devel harfbuzz-devel libepoxy-devel \
  libogg-devel libopus-devel libpng-devel libtoml11-dev \
  qt6-declarative-dev qt6-quickcontrols2 qt6-multimedia-dev \
  qt6-shadertools-devel opusfile-devel
```

Package names can vary across openSUSE releases. You also need
[nyan](https://github.com/SFTtech/nyan); a fresh `bash build-fvd.sh`
build can ask CMake to fetch it.

For the Python-free **FVD runtime package**, run `bash build-fvd.sh` from
the repository root. It packages and smoke-tests the game and relay.
Use `bash build-fvd.sh test`, `solo` or `relay` for subsequent checks.

**The native FVD build and distribution require no Python, Cython, Mako or converter.** Optional pure-Python network tests run separately with `bash build-fvd.sh python-test` (Python 3.12+). `uv sync` installs only optional lint/type-check tools.

See [building.md](../building.md) for details.
