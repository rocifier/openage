# Tasks

## 1. Make runtime data independent of conversion

- [ ] 1.1 Copy the currently accepted engine API into a tracked FVD-owned source directory, preserving LF endings, API metadata and licence information.
- [ ] 1.2 Replace `FVD_NATIVE_API_ASSET_DIR` bootstrap/export logic with the tracked source directory as the default build and package input.
- [ ] 1.3 Centralize the expected engine API version and validate the tracked snapshot during configuration, package staging and native startup.
- [ ] 1.4 Add regressions for missing, incomplete and version-incompatible tracked API data and for its installed runtime path.
- [ ] 1.5 Build and smoke-test a Windows and Linux native archive before deleting the exporter.

## 2. Replace Python-generated C++ and the Cython test bridge

- [ ] 2.1 Generate the four coordinate header/source pairs one final time, remove generated-file warnings and commit them as ordinary C++ source.
- [ ] 2.2 Compile the committed coordinate `.cpp` files directly and remove their Mako templates and generated single-compilation-unit wrapper.
- [ ] 2.3 Add a native C++ test executable that lists tests, runs one exact or prefixed name and runs all registered C++ tests.
- [ ] 2.4 Report the active test name, source failure location and uncaught C++ exception details without Python exception translation.
- [ ] 2.5 Move C++ test registration out of the Python test list and generated dispatcher into a C++-owned source of truth.
- [ ] 2.6 Port retained tests that use Python-backed paths or callbacks to native fixtures.
- [ ] 2.7 Update `build-fvd` and editor tasks to run native C++ tests and pure Python tests as separate commands.

## 3. Remove Python code generation, Cython and Python adapters

- [ ] 3.1 Delete `openage/codegen`, its build integration, generated-file cache and generator cleanup targets.
- [ ] 3.2 Delete all retained `.pyx` and `.pxd` modules and their CMake declarations.
- [ ] 3.3 Remove `pxdgen`, Cython compilation, in-place extension copying and generated binding cleanup targets from the build system.
- [ ] 3.4 Remove Python object wrappers, exception translation, filesystem callbacks and other C++ branches used only by the deleted bindings.
- [ ] 3.5 Remove `FVD_WITH_PYTHON_ADAPTERS`, Python/Mako discovery, Python development headers/libraries and Cython from native build requirements.
- [ ] 3.6 Verify clean native game, relay and C++ test builds without a Python interpreter installed.

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
