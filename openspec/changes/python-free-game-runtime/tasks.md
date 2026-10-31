# Tasks

- [ ] 1.1 Inventory Python ABI imports and Python-backed runtime types reachable
  from the game and relay; record the required library boundary changes.
- [ ] 1.2 Add a CI check that reports Python imports in proposed runtime binaries
  and rejects Python artifacts in the end-user package component.
- [ ] 2.1 Add native argument structures and parsers for the packaged game and
  relay commands, with unit tests for defaults, validation and failures.
- [ ] 2.2 Add native executable-relative and platform user-configuration path
  discovery, preserving explicit developer overrides.
- [ ] 2.3 Construct the engine root from native `FSLike` objects and remove
  Python-backed path objects from the native startup interface.
- [ ] 2.4 Move Python object, exception and filesystem adapters behind an optional
  adapter target so shipped runtime binaries do not import the Python ABI.
- [ ] 3.1 Add the native relay executable, signal handling and start/stop smoke
  test while preserving the existing relay option contract.
- [ ] 3.2 Add the native graphical game executable and call `run_game` directly
  with the packaged FVD defaults.
- [ ] 3.3 Preserve useful native logging, exception reporting and Windows error
  dialogs for failures before and during engine startup.
- [ ] 4.1 Define a versioned packaged-asset manifest covering the exported nyan
  API, FVD modpack and compatibility version.
- [ ] 4.2 Generate and validate the manifest during release packaging; fail the
  build when required converted data is absent or stale.
- [ ] 4.3 Validate the manifest in native startup and report a clear error without
  invoking asset conversion.
- [ ] 5.1 Split end-user runtime and Python developer/converter install components.
- [ ] 5.2 Switch Windows 7Z/installer, Linux and macOS packages to the native
  executables and remove embedded Python from the runtime component.
- [ ] 5.3 Test native `--help`, `--version`, bounded headless game startup and relay
  startup/shutdown in package CI on Windows, Linux and macOS.
- [ ] 5.4 Verify the final package contains no Python executable, library, standard
  library, source/bytecode module or Cython extension and that no runtime binary
  imports the Python ABI.
- [ ] 5.5 Update the build, install and packaging documentation to distinguish
  Python-powered developer tools from the Python-free end-user runtime.
- [ ] 5.6 Manually verify graphical solo play, hosting, guest join and clean exit
  from an extracted Windows package.
