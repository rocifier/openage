# Copyright 2026 the openage authors. See copying.md for legal info.

"""Process integration coverage for FVD networking."""

from __future__ import annotations

import os
import subprocess
import tempfile
import time
from pathlib import Path

from openage import config
from openage.testing.testing import TestError

_PROCESS_TIMEOUT = 60
_READY_TIMEOUT = 30
_LOG_TAIL_SIZE = 4000


def _stop_process(process: subprocess.Popen | None) -> None:
    """Stop a child process left alive by a failed integration test."""
    if process is None or process.poll() is not None:
        return

    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def _log_tail(path: Path) -> str:
    """Read the useful tail of a child-process log."""
    try:
        return path.read_text(encoding="utf-8", errors="replace")[-_LOG_TAIL_SIZE:]
    except OSError as exc:
        return f"could not read {path}: {exc}"


def _wait_for_port(ready_path: Path, process: subprocess.Popen, role: str) -> int:
    """Wait until a child process has bound its ephemeral loopback port."""
    deadline = time.monotonic() + _READY_TIMEOUT
    while time.monotonic() < deadline:
        if ready_path.exists():
            try:
                port = int(ready_path.read_text(encoding="ascii").strip())
            except (OSError, UnicodeError, ValueError):
                pass
            else:
                if 0 < port <= 65535:
                    return port

        return_code = process.poll()
        if return_code is not None:
            raise TestError(f"network {role} exited before announcing its port: {return_code}")
        time.sleep(0.01)

    raise TestError(f"network {role} did not announce its port")


def _worker_command(worker: str) -> list[str]:
    """Run the C++ process worker directly, without Python/Cython dispatch."""
    build_dir = Path(config.BUILD_BIN_DIR)
    executable = "openage_native_cpp_tests.exe" if os.name == "nt" else "openage_native_cpp_tests"
    candidates = [
        build_dir / "packaging" / executable,
        build_dir / "packaging" / "RelWithDebInfo" / executable,
        build_dir / "packaging" / "Release" / executable,
        build_dir / "packaging" / "Debug" / executable,
    ]
    runner = next((path for path in candidates if path.is_file()), None)
    if runner is None:
        raise TestError(
            "native network test runner does not exist; first run build-fvd test"
        )
    return [str(runner), "--exact", worker]


def test() -> None:
    """Synchronize FVD world state through host and guest processes over TCP."""
    command = _worker_command("network::tests::fvd_process_worker")
    with tempfile.TemporaryDirectory(prefix="openage-fvd-network-") as temp_dir:
        temp_path = Path(temp_dir)
        ready_path = temp_path / "host.ready"
        host_log_path = temp_path / "host.log"
        guest_log_path = temp_path / "guest.log"
        host = None
        guest = None
        failure = None
        host_code = None
        guest_code = None

        with (
            host_log_path.open("w", encoding="utf-8") as host_log,
            guest_log_path.open("w", encoding="utf-8") as guest_log,
        ):
            try:
                host_environment = os.environ.copy()
                host_environment["OPENAGE_FVD_NETWORK_TEST_ROLE"] = "host"
                host_environment["OPENAGE_FVD_NETWORK_TEST_READY_FILE"] = str(ready_path)
                host = subprocess.Popen(
                    command,
                    cwd=config.BUILD_BIN_DIR,
                    env=host_environment,
                    stdout=host_log,
                    stderr=subprocess.STDOUT,
                )
                port = _wait_for_port(ready_path, host, "host")

                guest_environment = os.environ.copy()
                guest_environment["OPENAGE_FVD_NETWORK_TEST_ROLE"] = "guest"
                guest_environment["OPENAGE_FVD_NETWORK_TEST_PORT"] = str(port)
                guest = subprocess.Popen(
                    command,
                    cwd=config.BUILD_BIN_DIR,
                    env=guest_environment,
                    stdout=guest_log,
                    stderr=subprocess.STDOUT,
                )
                guest_code = guest.wait(timeout=_PROCESS_TIMEOUT)
                host_code = host.wait(timeout=_PROCESS_TIMEOUT)
            except (OSError, subprocess.SubprocessError, TestError) as exc:
                failure = str(exc)
            finally:
                _stop_process(guest)
                _stop_process(host)

        if failure is not None or host_code != 0 or guest_code != 0:
            raise TestError(
                f"two-process network test failed: {failure or 'child process failure'}\n"
                f"host exit code: {host_code}\n"
                f"guest exit code: {guest_code}\n"
                f"host log:\n{_log_tail(host_log_path)}\n"
                f"guest log:\n{_log_tail(guest_log_path)}"
            )


def test_relay() -> None:
    """Route an FVD command through a separate relay process in both directions."""
    command = _worker_command("network::tests::relay_process_worker")
    with tempfile.TemporaryDirectory(prefix="openage-fvd-relay-") as temp_dir:
        temp_path = Path(temp_dir)
        ready_path = temp_path / "relay.ready"
        relay_log_path = temp_path / "relay.log"
        host_log_path = temp_path / "host.log"
        guest_log_path = temp_path / "guest.log"
        relay = None
        host = None
        guest = None
        failure = None
        relay_code = None
        host_code = None
        guest_code = None

        with (
            relay_log_path.open("w", encoding="utf-8") as relay_log,
            host_log_path.open("w", encoding="utf-8") as host_log,
            guest_log_path.open("w", encoding="utf-8") as guest_log,
        ):
            try:
                relay_environment = os.environ.copy()
                relay_environment["OPENAGE_FVD_RELAY_TEST_ROLE"] = "relay"
                relay_environment["OPENAGE_FVD_RELAY_TEST_READY_FILE"] = str(ready_path)
                relay = subprocess.Popen(
                    command,
                    cwd=config.BUILD_BIN_DIR,
                    env=relay_environment,
                    stdout=relay_log,
                    stderr=subprocess.STDOUT,
                )
                port = _wait_for_port(ready_path, relay, "relay")

                host_environment = os.environ.copy()
                host_environment["OPENAGE_FVD_RELAY_TEST_ROLE"] = "host"
                host_environment["OPENAGE_FVD_RELAY_TEST_PORT"] = str(port)
                host = subprocess.Popen(
                    command,
                    cwd=config.BUILD_BIN_DIR,
                    env=host_environment,
                    stdout=host_log,
                    stderr=subprocess.STDOUT,
                )

                guest_environment = os.environ.copy()
                guest_environment["OPENAGE_FVD_RELAY_TEST_ROLE"] = "guest"
                guest_environment["OPENAGE_FVD_RELAY_TEST_PORT"] = str(port)
                guest = subprocess.Popen(
                    command,
                    cwd=config.BUILD_BIN_DIR,
                    env=guest_environment,
                    stdout=guest_log,
                    stderr=subprocess.STDOUT,
                )
                guest_code = guest.wait(timeout=_PROCESS_TIMEOUT)
                host_code = host.wait(timeout=_PROCESS_TIMEOUT)
                relay_code = relay.wait(timeout=_PROCESS_TIMEOUT)
            except (OSError, subprocess.SubprocessError, TestError) as exc:
                failure = str(exc)
            finally:
                _stop_process(guest)
                _stop_process(host)
                _stop_process(relay)

        if failure is not None or relay_code != 0 or host_code != 0 or guest_code != 0:
            raise TestError(
                f"three-process relay test failed: {failure or 'child process failure'}\n"
                f"relay exit code: {relay_code}\n"
                f"host exit code: {host_code}\n"
                f"guest exit code: {guest_code}\n"
                f"relay log:\n{_log_tail(relay_log_path)}\n"
                f"host log:\n{_log_tail(host_log_path)}\n"
                f"guest log:\n{_log_tail(guest_log_path)}"
            )
