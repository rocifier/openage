#!/usr/bin/env bash
# Copyright 2026 the openage authors. See copying.md for legal info.
# One-command Linux FVD native candidate: build, package and smoke-test.
# Usage: bash build-fvd.sh [package|build|test|legacy-test|solo|relay] [options]
set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "This script targets Linux; use build-fvd.cmd for Windows." >&2
    exit 1
fi

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
jobs="$(nproc 2>/dev/null || echo 4)"
nyan_version="${NYAN_CLONE_VERSION:-v0.3.1}"
reexport_api=0
skip_smoke=0
mode=package
forwarded=()
if (($#)) && [[ "$1" =~ ^(package|build|test|legacy-test|solo|relay)$ ]]; then
    mode="$1"
    shift
fi

usage() {
    cat <<'USAGE'
FVD Linux native release candidate

  bash build-fvd.sh                  Build, package and verify
  bash build-fvd.sh build            Build the native game and relay
  bash build-fvd.sh test             Run native CTest
  bash build-fvd.sh legacy-test      Build and run the legacy C++/Python tests
  bash build-fvd.sh solo --no-audio  Launch packaged native solo gameplay
  bash build-fvd.sh relay --port 32456  Start packaged native relay
  bash build-fvd.sh relay -- --help  Pass any native options after --
  bash build-fvd.sh --reexport-api   Refresh the developer-exported nyan API
  bash build-fvd.sh --skip-smoke     Create the archive without extracting it
  bash build-fvd.sh --jobs 4         Limit parallel compilation

Requires an existing Linux C++/Qt/CMake development setup; CMake, CPack,
a C++ compiler and nyan development dependencies. Python, Cython and the
declared Python modules are build-time tools and are not shipped.
USAGE
}

while (($#)); do
    case "$1" in
        --) shift; forwarded=("$@"); break ;;
        --reexport-api) reexport_api=1; shift ;;
        --skip-smoke) skip_smoke=1; shift ;;
        --jobs)
            if (($# < 2)) || [[ ! "$2" =~ ^[1-9][0-9]*$ ]]; then
                echo "--jobs needs a positive integer" >&2
                exit 2
            fi
            jobs="$2"
            shift 2
            ;;
        -h|--help) usage; exit 0 ;;
        *)
            if [[ "$mode" == "legacy-test" || "$mode" == "solo" || "$mode" == "relay" ]]; then
                forwarded+=("$1")
                shift
            else
                echo "Unknown argument: $1" >&2
                usage >&2
                exit 2
            fi
            ;;
    esac
done

for tool in cmake c++; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing build tool: $tool" >&2
        exit 1
    fi
done

# Reuse the preconfigured developer build if one exists. ./configure uses
# a bin symlink; Linux CI commonly uses a separate build directory.
if [[ -f "$root/bin/CMakeCache.txt" ]]; then
    build="$root/bin"
elif [[ -f "$root/build/CMakeCache.txt" ]]; then
    build="$root/build"
else
    build="$root/build"
fi
api="$root/assets/converted/engine"
converted="$root/assets/converted"
dist="$root/dist"

step() { printf '\n==> %s\n' "$*"; }

# Launch commands act on the most recent archive and do not reconfigure/build.
find_archive() {
    [[ -d "$dist" ]] || { echo "No Linux native package found; run: bash build-fvd.sh" >&2; exit 1; }
    local candidate listing
    while IFS= read -r candidate; do
        candidate="${candidate#* }"
        listing="$(cmake -E tar tf "$candidate" 2>/dev/null)" || continue
        if grep -Eq '(^|/)bin/openage-native$' <<< "$listing"; then
            printf '%s\n' "$candidate"
            return
        fi
    done < <(find "$dist" -maxdepth 1 -type f -name '*.7z' -printf '%T@ %p\n' | sort -nr)

    echo "No Linux native 7Z found in $dist; run: bash build-fvd.sh" >&2
    exit 1
}

extract_for_play() {
    local archive="$1" stamp expected
    local run_dir="$dist/run"
    expected="$(stat -c '%s:%Y' "$archive"):$archive"
    stamp=""
    [[ -f "$run_dir/.archive-stamp" ]] && stamp="$(cat "$run_dir/.archive-stamp")"
    if [[ "$stamp" != "$expected" ]]; then
        step "Extract native Linux package to $run_dir"
        rm -rf -- "$run_dir"
        mkdir -p "$run_dir"
        (cd "$run_dir" && cmake -E tar xf "$archive")
        printf '%s\n' "$expected" > "$run_dir/.archive-stamp"
    fi
    mapfile -d '' -t game_paths < <(find "$run_dir" -type f -name openage-native -path '*/bin/*' -print0)
    if ((${#game_paths[@]} != 1)); then
        echo "Expected one native game in $run_dir, found ${#game_paths[@]}" >&2
        exit 1
    fi
    bin_dir="$(dirname "${game_paths[0]}")"
    stage_dir="$(dirname "$bin_dir")"
    export LD_LIBRARY_PATH="$stage_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
}

if [[ "$mode" == "solo" || "$mode" == "relay" ]]; then
    archive="$(find_archive)"
    extract_for_play "$archive"
    if [[ "$mode" == "solo" ]]; then
        step "Launch native FVD solo (Ctrl+C to stop)"
        (cd "$bin_dir" && exec ./openage-native --solo "${forwarded[@]}")
    else
        step "Launch native FVD relay (Ctrl+C to stop)"
        (cd "$bin_dir" && exec ./openage-relay "${forwarded[@]}")
    fi
    exit 0
fi

if [[ "$mode" == "test" ]]; then
    [[ -f "$build/CMakeCache.txt" ]] || { echo "No configured build. First run: bash build-fvd.sh build" >&2; exit 1; }
    step "Build native test targets (incremental)"
    cmake --build "$build" --parallel "$jobs" \
        --target openage_native_game_args_test openage_native_relay_args_test openage_native_relay_network_test
    step "Run native game/relay CTest suite"
    ctest --test-dir "$build" --output-on-failure -R '^fvd_native_(game|relay)_'
    echo "Native Linux tests passed."
    exit 0
fi


# For a fresh checkout only, initialize Ninja and download the nyan source.
# Existing generators and toolchain settings remain untouched.
initial=()
if [[ ! -f "$build/CMakeCache.txt" ]]; then
    if ! command -v ninja >/dev/null 2>&1; then
        echo "Install Ninja (e.g. apt install ninja-build), or configure CMake first." >&2
        exit 1
    fi
    initial=(-G Ninja -DDOWNLOAD_NYAN=YES -DCMAKE_BUILD_TYPE=Release)
fi

if [[ "$mode" == "legacy-test" ]]; then
    python_args=()
    if [[ -x "$root/.venv/bin/python" ]]; then
        python_args=("-DPython3_EXECUTABLE=$root/.venv/bin/python")
    fi
    step "Configure Python-enabled developer tests"
    cmake -S "$root" -B "$build" "${initial[@]}" "${python_args[@]}" \
        -DFVD_NATIVE_RELEASE=OFF \
        -DFVD_WITH_PYTHON_ADAPTERS=ON \
        "-DNYAN_CLONE_VERSION=$nyan_version"
    step "Build Python-enabled developer tests"
    cmake --build "$build" --parallel "$jobs"
    [[ -x "$build/run" ]] || { echo "Cannot locate legacy test runner at $build/run" >&2; exit 1; }
    test_args=("${forwarded[@]}")
    if ((${#test_args[@]} == 0)); then
        test_args=(-a)
    fi
    step "Run legacy C++/Python tests"
    (cd "$build" && ./run test "${test_args[@]}")
    exit 0
fi

api_valid() {
    [[ -f "$api/modpack.toml" && -f "$api/manifest.toml" ]] &&
        grep -Eq '^[[:space:]]*version[[:space:]]*=[[:space:]]*"0[.]5[.]0"' "$api/modpack.toml"
}

if ((reexport_api)) || ! api_valid; then
    step "Build developer tools to export the engine API (build-time Python only)"
    cmake -S "$root" -B "$build" "${initial[@]}" \
        -DFVD_NATIVE_RELEASE=OFF -DFVD_WITH_PYTHON_ADAPTERS=ON \
        "-DNYAN_CLONE_VERSION=$nyan_version"
    initial=()
    cmake --build "$build" --parallel "$jobs"

    if [[ ! -x "$build/run" ]]; then
        echo "Cannot locate developer launcher at $build/run" >&2
        exit 1
    fi
    mkdir -p "$converted"
    step "Export compatible engine nyan API"
    (cd "$build" && ./run convert-export-api "$converted")
else
    step "Reuse existing engine API; skipping the developer/Cython rebuild"
fi
if ! api_valid; then
    echo "Missing/incompatible engine API in $api after export (requires version 0.5.0)" >&2
    exit 1
fi

native_release=ON
configure_description="Configure native-only FVD release"
if [[ "$mode" == "build" ]]; then
    native_release=OFF
    configure_description="Configure native-only FVD developer build"
fi
step "$configure_description"
cmake -S "$root" -B "$build" "${initial[@]}" \
    "-DFVD_NATIVE_RELEASE=$native_release" \
    -DFVD_WITH_PYTHON_ADAPTERS=OFF \
    "-DNYAN_CLONE_VERSION=$nyan_version" \
    "-DFVD_NATIVE_API_ASSET_DIR=$api"

step "Incrementally build native game and relay"
cmake --build "$build" --config Release --parallel "$jobs" \
    --target openage_native_game openage_native_relay

if [[ "$mode" == "build" ]]; then
    echo "Native Linux game and relay build completed."
    exit 0
fi

if ! command -v cpack >/dev/null 2>&1; then
    echo "Missing packaging tool: cpack" >&2
    exit 1
fi

mkdir -p "$dist"
step "Create native 7Z archive (no shipped Python/Cython)"
cpack --config "$build/CPackConfig.cmake" -C Release -G 7Z -B "$dist"

# -printf is available on the GNU find used by the supported Linux build.
archive="$(find_archive)"
if [[ -z "$archive" || ! -f "$archive" ]]; then
    echo "CPack produced no 7Z archive in $dist" >&2
    exit 1
fi
printf '\nPackage: %s\n' "$archive"

if (( ! skip_smoke )); then
    step "Smoke-test the extracted package"
    bash "$root/packaging/native_game/SmokeTestLinuxArchive.sh" "$archive" --keep-extracted
else
    echo "Smoke test skipped (--skip-smoke)."
fi

printf '\nLinux FVD native candidate: %s\n' "$archive"
echo "Final release still requires graphical/multiplayer and clean-distro validation."
