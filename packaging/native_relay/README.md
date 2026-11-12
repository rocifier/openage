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
./relay-distribution/bin/openage-relay --listen 0.0.0.0 --port 40000
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
40000 (or the port you configured) in the firewall. Test room listing and a
real host/guest connection before switching production traffic.

## Pelican's unmodified Python Generic egg (upload-only)

For hosts where you **cannot** change the egg, Python image or Docker installation,
use the CI-produced **`fvd-pelican-relay.zip`** instead of the ordinary standalone
binary above. The stock egg runs `app.py` using its already-installed Python
interpreter; the bundled `app.py` imports `relay.py`, which `os.execv`-replaces
Python with a prebuilt **fully static musl C++ relay**. No host compiling,
library installation, pip packages or archive extraction at startup.

The archive contains both Linux amd64 and arm64 relay executables under
`bin/`; the launcher detects the host CPU, binds to Pelican's `SERVER_PORT`
and preserves the native relay's SIGINT/SIGTERM shutdown behavior. The optional
`relay.json` can set room and connection limits and private player tokens.
The bundled `README.md` contains the exact file layout and panel settings.
See [Pelican upload instructions](pelican/README.md) and
[CI bundle workflow](../../.github/workflows/fvd-pelican-relay.yml).

**Do not** upload a Windows relay, a Linux binary built against a newer glibc,
or the ordinary standalone build into a Python egg and expect it to run.
The portable CI build is compiled and tested outside Pelican, then exercised
inside the stock Pelican `python_3.12` image before release.
