# Arch Linux dependencies

The general openage developer project is also available in the
[AUR](https://aur.archlinux.org/packages/openage-git/).

For native FVD, install a compiler, CMake/Ninja, and the underlying
engine libraries:

```bash
sudo pacman -S --needed gcc cmake ninja flex eigen libepoxy libogg libpng \
  ttf-dejavu freetype2 fontconfig harfbuzz opusfile opus \
  qt6-declarative qt6-multimedia
```

Install [toml11 from the AUR](https://aur.archlinux.org/packages/toml11)
(e.g., `yay -S toml11`) and
[nyan](https://github.com/SFTtech/nyan), or configure CMake to
download nyan.

Build, package and verify native FVD from the repository root:

```bash
bash build-fvd.sh
```

Use `bash build-fvd.sh test`, `solo` or `relay` afterwards.

**The native FVD build and distribution require no Python, Cython, Mako or converter.** Optional pure-Python network tests run separately with `bash build-fvd.sh python-test` (Python 3.12+). `uv sync` installs only optional lint/type-check tools.
