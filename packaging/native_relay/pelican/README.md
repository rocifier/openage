# FVD relay for Pelican's stock Python Generic egg

Upload the **contents** of the `fvd-pelican-relay.zip` release artifact to the
Pelican server's **`/home/container/`** file directory. After extraction, the
layout is:

```text
/home/container/
  app.py                           # stock egg default PY_FILE
  relay.py                         # stdlib-only native relay launcher
  relay.example.json               # optional configuration template
  bin/
    openage-relay-linux-amd64      # static native C++ executable
    openage-relay-linux-arm64      # static native C++ executable
```

No pip requirements, build tools, native C++ runtime libraries, image/egg
edits, installation-script edits or superuser permissions are needed.

## Existing egg settings

Use Pelican's existing **Python Generic** egg:

- `PY_FILE` = `app.py` (**already the egg default**, so usually no change).
  Alternatively set `PY_FILE` = `relay.py`.
- `USER_UPLOAD` = `1` if uploading files yourself, so the original egg's
  installer does not try to clone a Git repo.
- `PY_PACKAGES` empty; no `requirements.txt` supplied.
- `SERVER_PORT` is assigned by Pelican for the server. The launcher binds
  the native relay to **0.0.0.0:SERVER_PORT** (not the public SERVER_IP).
  The allocated port must be **TCP** and reachable by players.

Simply start the server using the egg's normal Start button. Its stock startup
command runs `/usr/local/bin/python /home/container/app.py`; the launcher
replaces itself using `os.execv`, so the panel's stop/restart signals,
console output and exit status belong directly to the native relay.

## Optional configuration without changing the egg

Copy `relay.example.json` to `relay.json` beside `app.py` to override
`listen`, `max_rooms`, `max_connections` or set `player_tokens`. Each
token is a private 64-character hexadecimal value used by the FVD clients.
`SERVER_PORT` always takes priority over a `port` field in `relay.json`.
Do not commit a real token to a public repository.

The relay defaults to **public access** without tokens, 64 rooms and 128
connections. If you want private alpha access, upload `relay.json`
containing one or more authorized tokens and configure clients accordingly.

## Building the upload archive

The GitHub Actions workflow **FVD Pelican Python Generic relay bundle** builds
self-contained Linux x86_64 and arm64 native binaries in Alpine/musl *on CI*,
tests them, exercises the exact stock Python egg runtime, and publishes
`fvd-pelican-relay.zip` as a workflow artifact. Download that ZIP, extract
it through the Pelican Files page (or upload its extracted contents by SFTP).

Files are built outside the hosting container. `relay.py` never invokes gcc,
CMake, pip, a shell, or a package manager on the Pelican host.

If startup reports an unsupported architecture or missing binary, check the
panel's Docker image architecture and the uploaded `bin/` directory. The
included binaries are for Linux x86_64 and arm64 only. If a host refuses
execution from `/home/container` due to `noexec` mounts or security policy,
no user-file-only native launcher can bypass that restriction; the hosting
provider would need to permit executing uploaded files.

FVD still speaks the identical native relay protocol. The Python component
only selects the uploaded native binary and passes Pelican's allocated port.
