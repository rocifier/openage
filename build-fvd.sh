#!/usr/bin/env bash
# Copyright 2026 the openage authors. See copying.md for legal info.
# One-command Linux FVD native candidate: build, package and smoke-test.
# Usage: bash build-fvd.sh [package|build|test|python-test|game|solo|relay] [options]
set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "This script targets Linux; use build-fvd.cmd for Windows." >&2
    exit 1
fi

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
jobs="$(nproc 2>/dev/null || echo 4)"
nyan_version="${NYAN_CLONE_VERSION:-v0.3.1}"
skip_smoke=0
mode=package
forwarded=()
if (($#)) && [[ "$1" =~ ^(package|build|test|python-test|game|solo|relay)$ ]]; then
    mode="$1"
    shift
fi

usage() {
    cat <<'USAGE'
FVD Linux native release candidate

  bash build-fvd.sh                     Build, package and verify
  bash build-fvd.sh build               Build the native game and relay
  bash build-fvd.sh test                Run native CTest (optional native runner args)
  bash build-fvd.sh python-test         Run pure Python tests, no CMake/Cython
  bash build-fvd.sh game                Open packaged multiplayer lobby
  bash build-fvd.sh solo --no-audio     Launch packaged native solo gameplay
  bash build-fvd.sh relay --port 40000  Start packaged native relay
  bash build-fvd.sh relay -- --help     Pass any native options after --
  bash build-fvd.sh --skip-smoke        Create the archive without extracting it
  bash build-fvd.sh --jobs 4            Limit parallel compilation

Requires an existing Linux C++/Qt/CMake development setup; CMake, CPack,
a C++ compiler and nyan development dependencies. Building requires no
Python, Cython or Python packages; only 'python-test' needs Python 3.12+.
USAGE
}

while (($#)); do
    case "$1" in
        --) shift; forwarded=("$@"); break ;;
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
            if [[ "$mode" == "game" || "$mode" == "solo" || "$mode" == "relay" || "$mode" == "python-test" || "$mode" == "test" ]]; then
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

# This optional suite is pure stdlib Python and needs no configured build.
if [[ "$mode" == "python-test" ]]; then
    python_bin="$root/.venv/bin/python"
    if [[ ! -x "$python_bin" ]]; then
        python_bin="$(command -v python3 || true)"
    fi
    [[ -n "$python_bin" ]] || { echo "Python 3 is not installed" >&2; exit 1; }
    if (("${#forwarded[@]}" == 0)); then
        forwarded=("openage.testing.test_fvd_network_benchmark")
    fi
    (cd "$root" && "$python_bin" -m unittest -v "${forwarded[@]}")
    exit 0
fi

for tool in cmake c++; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing build tool: $tool" >&2
        exit 1
    fi
done

# Reuse a configured native developer build when present. Otherwise use build/.
if [[ -f "$root/bin/CMakeCache.txt" ]]; then
    build="$root/bin"
elif [[ -f "$root/build/CMakeCache.txt" ]]; then
    build="$root/build"
else
    build="$root/build"
fi
api="$root/assets/fvd/engine_api"
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

if [[ "$mode" == "game" || "$mode" == "solo" || "$mode" == "relay" ]]; then
    archive="$(find_archive)"
    extract_for_play "$archive"
    if [[ "$mode" == "solo" ]]; then
        step "Launch native FVD solo (Ctrl+C to stop)"
        (cd "$bin_dir" && exec ./openage-native --solo "${forwarded[@]}")
    elif [[ "$mode" == "game" ]]; then
        step "Launch native FVD multiplayer lobby (Ctrl+C to stop)"
        (cd "$bin_dir" && exec ./openage-native "${forwarded[@]}")
    else
        step "Launch native FVD relay (Ctrl+C to stop)"
        (cd "$bin_dir" && exec ./openage-relay "${forwarded[@]}")
    fi
    exit 0
fi

if [[ "$mode" == "test" ]]; then
    [[ -f "$build/CMakeCache.txt" ]] || { echo "No configured build. First run: bash build-fvd.sh build" >&2; exit 1; }
    step "Configure native-only C++ test suite"
    cmake -S "$root" -B "$build" -DFVD_NATIVE_RELEASE=OFF
    step "Build native test targets (incremental)"
    cmake --build "$build" --parallel "$jobs" \
        --target openage_native_game_args_test openage_native_relay_args_test openage_native_relay_network_test openage_native_cpp_tests
    step "Run native game/relay CTest suite"
    ctest --test-dir "$build" --output-on-failure -R '^fvd_native_(game|relay)_'
    if (("${#forwarded[@]}" > 0)); then
        runner="$build/packaging/openage_native_cpp_tests"
        [[ -x "$runner" ]] || { echo "Missing native C++ runner: $runner" >&2; exit 1; }
        "$runner" "${forwarded[@]}"
    fi
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

# The tracked source snapshot replaces the ignored converter output.
for required in modpack.toml manifest.toml root.nyan; do
    if [[ ! -f "$api/$required" ]]; then
        echo "Tracked FVD engine API incomplete: missing $api/$required" >&2
        exit 1
    fi
done

native_release=ON
configure_description="Configure native-only FVD release"
if [[ "$mode" == "build" ]]; then
    native_release=OFF
    configure_description="Configure native-only FVD developer build"
fi
step "$configure_description"
cmake -S "$root" -B "$build" "${initial[@]}" \
    "-DFVD_NATIVE_RELEASE=$native_release" \
    "-DNYAN_CLONE_VERSION=$nyan_version"

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
