# Proposal

## Why

FVD now ships and runs through native C++ game and relay executables.
The remaining converter, Cython bridge, legacy launcher and generic Python packaging exist for upstream openage workflows that FVD does not intend to support.
Keeping them makes every developer build configure a second application architecture, retains hundreds of unused files and dependencies, and creates recurring ambiguity about which executable and package are authoritative.

FVD still benefits from some pure Python tests as developer-only checks.
The C++ generator does not need to remain because its stable coordinate outputs can become ordinary source files and its changing test table can be owned by the native test runner.

## What Changes

- Make the exported nyan engine API a versioned, tracked FVD source asset instead of an ignored converter output.
- Commit the generated coordinate types as ordinary C++ source and replace Python-generated test dispatch with C++-owned registration.
- Add a native C++ test runner so C++ tests no longer require a Cython dispatch module or embedded Python launcher.
- Remove the Python C++ generator, Mako templates and codegen build integration after their outputs have native owners.
- Remove every Cython extension, generated `.pxd` workflow, Python/C++ exception bridge and Python adapter build option.
- Remove original-game asset conversion, converter-only media readers/exporters and their dependencies.
- Remove the Python `run` launcher and the Python game, relay and developer compatibility entry points.
- Remove generic openage packaging, the embedded Python distribution, the legacy `openage.exe`/`openage.bat` launchers and non-FVD package paths.
- Keep the native FVD game, native relay, native FVD archive and strict asset/import audits as the only supported player distribution.
- Retain relevant pure Python tests as an optional developer workflow until a separate change chooses replacements for them.

## Capabilities

### New Capabilities

- `fvd-native-source-tree`: an FVD-focused source tree with tracked runtime data, committed C++ sources, native C++ testing and no Python code generator, converter, Cython, legacy launcher or generic Python package.

### Modified Capabilities

- `fvd-native-runtime`: consume a tracked engine API snapshot and build/package without Python source generation, a Cython development configuration or API-export bootstrap.

## Impact

- Build configuration and generated-source pipeline
- Coordinate type and C++ test registry ownership
- C++ test discovery and execution
- Engine API asset ownership and package staging
- Python package layout and dependencies
- Windows and Linux developer commands
- CPack components and platform launchers
- Developer and packaging documentation

## Status

Proposed.
