# Design

## Product boundary

FVD is a standalone game fork rather than a general Age of Empires conversion engine.
The supported product surface consists of `openage-native`, `openage-relay`, FVD source assets, the tracked nyan engine API and native FVD packages.
Compatibility with upstream converter inputs, `bin/run`, embedded Python installations and generic openage packages is not a requirement.

Nyan remains part of the native engine.
The game continues to link the C++ nyan library and load `.nyan` data at runtime without Python.

## Tracked engine API

The current release build obtains the engine API from ignored `assets/converted/engine` output produced by `convert-export-api`.
Removing conversion requires this data to become an ordinary versioned FVD input.

Store the compatible exported API under a tracked FVD-owned directory such as `assets/fvd/engine_api`.
Package it at the existing runtime mount point `assets/converted/engine` so simulation and nyan paths do not change merely because source ownership changed.
Keep its API version in one C++ or CMake source of truth shared by build validation and native startup.
Generate the release integrity manifest from the tracked snapshot and the rest of the FVD assets.

Package configuration must fail when the tracked API is absent, has an incompatible version or lacks its manifest.
No build or player command may attempt to regenerate it automatically.
Updating the snapshot is an explicit source change reviewed alongside engine API changes.

## Native C++ test runner

The existing code generator already creates a C++ name-to-function test table.
Expose that table through a small native executable that supports listing tests, running one named test and running all registered C++ tests.
It must report the failing test name, preserve C++ exception details and return nonzero on failure.

Move C++ test registration to a C++-owned or neutral declarative source of truth.
The runner must not import Python, embed Python or call through Cython.
Tests that exercise deleted converter or Python-bridge behavior are removed with those systems.
Tests for engine behavior are retained and adapted to native filesystem/configuration interfaces where necessary.

Relevant pure Python tests remain separate and run through the selected Python environment.
They may invoke native executables as subprocesses, but they do not load `libopenage` through an extension module.

## Build-system reduction

Generate the four coordinate header/source pairs one final time, remove generated-file warnings and commit them as ordinary C++ source.
Compile their four `.cpp` files directly instead of creating a generated single-compilation-unit wrapper.
Replace the Python-produced test dispatch table with C++-owned registration maintained alongside the native test runner.
Delete the Python code generator, its Mako templates, generated-file cache and `cppgen`/`cleancodegen` targets after both output families have native owners.

Delete Cython module declarations, `pxdgen`, `.pxd` generation, Cython compilation, in-place extension copying and Python development-library linking.
Remove `FVD_WITH_PYTHON_ADAPTERS` after all native targets use the sole remaining implementation.
Delete dead `pxd:` annotations and Python callback branches from C++ interfaces as each adapter is removed.

Retain Python only as an optional environment for relevant pure Python tests.
Configuring and building the native game, relay and C++ tests must not require a Python interpreter, Mako, Cython, Python headers, a Python import library or compiled Python extensions.
The standalone relay remains buildable without Python, Qt, nyan or game assets.

## Converter removal

Delete `openage/convert`, converter-only cab extraction, proprietary media readers/exporters and dependencies used only by those tools.
FVD-authored textures, animation metadata, audio, QML, nyan data and Blender exports become the authoritative source assets.
No supported command converts an original game installation or generates an upstream-compatible modpack.

The removal includes converter commands, documentation, tests and configuration data.
Shared utilities remain only when a retained native or pure Python workflow uses them.

## Launcher removal

Delete the embedded `run.py` executable, Python command dispatcher and Python game/relay entry points.
Developer and player game launches use `openage-native`.
Relay launches use `openage-relay`.
C++ tests use the native test runner, and pure Python tests use an explicit Python test command.

Build and editor tasks must build the relevant target before launching it so stale native objects cannot be mistaken for current source behavior.

## Packaging removal

Delete the generic `openage.exe` launcher, `openage.bat`, embedded Python download/install scripts, generic Python module installation and NSIS or all-component paths retained solely for upstream packaging.
CPack produces the FVD native component and its platform archive only.
Package audits continue to reject Python artifacts, Python ABI imports, missing native dependencies and invalid asset manifests.

## Delivery order

1. Commit and validate the tracked engine API snapshot while the exporter still exists.
2. Materialize the coordinate sources and add the native C++ test runner with C++-owned registration.
3. Remove Python code generation, Cython and Python adapter code from the build graph and source tree.
4. Remove conversion and the legacy launcher.
5. Reduce packaging to the native FVD component.
6. Remove unused dependencies and update documentation after clean Windows and Linux builds pass.

Each step must leave the native game, relay and relevant tests buildable.
Avoid a single deletion commit that makes failures impossible to localize.

## Risks

The engine API snapshot can become stale if its version is duplicated or updates are informal.
A single compatibility constant, manifest validation and reviewable source diffs mitigate that risk.

Some C++ tests currently construct Python-backed filesystem objects or rely on the Cython exception translator.
They need native fixtures and direct exception reporting before the bridge can disappear.

Top-level CMake currently assumes Python and Cython are both available.
The native targets must stop finding or invoking them while optional pure Python test commands remain usable outside the native build graph.

Deleting the converter removes the ability to recreate upstream assets from an original game installation.
This is intentional for FVD and should be stated prominently in development documentation.
