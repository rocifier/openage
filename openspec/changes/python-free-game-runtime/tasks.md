# Tasks — shortest route to a Python-free FVD distribution

**Goal:** Ship FVD game and relay without **any Python interpreter, Python DLL/ABI, Python modules, or Cython extensions**. Python/Cython may remain **build-time/developer tools** (asset export, codegen, tests); rewriting or removing those tools is explicitly **not part of this work**.

**Priority:** Get a working **Windows portable 7Z** first. Reuse that native runtime for Linux/macOS afterwards. Do not add intermediate adapter migrations, dual-runtime support, infrastructure rewrites, or broad refactors unless the actual native link or gameplay test requires them.

## Existing work — keep, do not redo

- [x] Native C++ game entry point (`openage-native`) with CLI, platform paths, native filesystem mounts and config loading.
- [x] Standalone C++ relay executable, independent of Python/Cython, Qt, nyan and the game engine.
- [x] Native-only staging component plus Python-file/import audit tools.
- [x] Build-time exported-API checks, versioned SHA-256 asset-manifest generator and native manifest validation when present.

The native Windows archive has now passed its automated extracted-package audit and bounded solo-process check. **Full graphical gameplay, native relay multiplayer, clean-machine installation and clean shutdown remain manual acceptance gates.**

## Critical path — do these in order

- [x] **1. Make the actual game executable and its full runtime library graph Python/Cython-free (Windows verification).**
  - Build the release variant of `libopenage` and `openage-native` with no Python/Cython dependency. Remove Python-only source files/callbacks from the **runtime link**, replacing the few actually used by gameplay with native C++ implementations; keep the developer Python/Cython build intact.
  - **No separate adapter-migration milestone**: change only what blocks the native build/link/run. Do not require a full rearchitecture of `pyinterface` when unused code can simply be excluded from the release target.
  - Gate with a successful native startup and Windows `dumpbin` checks (plus Linux `ldd`/macOS `otool` when applicable) across the executable **and all shipped or loaded libraries**. A source-level or CLI-only pass is not enough.

- [x] **2. Produce a complete, extract-and-run Windows 7Z with the native launcher as the player entry point.**
  - Make the release packaging path ship **only** the native game/relay, engine/nyan libraries, Qt/QML/plugins, fonts, sound, shaders, FVD graphics/data and config. Package the pre-exported engine API and require the release SHA-256 manifest. Include all necessary native DLLs; fail the package build if inputs are missing.
  - Do **not** include the old embedded-Python launcher, interpreter, Python standard library, `.py`/`.pyc`/`.pyd` files or Cython extensions. No conversion or code generation on the player's machine.
  - Keep the source-tree developer launcher and tools available; no need to port them.

- [ ] **3. Prove the extracted Windows package works without Python installed (automated checks pass; manual acceptance pending).**
  - [x] Run extracted native `--help`, `--version`, invalid-input, missing-asset, successful `--verify-assets`, tampered-asset rejection and restored-asset validation.
  - [x] Audit **the extracted archive**, not just build outputs: 112 shipped Windows EXE/DLL binaries (including plugins), no bundled Python artifacts, no Python ABI imports, and no unresolved non-system DLL imports.
  - [x] Launch the extracted native solo executable for 30 seconds; verify it remains running and inspect its live loaded modules for Python DLLs (automated process smoke test).
  - [ ] On a **clean Windows machine/VM without Python**, launch the archive, observe actual graphical FVD gameplay and verify solo match start (a running process alone does not prove rendering or playability).
  - [ ] Host/join with two packaged native clients through the packaged native relay; verify gameplay replication and clean shutdown of both clients and relay.
  - Fix only defects uncovered by these tests; don't spend this milestone expanding unrelated legacy tests.

## One-command Windows release workflow

- [x] Provide a zero-argument `build-fvd.cmd` entry point from the repository root that handles asset export when absent, native-only CMake configuration, incremental game/relay build, strict 7Z packaging and extracted-archive smoke checks.
- [x] Automatically discover local 7-Zip and Visual Studio `dumpbin.exe`; place the archive in `dist/`, keep the extracted candidate for manual playtesting, and support optional API refresh/test skips.
- [x] Document the one-command path as the default, with manual CMake/CPack arguments reserved for diagnostics.
- [ ] Validate the complete wrapper on a fresh configured Windows developer checkout. Automated script syntax validation is wired into Windows CI; the actual end-to-end wrapper still requires a developer run.

## Unified Linux/Windows developer commands

- [x] Add zero-argument `bash build-fvd.sh` for Linux: reuse/export the engine API, incrementally compile the native game and relay, generate the strict native-only 7Z, and smoke-test the extracted package (ELF closure, Python-free contents, asset integrity and headless solo bootstrap).
- [x] Add shared `package`, `test`, `solo` and `relay` modes to the Windows and Linux scripts, including forwarded native arguments after the mode or `--`.
- [x] Make `solo` and `relay` reuse the latest packaged archive via a stable `dist/run/` extraction without invoking CMake or recompiling; let `test` run native CTest targets and package smoke verification independently.
- [x] Document the commands with no required CMake/CPack flags; add Linux shell syntax/usage tests and Windows PowerShell syntax checks in CI.
- [ ] Confirm the new convenience commands end-to-end on a real Windows and Linux developer checkout (CI syntax/argument tests alone do not prove full package portability).
- [ ] For public Linux distribution, validate runtime portability and Python-free execution on the target clean distro(s).

## Developer documentation cleanup

- [x] Make `doc/building.md` a direct entry point for native Windows/Linux FVD package, test, solo and relay commands.
- [x] Distinguish native player/relay runtime dependencies from the Python 3.12+/Cython 3.1+ requirements of asset export and legacy developer tools.
- [x] Remove stale platform-specific manual pip package lists; use the maintained `pyproject.toml` and `uv sync` for developer dependencies.
- [x] Update Windows, Linux distribution and macOS instructions; surface the FVD native workflow in the README and shorten the detailed FVD packaging guide.
- [ ] Final packaging transition and public release documentation remain blocked on manual graphical, multiplayer and clean-machine acceptance.

## Windows extracted-archive verification — 2026-11-06

**Reported result: automated smoke test passed after manifest/install filter fix.** The developer built and launched the Windows native executable, created the release 7Z and ran `packaging/native_game/SmokeTestWindowsArchive.ps1` against a newly extracted archive with `-KeepExtracted -ObserveSoloSeconds 30`.

- **PASS:** 112 staged Windows EXE/DLL imports audited; no Python artifacts/ABI dependencies; all non-system imported DLLs present.
- **PASS:** native CLI help, version, invalid arguments, nonexistent asset directory, valid manifest, deliberate SHA-256 asset tampering rejection, restored manifest verification.
- **PASS (bounded smoke):** native solo process stayed alive for 30 seconds with no Python DLL in the inspected live module list.
- **Packaging fix verified:** `GenerateNativeManifest.cmake` now excludes recursively omitted `README.md`/`prompts.md` entries; manifest filtering has cross-platform regression tests, and Windows staging validates assets before archive creation.
- **Not yet claimed:** full visual gameplay, multiplayer host/guest replication, clean shutdown, or independent machine without Python. Keep task 3 and final release acceptance open until these are explicitly reported.

## Follow-on after Windows passes

- [ ] **4. Apply the proven native packaging/ABI checks to Linux and macOS.** Reuse the game/relay code and pre-exported assets; provide equivalent native dependency deployment, headless/graphical smoke tests and no-Python process/package audits. Do not reopen adapter architecture work.
- [ ] **5. Minimal release cleanup and instructions.** Point the normal player distribution at the native executable, document developer-only Python/Cython commands separately, and remove obsolete Python *from the player package* (not from the repository).

## Definition of done

A user can extract the FVD player archive on a clean Windows machine with **no Python installed or bundled**, open the game, play solo, host/join a multiplayer match and quit. The archive and loaded-module/import audits contain **zero Python/Cython runtime components**. The dedicated relay remains a standalone native service. Only then mark this change complete and replace the existing release package.
