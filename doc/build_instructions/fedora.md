# Fedora dependencies

For a native FVD development build, install the compiler, build tools
and required C++ libraries:

```bash
sudo dnf install gcc-c++ cmake ninja-build flex eigen3-devel fontconfig-devel \
  freetype-devel harfbuzz-devel libepoxy-devel libogg-devel libopus-devel \
  libpng-devel opusfile-devel toml11-devel qt6-qtdeclarative-devel \
  qt6-qtmultimedia-devel
```

Install or let CMake fetch [nyan](https://github.com/SFTtech/nyan).
Then run `bash build-fvd.sh` from the repository root for a native FVD
package, or `bash build-fvd.sh test` / `solo` / `relay` afterwards.

**The native FVD build and distribution require no Python, Cython, Mako or converter.** Optional pure-Python network tests run separately with `bash build-fvd.sh python-test` (Python 3.12+). `uv sync` installs only optional lint/type-check tools.

See [building.md](../building.md) for details.
