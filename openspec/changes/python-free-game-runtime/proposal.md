# Proposal

## Why

The Windows portable package currently includes its own Python interpreter and
starts gameplay through `python -m openage game`.
Players do not need a separately installed Python, but the private interpreter,
standard library, Python modules and Cython extensions make the package larger
and make game startup depend on the development toolchain's runtime layout.

Gameplay is already implemented primarily in C++.
FVD should start through a native executable and reserve Python for building,
testing, asset conversion and other developer tools.

## What Changes

- Add native game and relay entry points that parse their own command-line
  options and call the existing C++ engine and relay services directly.
- Move runtime path discovery, configuration and asset validation into C++.
- Split Python and Cython adapters from the libraries loaded by the native game
  so no Python library is linked or loaded during gameplay.
- Package converted FVD assets and the exported nyan API as build artifacts.
  The shipped game reports missing or incompatible data instead of running the
  converter on a player's machine.
- Produce portable and installed runtime packages without a Python interpreter,
  Python modules, Cython extensions or Python-only dependencies.
- Keep Python available to source builds for code generation, conversion,
  tests and developer commands.

## Capabilities

### New Capabilities

- `fvd-native-runtime`: Python-free game and relay executables for end-user
  distributions.

### Modified Capabilities

None.

## Impact

- Native executable targets and command-line parsing
- Engine bootstrap, filesystem/path setup, cvars, logging and exception handling
- Separation of `libopenage` runtime code from Python/Cython adapter code
- Asset export and package assembly
- Windows, Linux and macOS package validation

Rewriting the converter, code generator, test runner or general development CLI
in C++ remains out of scope.

## Status

Proposed (2026-10-31).
