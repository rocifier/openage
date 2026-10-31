# Design

## Runtime boundary

Create native `openage` game and `openage-relay` executable targets.
The game executable owns command-line parsing, logging setup, installation path
discovery and error presentation before calling `openage::run_game`.
The relay executable performs the equivalent validation before calling
`network::run_relay_service`.
Both targets must work without initializing or loading a Python interpreter.

Keep the Python `openage` package as the developer interface for conversion,
code generation, tests and compatibility with existing source-tree workflows.
Python may call the same C++ APIs through Cython, but the native executables do
not call through those adapters.

## Engine and adapter separation

`libopenage` currently exposes Python-backed types in core interfaces.
For example, `main_arguments::root_path` is populated from a Python `Union`, and
`util::Path` supports a `py::Obj` filesystem implementation.
A native executable alone therefore does not prove a Python-free process if the
linked library still imports the Python ABI.

Inventory link-time and runtime Python references before changing targets.
Move Python object wrappers, exception translation and Cython-facing filesystem
adapters behind a separate adapter library or an equivalent optional build
boundary.
Core engine targets used by the shipped executables must link only native
filesystem implementations and must have no Python import-library dependency.
Source builds may continue to build and link the adapter for Python commands.

Use the existing native `FSLike` implementation to construct the game root in
C++.
Mount packaged assets and writable configuration locations using native paths.
Do not reproduce the Python `Union` API when the installed game needs only a
defined set of native roots.

## Paths and configuration

Resolve immutable data relative to the executable or an installed data prefix.
Resolve writable configuration through the platform's normal user-data location,
while preserving explicit `--asset-dir` and `--cfg-dir` overrides for developers
and diagnostics.
Generate version and compiled-path information into C++ configuration sources at
build time rather than reading `openage/config.py`.

The native game command supports the options used by the packaged FVD product:
`--gl-debug`, `--headless`, `--solo`, `--network-update-interval-ms`,
`--window-size`, `--vsync`, `--window-mode`, verbosity and explicit data paths.
Unsupported or invalid options fail before engine initialization with a useful
message and nonzero exit status.

## Assets

The current Python entry point can export the nyan API and inspect converted
modpacks during startup.
An end-user package must instead contain the complete exported API and FVD
modpack produced during the build or release pipeline.
Packaging validates those inputs and fails if they are absent.
At startup the native game checks a small versioned manifest and fails clearly
if required assets are missing or incompatible.
It never imports conversion code or modifies immutable packaged assets.

Asset conversion remains a Python-powered developer and release operation.
This keeps the migration bounded and avoids rewriting a mature converter that is
not needed while playing FVD.

## Packaging

Define an end-user runtime component containing native executables, shared
libraries, Qt plugins and QML, fonts, audio/render dependencies, configuration
defaults and validated FVD assets.
Do not install the embedded interpreter, Python standard library, `.py` files,
`.pyc` files, `.pyd`/Python extension modules or Python-only dependencies in
that component.

Developer and conversion packages may retain the Python package and Cython
extensions.
Keeping these components explicit prevents a future packaging change from
silently adding Python back to the game archive.

On Windows, `openage.exe` is the graphical application and writes startup
failures to `openage.log` as well as showing a concise error dialog.
Linux and macOS executables report the same failure through the normal log.
The relay is a console executable on every platform and handles process stop
signals directly in C++.

## Migration order

First establish a report that lists Python ABI imports in every prospective
runtime binary.
Then add the native filesystem/bootstrap API and native relay executable because
the relay has the smaller dependency surface.
Add the native game CLI and asset manifest after the engine library can link
without Python.
Finally switch packaging to the native targets and delete the temporary embedded
Python launcher and embedding scripts from the runtime component.

Keep the existing Python entry points working until native smoke tests cover the
same arguments and error cases.
Remove compatibility code once packages use the native route.

## Verification

Add deterministic unit tests for native argument parsing, path selection,
manifest compatibility and relay validation.
Add package smoke tests that run native `--help` and `--version`, start the game
in a bounded headless mode, and start then stop the relay.
Inspect packaged native imports and package contents in CI.
The check fails if a runtime binary imports the Python ABI or if the runtime
component contains a Python interpreter or Python module artifact.

Run these checks on Windows, Linux and macOS.
Manually verify a graphical game start, solo match, hosted match, guest join and
clean shutdown on Windows before replacing the current portable package.

## Risks

Python integration is present below the command-line layer, especially in the
filesystem and exception interfaces.
Removing only the launcher would leave a hidden Python DLL dependency, so the
link-import check is an acceptance gate.

The release pipeline must produce assets that runtime startup currently creates
on demand.
Package creation must fail on missing or stale data so a broken archive is not
published.

Platform path conventions and Qt deployment differ.
Centralize path policy and cover each supported package layout rather than adding
platform-specific path guesses throughout engine startup.
