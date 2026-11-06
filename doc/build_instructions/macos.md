# Instructions for macOS users

## Prerequisite steps
- XCode >= Xcode 12
- Install [Homebrew](http://brew.sh). If you use some other package managers, you're on your own :)

```
brew update-reset && brew update
brew install --cask font-dejavu
brew install cmake python3 libepoxy freetype fontconfig harfbuzz opus opusfile libogg libpng toml11 eigen
brew install qtbase qtdeclarative qtmultimedia
```

You will also need [nyan](https://github.com/SFTtech/nyan/blob/master/doc/building.md) and its dependencies:

```
brew install flex make
```

Optionally, for documentation generation:

```
brew install doxygen
```

## Clone the repository

```
git clone https://github.com/SFTtech/openage
cd openage
```

## Python dependencies (developer tools only)

The general openage developer build still needs Python 3.12+ and Cython
3.1+ for conversion, code generation, and the traditional developer
launcher. Install its declared dependencies from the repository root:

```bash
uv sync
```

The dependency list is maintained in [`pyproject.toml`](../../pyproject.toml);
there is no need to duplicate a potentially outdated `pip install` list
here. The native FVD player and standalone relay do not load Python.

**FVD packaging note:** `build-fvd.sh` is currently Linux-specific and
`build-fvd.cmd` is Windows-specific. The full macOS native release
packaging workflow has not yet been validated; the commands below are
for general openage developer builds, not a claimed Python-free macOS
player archive.

## Building

Recent versions of Apple Clang support the C++20 features openage uses, so the
compiler shipped with Xcode is normally fine:

```
./configure --download-nyan -- -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
```

Older Apple Clang releases are missing parts of C++20 and will fail to build.
If you hit compiler errors, install Homebrew's LLVM and point `configure` at it instead:

```
brew install llvm
./configure --compiler="$(brew --prefix llvm)/bin/clang++" --download-nyan -- -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
```

Afterwards, trigger the build using `make`:

```
make -j$(sysctl -n hw.ncpu)
```

## Testing
`make test` runs the built-in tests.


## Running
`make run` or `cd bin && ./run` launches the game. Try `./run --help` if you don't know what to do!


## To create the documentation
`make doc`
For more options and details, refer to [doc/README.md][/doc/README.md]
