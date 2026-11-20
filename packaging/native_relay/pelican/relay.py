# Copyright 2026 the openage authors. See copying.md for legal info.
"""Standard-library FVD relay launcher for Pelican's unmodified Python Generic egg.

The egg invokes /usr/local/bin/python /home/container/app.py. app.py calls
main() here, which replaces itself with the uploaded native relay executable.
No pip packages, shell, compilation, package extraction or root access needed.
"""
import json
import os
from pathlib import Path
import platform
import re
import stat
import sys


def _positive_integer(value, name, maximum):
    if isinstance(value, bool) or not str(value).isdigit():
        raise ValueError(f"{name} must be an integer between 1 and {maximum}")
    result = int(value)
    if not 1 <= result <= maximum:
        raise ValueError(f"{name} must be an integer between 1 and {maximum}")
    return result


def _architecture(machine):
    arch = machine.lower()
    if arch in ("x86_64", "amd64"):
        return "amd64"
    if arch in ("aarch64", "arm64"):
        return "arm64"
    raise ValueError(f"Unsupported Linux CPU architecture {machine!r}; "
                     "Pelican FVD bundle supports x86_64 and aarch64")


def make_command(root, environ=None, machine=None):
    """Return (binary_path, argument_list, port) without running a program."""
    if environ is None:
        environ = os.environ
    if machine is None:
        machine = platform.machine()
    if sys.platform != "linux":
        raise ValueError("Pelican relay bundle requires Linux; build the native "
                         "relay separately for development on other platforms")

    arch = _architecture(machine)
    root = Path(root).resolve()
    binary = root / "bin" / ("openage-relay-linux-" + arch)
    if not binary.is_file():
        raise FileNotFoundError(
            f"FVD native relay for {arch} not found at {binary}. "
            "Upload the complete FVD Pelican relay bundle, including bin/.")

    config_path = root / "relay.json"
    config = {}
    if config_path.exists():
        with config_path.open("r", encoding="utf-8") as source:
            config = json.load(source)
        if not isinstance(config, dict):
            raise ValueError("relay.json must contain a JSON object")
        supported = {"listen", "port", "max_rooms", "max_connections", "player_tokens"}
        unexpected = set(config) - supported
        if unexpected:
            raise ValueError("Unrecognized relay.json setting(s): "
                             + ", ".join(sorted(unexpected)))

    # Pelican allocates the external port. Its SERVER_PORT always takes
    # precedence over a user config/default; SERVER_IP is generally a public
    # address, *not* necessarily an address within the container.
    port = _positive_integer(
        environ.get("SERVER_PORT") or config.get("port")
        or environ.get("PORT") or 40000, "SERVER_PORT", 65535)
    listen = config.get("listen", "0.0.0.0")
    if not isinstance(listen, str) or not listen.strip():
        raise ValueError("relay.json listen must be a non-empty IP address")
    max_rooms = _positive_integer(config.get("max_rooms", 64), "max_rooms", 4096)
    max_connections = _positive_integer(
        config.get("max_connections", 128), "max_connections", 100000)

    tokens = config.get("player_tokens", [])
    if not isinstance(tokens, list):
        raise ValueError("relay.json player_tokens must be a list")
    if len(tokens) != len(set(map(str, tokens))):
        raise ValueError("relay.json player_tokens must be unique")
    for token in tokens:
        if not isinstance(token, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", token) or not int(token, 16):
            raise ValueError("Every relay.json player token must be 64 hex "
                             "characters and not all zeros")

    command = [
        str(binary), "--listen", listen, "--port", str(port),
        "--max-rooms", str(max_rooms), "--max-connections", str(max_connections),
    ]
    for token in tokens:
        command.extend(("--player-token", token))
    return binary, command, port


def main():
    root = Path(__file__).resolve().parent
    try:
        binary, command, port = make_command(root)

        # Many SFTP/panel uploads strip the executable bit. This modifies
        # only the user's uploaded file, never the host installation or egg.
        mode = binary.stat().st_mode
        if not mode & stat.S_IXUSR:
            binary.chmod(mode | stat.S_IXUSR)

        print(f"[FVD/Pelican] Starting native relay on TCP port {port} "
              f"({platform.machine()}); no Python runtime needed after launch",
              flush=True)
        # Replace Python instead of spawning an orphan child: stdout/stderr,
        # exit status, SIGINT/SIGTERM and the panel's stop button all go to
        # the original C++ relay, which already handles graceful shutdown.
        os.execv(str(binary), command)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"[FVD/Pelican] Startup failed: {error}", file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main())
