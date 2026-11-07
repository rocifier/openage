#!/usr/bin/env bash
# Copyright 2026 the openage authors. See copying.md for legal info.
# Fast, dependency-free simulation of the Linux developer command contract.
set -euo pipefail

source_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
tmp="$(mktemp -d)"
trap 'rm -rf -- "$tmp"' EXIT
mkdir -p "$tmp/build/packaging" "$tmp/assets/fvd/engine_api" "$tmp/mocks"
cp "$source_root/build-fvd.sh" "$tmp/build-fvd.sh"
touch "$tmp/assets/fvd/engine_api/modpack.toml"
touch "$tmp/assets/fvd/engine_api/manifest.toml"
touch "$tmp/assets/fvd/engine_api/root.nyan"
printf 'FVD_NATIVE_RELEASE:BOOL=OFF\n' > "$tmp/build/CMakeCache.txt"

cat > "$tmp/mocks/cmake" <<'MOCK'
#!/usr/bin/env bash
printf 'cmake %s\n' "$*" >> "$FVD_TEST_LOG"
if [[ "$1" == "-S" ]]; then
    for arg in "$@"; do
        case "$arg" in
            -DFVD_NATIVE_RELEASE=*)
                printf 'FVD_NATIVE_RELEASE:BOOL=%s\n' "${arg#*=}" > "$FVD_TEST_ROOT/build/CMakeCache.txt"
                ;;
        esac
    done
fi
MOCK
cat > "$tmp/mocks/ctest" <<'MOCK'
#!/usr/bin/env bash
printf 'ctest %s\n' "$*" >> "$FVD_TEST_LOG"
MOCK
cat > "$tmp/mocks/cpack" <<'MOCK'
#!/usr/bin/env bash
printf 'cpack %s\n' "$*" >> "$FVD_TEST_LOG"
touch "$FVD_TEST_ROOT/dist/fvd.7z"
MOCK
cat > "$tmp/build/packaging/openage-native" <<'MOCK'
#!/usr/bin/env bash
printf 'game %s\n' "$*" >> "$FVD_TEST_LOG"
MOCK
cat > "$tmp/build/packaging/openage-relay" <<'MOCK'
#!/usr/bin/env bash
printf 'relay %s\n' "$*" >> "$FVD_TEST_LOG"
MOCK
cat > "$tmp/build/packaging/openage_native_cpp_tests" <<'MOCK'
#!/usr/bin/env bash
printf 'cpp-tests %s\n' "$*" >> "$FVD_TEST_LOG"
MOCK
chmod +x "$tmp/mocks/"* "$tmp/build/packaging/"*
export FVD_TEST_ROOT="$tmp" FVD_TEST_LOG="$tmp/operations.log"
export PATH="$tmp/mocks:$PATH"

check() {
    if ! grep -Fq -- "$1" "$FVD_TEST_LOG"; then
        echo "Expected log to contain: $1" >&2
        cat "$FVD_TEST_LOG" >&2
        exit 1
    fi
}
run() {
    : > "$FVD_TEST_LOG"
    (cd / && bash "$tmp/build-fvd.sh" "$@")
}
no_archive() {
    if grep -Eq '^cpack |extract|dist/run' "$FVD_TEST_LOG"; then
        echo "Local development unexpectedly accessed an archive" >&2
        exit 1
    fi
}

run solo --no-audio
check 'cmake --build'
check '--target openage_native_game'
check "game --asset-dir $tmp/assets --solo --no-audio"
no_archive
[[ ! -d "$tmp/dist" ]]

run game --window-mode windowed
check "game --asset-dir $tmp/assets --window-mode windowed"
no_archive

run relay --port 4242
check '--target openage_native_relay'
check 'relay --port 4242'
no_archive

run
check '--target openage_native_game openage_native_relay'
no_archive

run test --list
check 'ctest --test-dir'
check 'cpp-tests --list'
no_archive

# Switching back from a release configuration must always restore
# developer mode before launching local source assets.
printf 'FVD_NATIVE_RELEASE:BOOL=ON\n' > "$tmp/build/CMakeCache.txt"
run solo
check '-DFVD_NATIVE_RELEASE=OFF'
check "game --asset-dir $tmp/assets --solo"
no_archive

run package
check '-DFVD_NATIVE_RELEASE=ON'
check 'cpack --config'
[[ -f "$tmp/dist/fvd.7z" ]]
if grep -q '^game ' "$FVD_TEST_LOG"; then
    echo "Release archive was unexpectedly launched" >&2
    exit 1
fi
echo 'PASS: Linux local developer commands build/run directly without archives'
