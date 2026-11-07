# Releasing Fast Villager Defense

The supported FVD distribution is a **native-only 7Z archive** containing the
single `FVDNative` CPack component. NSIS, generic launchers and embedded
Python distributions are unsupported.

1. Update `openage_version`, document changes, and merge accepted code to `FVD`.
2. Build and smoke-test Windows (`.\\build-fvd.cmd`) and Linux (`bash build-fvd.sh`) archives.
3. Inspect each extracted archive: no Python artifacts/imports, complete native dependencies, compatible tracked API, and verified asset SHA-256 hashes.
4. Manually verify graphical solo, audio, multiplayer host/guest/relay, clean exit, a Windows VM without Python, and Linux portability before public release.
5. Tag the accepted commit and attach both verified native archives to GitHub.

Windows archives preserve the portable version/platform suffix; Linux uses
`fvd.7z`. Both extract to `bin/`, `share/openage/`, and `etc/openage/`.
See [native packaging](../packaging/native_game/README.md).
