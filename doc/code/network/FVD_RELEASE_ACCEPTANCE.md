# FVD native release acceptance — Task 6

Status: **in verification**, 7 November 2026. This document distinguishes CI evidence from the manual checks still needed. The strict native player package is `FVDNative` in a 7Z archive; the original-game converter, embedded Python, NSIS launcher and `bin/run` are unsupported.

## Automated evidence and release gates

| Gate | Automated check | What it proves / does not prove |
| --- | --- | --- |
| Pure Python helper tests (6.2) | `fvd-native-game-args.yml` runs `python-test` with an isolated Python 3.12 environment | Developer-only test suite, independent from native packaging |
| Python physically absent on Linux (6.1) | `fvd-native-no-python-build.yml` builds game, relay and native tests in a fresh Ubuntu container with no Python installation, then packages and smoke-tests the extracted archive | Linux native absence when CI passes; **not** proof for Windows |
| Windows build without Python (6.1) | Requires a fresh Windows VM/toolchain with no Python interpreter or development libraries | **Manual acceptance outstanding** until a build log from that environment is recorded |
| C++ game, relay, audio, multiplayer (6.3) | Native CTest registry, audio placeholder decoding test, relay lobby and multi-session tests | Headless/regression coverage; not actual sound playback or visual parity on two clients |
| Native archive (6.3) | Windows: `SmokeTestWindowsArchive.ps1`, DLL audit, solo observation. Linux: `SmokeTestLinuxArchive.sh`, ELF closure, headless solo. Both: API version, manifests and full SHA-256 asset verification | Verifies staging, relocatability and startup on CI host; not all graphical/user scenarios |
| Source and quality (6.4–6.6) | `fvd-native-no-python-audit.yml` and `fvd-quality.yml` | Prohibits retired adapters/launchers and checks changed-file source quality |

**Passing `-DCMAKE_DISABLE_FIND_PACKAGE_Python=TRUE` alone does not satisfy 6.1.** The container job checks the actual absence of Python executables and libpython on Linux; a clean Windows VM is still required.

## Windows — clean machine and two-client test

1. Install only MSVC C++ Build Tools, CMake/CPack, vcpkg's native Qt/nyan dependencies and 7-Zip on a fresh Windows build VM **without Python, Cython, Mako or Python development headers**. Confirm the Python commands are absent, then run `.\\build-fvd.cmd`. Save the configuration log and archive path.
2. On a clean player VM without Python, extract that **same** portable 7Z archive. Run `bin\\openage-native.exe --verify-assets`, then `bin\\openage-native.exe` to open the lobby. Confirm UI, music, attack/build/death sounds, sprites and the absence of missing QML or DLL messages.
3. Run `bin\\openage-relay.exe --listen 0.0.0.0 --port 40000` on the trusted host. On two Windows PCs launch `.\\build-fvd.cmd game` (using the same packaged build on both), host and join a lobby, start a game, test Builder tower building/garrison, Hunter movement/combat, replicated sound/animation and a rematch. Use [multiplayer acceptance details](WINDOWS_MULTIPLAYER_ACCEPTANCE.md).
4. Close the game normally; check it exits code 0 and releases its room. Stop the relay normally and confirm no port/room leak. Repeat with the Hunter hosted by either PC. Store logs and note any graphics/audio differences.

## Linux — clean container and native archive test

The no-Python CI job invokes CMake, the developer C++ test runner, native audio-bank test, relay/multi-session tests, CPack and the **extracted archive** smoke test in one container. For local verification:

```bash
bash build-fvd.sh test
bash build-fvd.sh package
bash build-fvd.sh relay --listen 127.0.0.1 --port 40000
# In another terminal: bash build-fvd.sh game
```

Then repeat multiplayer and audio on a real desktop, including normal close/relay shutdown, and verify the archive on another compatible Linux distribution. The Linux archive is not automatically portable to every distro because Qt and native system libraries differ.

## Sign-off

Record date, commit, OS/compiler, Python absence proof, native test totals, archive hash, actual audio/visual/gameplay result and log location for each manual platform. **Do not mark 6.1 or 6.3 complete** until the corresponding clean-machine and real host/guest/audio/exit checks have actually been performed. Native code/CI evidence may support partial verification without substituting for those acceptance conditions.
