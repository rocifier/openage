# Tasks

## 1. Make runtime data independent of conversion

- [x] 1.1 Copy the currently accepted engine API into a tracked FVD-owned source directory, preserving LF endings, API metadata and licence information.
- [x] 1.2 Replace `FVD_NATIVE_API_ASSET_DIR` bootstrap/export logic with the tracked source directory as the default build and package input.
- [x] 1.3 Centralize the expected engine API version and validate the tracked snapshot during configuration, package staging and native startup.
- [x] 1.4 Add regressions for missing, incomplete and version-incompatible tracked API data and for its installed runtime path.
- [ ] 1.5 Build and smoke-test a Windows and Linux native archive before deleting the exporter.

## 2. Replace Python-generated C++ and the Cython test bridge

- [x] 2.1 Generate the four coordinate header/source pairs one final time, remove generated-file warnings and commit them as ordinary C++ source.
- [x] 2.2 Compile the committed coordinate `.cpp` files directly and remove their Mako templates and generated single-compilation-unit wrapper.
- [x] 2.3 Add a native C++ test executable that lists tests, runs one exact or prefixed name and runs all registered C++ tests.
- [x] 2.4 Report the active test name, source failure location and uncaught C++ exception details without Python exception translation.
- [x] 2.5 Move C++ test registration out of the Python test list and generated dispatcher into a C++-owned source of truth.
- [x] 2.6 Port retained tests that use Python-backed paths or callbacks to native fixtures.
- [x] 2.7 Update `build-fvd` and editor tasks to run native C++ tests and pure Python tests as separate commands.

Task 2 migration note: the Python-generated C++ dispatcher, coordinate Mako
generation and the `cpp_testing.pyx` bridge are no longer compiled or used.
The native C++-owned registry includes 113 retained tests, six opt-in demos and
one opt-in benchmark. Four tests/demos solely exercising the removed Python
C++ bridge are retired. C++ texture tests now use a native filesystem fixture;
FVD scenario and network tests load the tracked engine API, not the conversion
cache. Asset-dependent tests require `--all --have-assets`, while process
workers and slow benchmarks are opt-in. The `build-fvd test` and `python-test`
commands and VS Code tasks run independent native and pure-Python suites.
The remaining Python/Cython application adapters and codegen framework are
removed in Task 3. Full Windows/Linux archive release smoke tests are still
tracked separately under 1.5/6.3.

## 3. Remove Python code generation, Cython and Python adapters

- [x] 3.1 Delete `openage/codegen`, its build integration, generated-file cache and generator cleanup targets.
- [x] 3.2 Delete all retained `.pyx` and `.pxd` modules and their CMake declarations.
- [x] 3.3 Remove `pxdgen`, Cython compilation, in-place extension copying and generated binding cleanup targets from the build system.
- [x] 3.4 Remove Python object wrappers, exception translation, filesystem callbacks and other C++ branches used only by the deleted bindings.
- [x] 3.5 Remove `FVD_WITH_PYTHON_ADAPTERS`, Python/Mako discovery, Python development headers/libraries and Cython from native build requirements.
- [ ] 3.6 Verify clean native game, relay and C++ test builds without a Python interpreter installed.

Task 3 implementation: the Python `openage/codegen` subtree, all 44 tracked
Cython `.pyx` / `.pxd` files, their CMake registration, extension build
helpers, native Python adapter C++ sources, `pyinterface`, Python filesystem
constructors and configuration callbacks have been removed. Native CMake no
longer discovers Python/Mako/Cython or uses the `FVD_WITH_PYTHON_ADAPTERS`
option. Native-only source audits run in CI. Linux full-build verification
passed in GitHub Actions run 42335663484 (commit a8b5d905): all native
game/relay/C++ test targets built with CMake Python discovery disabled;
15/15 native CTest checks passed; nine standalone Python unittest cases
passed; the staged FVDNative component contained 999 files and no Python;
and headless staged-game startup reached initialized engine threads. This
does not by itself prove a machine with Python physically uninstalled.

Task 3.6 remains open until
fresh Windows and Linux native game/relay/C++ test builds are verified on
machines **without an installed Python interpreter**; disabling Python
discovery in hosted CI does not itself meet that stricter gate.

## 4. Remove unsupported upstream workflows

- [ ] 4.1 Delete `openage/convert`, converter-only cab/media modules, conversion commands, tests and configuration data.
- [ ] 4.2 Remove converter-only Python and native dependencies from CMake, `pyproject.toml`, lock files, platform instructions and packaging metadata.
- [ ] 4.3 Delete `run.py`, the Python command dispatcher and Python game, relay, main and demo entry points that have no retained use.
- [ ] 4.4 Replace documentation and scripts that invoke `bin/run` with native game, relay, C++ test or explicit pure Python commands.
- [ ] 4.5 Confirm no supported build or startup path reads or writes `assets/converted` as a generated source-tree cache.

## 5. Keep only native FVD packaging

- [ ] 5.1 Remove the generic graphical launcher, batch launcher, embedded Python download/install and generic Python module installation.
- [ ] 5.2 Remove generic all-component and NSIS packaging paths that are not used by the native FVD distribution.
- [ ] 5.3 Make the FVD native component the only supported CPack payload and preserve Windows/Linux archive naming and layout.
- [ ] 5.4 Keep strict package audits for Python artifacts/imports, native dependency closure, tracked API compatibility and asset hashes.

## 6. Verify and document the fork boundary

- [ ] 6.1 Run clean Windows and Linux native game, relay and C++ test builds without Python, Mako, Cython or Python development libraries installed.
- [ ] 6.2 Run retained pure Python tests through the documented optional Python environment.
- [ ] 6.3 Build and inspect fresh Windows and Linux FVD archives, then run solo, host, guest, relay, audio and clean-shutdown smoke checks.
- [ ] 6.4 Update project structure, building, build-system, testing and packaging documentation to describe the FVD-only workflow.
- [ ] 6.5 Remove stale upstream compatibility instructions and state that original-game asset conversion is unsupported.
- [ ] 6.6 Run changed-file quality checks and record anything blocked by unavailable platform tooling.
