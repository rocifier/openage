#!/usr/bin/env bash
# Copyright 2026 the openage authors. See copying.md for legal info.
# Linux FVD developer build/run; packaging is an explicit release-only command.
# Usage: bash build-fvd.sh [package|build|test|python-test|game|solo|relay] [options]
set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "This script targets Linux; use build-fvd.cmd for Windows." >&2
    exit 1
fi

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
jobs="$(nproc 2>/dev/null || echo 4)"
nyan_version="${NYAN_CLONE_VERSION:-v0.3.1}"
mode=build
forwarded=()
if (($#)) && [[ "$1" =~ ^(package|build|test|python-test|game|solo|relay)$ ]]; then
    mode="$1"
    shift
fi

usage() {
    cat <<'USAGE'
FVD Linux native developer commands

  bash build-fvd.sh                     Incrementally build game and relay
  bash build-fvd.sh build               Incrementally build the native game and relay
  bash build-fvd.sh test                Run native CTest (optional native runner args)
  bash build-fvd.sh python-test         Run pure Python tests, no CMake/Cython
  bash build-fvd.sh game                Build and open local multiplayer lobby
  bash build-fvd.sh solo --no-audio     Build and launch local solo gameplay
  bash build-fvd.sh relay --port 40000  Build and start local native relay
  bash build-fvd.sh relay -- --help     Pass any native options after --
  bash build-fvd.sh package             Build a release and create 7Z (no extraction)
  bash build-fvd.sh --jobs 4            Limit parallel compilation

Requires an existing Linux C++/Qt/CMake development setup; CMake, CPack,
a C++ compiler and nyan development dependencies. Building requires no
Python, Cython or Python packages; only 'python-test' needs Python 3.12+.
USAGE
}

while (($#)); do
    case "$1" in
        --) shift; forwarded=("$@"); break ;;
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

# All play/test commands use local native builds. Release-only CMake
# configuration is enabled exclusively for an explicit packaging command.
native_release=OFF
if [[ "$mode" == "package" ]]; then
    native_release=ON
fi
# Let cmake --build auto-regenerate when CMake files change. Reconfigure
# only on first use, release-mode switches, or when packaging needs a
# refreshed asset integrity manifest.
if [[ ! -f "$build/CMakeCache.txt" ]] ||
   ! grep -qx "FVD_NATIVE_RELEASE:BOOL=$native_release" "$build/CMakeCache.txt" ||
   [[ "$mode" == "package" ]]; then
    step "Configure native FVD ($native_release)"
    cmake -S "$root" -B "$build" "${initial[@]}" \
        "-DFVD_NATIVE_RELEASE=$native_release" \
        "-DNYAN_CLONE_VERSION=$nyan_version"
fi

case "$mode" in
    game|solo) targets=(openage_native_game) ;;
    relay) targets=(openage_native_relay) ;;
    test) targets=(openage_native_game openage_native_relay
                   openage_native_game_args_test openage_native_relay_args_test
                   openage_native_relay_network_test openage_native_cpp_tests) ;;
    *) targets=(openage_native_game openage_native_relay) ;;
esac
step "Incremental native $mode build"
cmake --build "$build" --config Release --parallel "$jobs" --target "${targets[@]}"

case "$mode" in
    game|solo)
        exe="$build/packaging/openage-native"
        [[ -x "$exe" ]] || { echo "Native game executable missing: $exe" >&2; exit 1; }
        args=(--asset-dir "$root/assets")
        if [[ "$mode" == "solo" ]]; then args+=(--solo); fi
        step "Launch freshly built FVD $mode (no archive)"
        (cd "$root" && exec "$exe" "${args[@]}" "${forwarded[@]}")
        ;;
    relay)
        exe="$build/packaging/openage-relay"
        [[ -x "$exe" ]] || { echo "Native relay executable missing: $exe" >&2; exit 1; }
        step "Launch freshly built FVD relay (no archive)"
        (cd "$root" && exec "$exe" "${forwarded[@]}")
        ;;
    test)
        step "Run native game/relay CTest suite"
        ctest --test-dir "$build" -C Release --output-on-failure -R '^fvd_native_(game|relay)_'
        if (("${#forwarded[@]}" > 0)); then
            runner="$build/packaging/openage_native_cpp_tests"
            [[ -x "$runner" ]] || { echo "Missing native C++ runner: $runner" >&2; exit 1; }
            "$runner" "${forwarded[@]}"
        fi
        echo "Native Linux tests passed."
        ;;
    build)
        echo "FVD local native game and relay are up to date."
        ;;
    package)
        command -v cpack >/dev/null 2>&1 || { echo "Missing packaging tool: cpack" >&2; exit 1; }
        mkdir -p "$dist"
        step "Create native release 7Z (no extraction)"
        cpack --config "$build/CPackConfig.cmake" -C Release -G 7Z -B "$dist"
        archive="$dist/fvd.7z"
        [[ -f "$archive" ]] || { echo "CPack produced no $archive" >&2; exit 1; }
        printf '\\nRelease archive: %s\\n' "$archive"
        echo "Archive extraction and smoke tests are separate manual deployment/acceptance steps."
        ;;
esac
