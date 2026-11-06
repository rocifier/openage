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

**The packaged native FVD player and standalone relay do not use
Python.** The initial engine API export and generic openage developer
tools still need Python 3.12+, Cython 3.1+ and Python development headers.
Use `uv sync` for the Python packages listed in
[`pyproject.toml`](../../pyproject.toml) only when using that workflow.

See [building.md](../building.md) for details.
