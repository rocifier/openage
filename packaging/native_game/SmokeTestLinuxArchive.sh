#!/usr/bin/env bash
# Copyright 2026 the openage authors. See copying.md for legal info.
# Verify an EXTRACTED Linux native release archive, not just the build tree.
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "Usage: bash SmokeTestLinuxArchive.sh <archive.7z> [--keep-extracted]" >&2
    exit 2
fi
archive="$(realpath "$1")"
[[ -f "$archive" ]] || { echo "Archive not found: $archive" >&2; exit 1; }
keep=0
if [[ $# -eq 2 ]]; then
    [[ "$2" == "--keep-extracted" ]] || { echo "Unknown option: $2" >&2; exit 2; }
    keep=1
fi
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/fvd-native-linux.XXXXXXXX")"
cleanup() {
    if ((keep)); then
        echo "Extracted archive retained for manual testing: $tmp"
    else
        rm -rf -- "$tmp"
    fi
}
trap cleanup EXIT

# CMake's libarchive reader supports 7Z. No p7zip required.
(cd "$tmp" && cmake -E tar xf "$archive")
mapfile -d '' -t games < <(find "$tmp" -type f -name openage-native -path '*/bin/*' -print0)
if ((${#games[@]} != 1)); then
    echo "Expected one bin/openage-native in archive, found ${#games[@]}" >&2
    exit 1
fi
game="${games[0]}"
bin="$(dirname "$game")"
stage="$(dirname "$bin")"
relay="$bin/openage-relay"
[[ -x "$relay" ]] || { echo "Packaged relay not found: $relay" >&2; exit 1; }

cmake "-DFVD_NATIVE_STAGE=$stage" -DFVD_NATIVE_REQUIRE_MANIFEST=ON \
    -P "$root/packaging/native_game/AuditNativePackage.cmake"

# ELF closure checks: exported executable and engine must resolve without Python.
export LD_LIBRARY_PATH="$stage/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
for elf in "$game" "$relay" "$stage/lib/libopenage.so"; do
    [[ -e "$elf" ]] || { echo "Missing runtime ELF: $elf" >&2; exit 1; }
    deps="$(ldd "$elf")"
    if grep -Eiq 'libpython|cython|not found' <<<"$deps"; then
        echo "Python ABI dependency or missing runtime library in $elf:" >&2
        echo "$deps" >&2
        exit 1
    fi
done
if find "$stage" -type f \( -name '*.py' -o -name '*.pyc' -o -name '*.pyd' \) -print -quit | grep -q .; then
    echo "Packaged Python source/bytecode/extension found" >&2
    exit 1
fi
echo "PASS: packaged Linux libraries resolve without Python."

help_text="$("$game" --help)"
grep -q 'Usage: openage-native' <<<"$help_text"
echo "PASS: help"
"$game" --version | grep -q 'openage-native'
echo "PASS: version"
"$game" --verify-assets | grep -q 'assets verified'
echo "PASS: asset integrity"

# Exercise a real native-only simulation bootstrap (same gate as Linux CI).
# A controlled timeout is expected; an early crash or missing initialization is not.
startlog="$tmp/native-startup.log"
set +e
timeout --signal=TERM --kill-after=3s 12s \
    "$game" --headless --solo --no-audio \
    --cfg-dir "$tmp/native-cfg" >"$startlog" 2>&1
rc=$?
set -e
if [[ $rc -ne 0 && $rc -ne 124 ]] || ! grep -Eq 'Using [0-9]+ threads' "$startlog"; then
    cat "$startlog" >&2
    echo "Native Linux simulation startup failed (exit $rc)." >&2
    exit 1
fi
echo "PASS: native headless solo initialized."
echo "Linux extracted archive smoke checks passed."
