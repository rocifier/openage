# Tasks

- [ ] 1.1 Inventory Python ABI imports and Python-backed runtime types reachable
  from the game and relay; record the required library boundary changes.
- [ ] 1.2 Add a CI check that reports Python imports in proposed runtime binaries
  and rejects Python artifacts in the end-user package component.
- [x] 2.1 Add native argument structures and parsers for the packaged game and
  relay commands, with unit tests for defaults, validation and failures.
- [x] 2.2 Add native executable-relative and platform user-configuration path
  discovery, preserving explicit developer overrides.
- [x] 2.3 Construct the engine root from native `FSLike` objects and remove
  Python-backed path objects from the native startup interface.
- [ ] 2.4 Move Python object, exception and filesystem adapters behind an optional
  adapter target so shipped runtime binaries do not import the Python ABI.
  Progress: Path/File and Python filesystem implementations are optional
  sources (`FVD_WITH_PYTHON_ADAPTERS=OFF`); pyinterface object/exception
  scaffolding and its final library boundary are still outstanding.
- [x] 3.1 Add the native relay executable, signal handling and start/stop smoke
  test while preserving the existing relay option contract.
- [x] 3.2 Add the native graphical game executable and call `run_game` directly
  with the packaged FVD defaults. (Startup and real gameplay still need validation.)
- [ ] 3.3 Preserve useful native logging, exception reporting and Windows error
  dialogs for failures before and during engine startup.
- [x] 4.1 Define a versioned packaged-asset manifest covering the exported nyan
  API, FVD modpack and compatibility version.
- [ ] 4.2 Generate and validate the manifest during release packaging; fail the
  build when required converted data is absent or stale.
  Progress: strict native release mode now requires an exported compatible
  engine API, generates SHA-256 records and stages a versioned manifest.
  End-to-end package verification remains.
- [x] 4.3 Validate the manifest in native startup and report a clear error without
  invoking asset conversion. (Developer runs may omit the manifest; strict
  release packaging must include one.)
- [ ] 5.1 Split end-user runtime and Python developer/converter install components.
  Progress: explicit FVDNative staging excludes Python source/bytecode;
  existing full package remains the developer/legacy path.
- [ ] 5.2 Switch Windows 7Z/installer, Linux and macOS packages to the native
  executables and remove embedded Python from the runtime component.
- [ ] 5.3 Test native `--help`, `--version`, bounded headless game startup and relay
  startup/shutdown in package CI on Windows, Linux and macOS.
- [x] 3.4 Add an independent CMake entry point for the relay that requires only
  a C++20 compiler, CMake and OS networking primitives (no Python, Cython,
  Qt, nyan or game assets), and automated cross-platform build/test coverage.
- [ ] 3.5 Verify a live relay host/guest pair and deployed ABI on the volunteer
  host before replacing the hosted Python/Cython service.
- [ ] 5.4 Verify the final package contains no Python executable, library, standard
  library, source/bytecode module or Cython extension and that no runtime binary
  imports the Python ABI.
- [ ] 5.5 Update the build, install and packaging documentation to distinguish
  Python-powered developer tools from the Python-free end-user runtime.
- [ ] 5.6 Manually verify graphical solo play, hosting, guest join and clean exit
  from an extracted Windows package.
