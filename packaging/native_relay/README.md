# FVD native relay (Python- and Qt-free)

The relay is a standalone console server using the existing FVD networking
code. Building or running it does **not** require Python, Cython, Qt, nyan,
game assets or an installed openage game engine.

## Build

From the repository root, using CMake and a C++20 compiler:

```sh
cmake -S packaging/native_relay -B build/native-relay -DCMAKE_BUILD_TYPE=Release
cmake --build build/native-relay --config Release
ctest --test-dir build/native-relay -C Release --output-on-failure
cmake --install build/native-relay --prefix ./relay-distribution --config Release
```

The result is `relay-distribution/bin/openage-relay` (or
`openage-relay.exe` on Windows). It does not link against `libopenage`.

## Run

```sh
./relay-distribution/bin/openage-relay --listen 0.0.0.0 --port 32456
```

Add `--player-token` followed by a **64-hex-character** token, once per
authorized player, for a private relay. Without tokens, the relay is public.
Defaults: `--max-rooms 64` and `--max-connections 128`.

Use `--help` and `--version` for diagnostics. On Unix, SIGINT/SIGTERM stops
the relay and its active sessions; Windows console Ctrl+C also sends SIGINT.

### Host deployment notes

Copy **only** the installed native relay executable to the host (and any
standard runtime C++ libraries required by that platform). No `python.exe`,
`python3`, `libopenage`, Qt libraries, Python packages, Cython extensions,
nyan or game asset directories are required.

Build on the host's distribution or an ABI-compatible environment; an executable
built against newer glibc may not run on an older Linux host. Open TCP port
32456 (or the port you configured) in the firewall. Test room listing and a
real host/guest connection before switching production traffic.
