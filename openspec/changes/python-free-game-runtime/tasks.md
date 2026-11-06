# Tasks — shortest route to a Python-free FVD distribution

**Goal:** Ship FVD game and relay without **any Python interpreter, Python DLL/ABI, Python modules, or Cython extensions**. Python/Cython may remain **build-time/developer tools** (asset export, codegen, tests); rewriting or removing those tools is explicitly **not part of this work**.

**Priority:** Get a working **Windows portable 7Z** first. Reuse that native runtime for Linux/macOS afterwards. Do not add intermediate adapter migrations, dual-runtime support, infrastructure rewrites, or broad refactors unless the actual native link or gameplay test requires them.

## Existing work — keep, do not redo

- [x] Native C++ game entry point (`openage-native`) with CLI, platform paths, native filesystem mounts and config loading.
- [x] Standalone C++ relay executable, independent of Python/Cython, Qt, nyan and the game engine.
- [x] Native-only staging component plus Python-file/import audit tools.
- [x] Build-time exported-API checks, versioned SHA-256 asset-manifest generator and native manifest validation when present.

These are **building blocks, not proof of a shippable Python-free game**. The game library's remaining Python-facing code, complete DLL deployment, and real gameplay verification are still open.

## Critical path — do these in order

- [ ] **1. Make the actual game executable and its full runtime library graph Python/Cython-free.**
  - Build the release variant of `libopenage` and `openage-native` with no Python/Cython dependency. Remove Python-only source files/callbacks from the **runtime link**, replacing the few actually used by gameplay with native C++ implementations; keep the developer Python/Cython build intact.
  - **No separate adapter-migration milestone**: change only what blocks the native build/link/run. Do not require a full rearchitecture of `pyinterface` when unused code can simply be excluded from the release target.
  - Gate with a successful native startup and Windows `dumpbin` checks (plus Linux `ldd`/macOS `otool` when applicable) across the executable **and all shipped or loaded libraries**. A source-level or CLI-only pass is not enough.

- [ ] **2. Produce a complete, extract-and-run Windows 7Z with the native launcher as the player entry point.**
  - Make the release packaging path ship **only** the native game/relay, engine/nyan libraries, Qt/QML/plugins, fonts, sound, shaders, FVD graphics/data and config. Package the pre-exported engine API and require the release SHA-256 manifest. Include all necessary native DLLs; fail the package build if inputs are missing.
  - Do **not** include the old embedded-Python launcher, interpreter, Python standard library, `.py`/`.pyc`/`.pyd` files or Cython extensions. No conversion or code generation on the player's machine.
  - Keep the source-tree developer launcher and tools available; no need to port them.

- [ ] **3. Prove the extracted Windows package works without Python installed.**
  - Run native `--help`, `--version`, invalid-input and missing-asset tests; launch real FVD graphics; complete a solo match start; host/join between two native clients (through the native relay where applicable); verify gameplay replication and clean exit.
  - Audit **the extracted archive**, not just build outputs: no Python artifacts, no Python ABI imports in EXE/DLL/Qt plugin closure, and no Python DLL loaded by a running game. Block release on failure.
  - Fix only defects uncovered by these tests; don't spend this milestone expanding unrelated legacy tests.

## Follow-on after Windows passes

- [ ] **4. Apply the proven native packaging/ABI checks to Linux and macOS.** Reuse the game/relay code and pre-exported assets; provide equivalent native dependency deployment, headless/graphical smoke tests and no-Python process/package audits. Do not reopen adapter architecture work.
- [ ] **5. Minimal release cleanup and instructions.** Point the normal player distribution at the native executable, document developer-only Python/Cython commands separately, and remove obsolete Python *from the player package* (not from the repository).

## Definition of done

A user can extract the FVD player archive on a clean Windows machine with **no Python installed or bundled**, open the game, play solo, host/join a multiplayer match and quit. The archive and loaded-module/import audits contain **zero Python/Cython runtime components**. The dedicated relay remains a standalone native service. Only then mark this change complete and replace the existing release package.
