# openSUSE dependencies

Install the native C++ toolchain and FVD engine development libraries:

```bash
sudo zypper install --no-recommends gcc-c++ cmake ninja flex eigen3-devel \
  fontconfig-devel libfreetype-devel harfbuzz-devel libepoxy-devel \
  libogg-devel libopus-devel libpng-devel libtoml11-dev \
  qt6-declarative-dev qt6-quickcontrols2 qt6-multimedia-dev opusfile-devel
```

Package names can vary across openSUSE releases. You also need
[nyan](https://github.com/SFTtech/nyan); a fresh `bash build-fvd.sh`
build can ask CMake to fetch it.

For the Python-free **FVD runtime package**, run `bash build-fvd.sh` from
the repository root. It packages and smoke-tests the game and relay.
Use `bash build-fvd.sh test`, `solo` or `relay` for subsequent checks.

Python and Cython are not required to *run* the native FVD distribution.
The first build-time engine API export and traditional openage developer
workflow still use Python 3.12+, Cython 3.1+ and Python development
headers. Install the developer environment via `uv sync` using
[`pyproject.toml`](../../pyproject.toml).

See [building.md](../building.md) for details.
