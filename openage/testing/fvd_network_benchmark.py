# Copyright 2026 the openage authors. See copying.md for legal info.
"""Opt-in FVD five-player relay benchmark with per-guest TCP impairment.

This is an application-level TCP byte-stream shaper, not an OS packet emulator:
it delays and rate-limits the bytes before handing them to a real relay.
See doc/code/network/FVD_SIMULATED_NETWORK_TESTING.md.
"""

from __future__ import annotations

import argparse
from collections import deque
import json
import os
import random
import queue
import select
import re
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def free_port_block(length: int, seed: int, excluded: set[int]) -> int:
    rng = random.Random(seed)
    for _ in range(1000):
        base = rng.randint(20000, 59000)
        sockets = []
        try:
            for port in range(base, base + length):
                if port in excluded:
                    raise OSError("reserved port")
                sock = socket.socket()
                sockets.append(sock)
                sock.bind(("127.0.0.1", port))
            return base
        except OSError:
            pass
        finally:
            for sock in sockets:
                sock.close()
    raise RuntimeError("Cannot reserve consecutive localhost ports")


def bounded_sleep_seconds(deadline: float, now: float) -> float:
    """Never pass a negative delay to sleep after a deadline-check race."""
    return max(0.0, min(0.02, deadline - now))


class TokenBucketPacer:
    """A clock-replenished byte budget; late wakeups never incur extra debt.

    Each forward direction owns its own pacer. Bursts are bounded by
    burst_ms of capacity, with enough allowance for one 4096-byte recv.
    """

    def __init__(self, kbps: float, burst_ms: float = 50.0,
                 chunk_bytes: int = 4096, now: float | None = None):
        self.rate = kbps * 125.0  # decimal kilobits/second -> bytes/second
        self.capacity = max(float(chunk_bytes),
                            self.rate * burst_ms / 1000.0) if self.rate else 0.0
        self.tokens = self.capacity
        self.last = time.monotonic() if now is None else now

    def wait_seconds(self, size: int, now: float) -> float:
        """Consume available tokens or return required wait (no consumption)."""
        if not self.rate:
            return 0.0
        if size > self.capacity:
            raise ValueError("Pacer chunk exceeds burst capacity")
        self.tokens = min(self.capacity,
                          self.tokens + max(0.0, now - self.last) * self.rate)
        self.last = now
        if self.tokens >= size:
            self.tokens -= size
            return 0.0
        return (size - self.tokens) / self.rate


class TransferStats:
    """Forwarded bytes and backpressure, aggregated per proxy direction."""

    def __init__(self):
        self.bytes = 0
        self.first_at: float | None = None
        self.last_at: float | None = None
        self.pacing_wait_s = 0.0
        self.reader_blocked_s = 0.0
        self.peak_queue_chunks = 0
        self.full_queue_events = 0
        self.peak_1s_kbps = 0.0
        self._trailing: deque[tuple[float, int]] = deque()
        self._trailing_bytes = 0

    def delivered(self, size: int, at: float) -> None:
        if self.first_at is None:
            self.first_at = at
        self.last_at = at
        self.bytes += size
        self._trailing.append((at, size))
        self._trailing_bytes += size
        while self._trailing and self._trailing[0][0] < at - 1.0:
            _, expired = self._trailing.popleft()
            self._trailing_bytes -= expired
        # A trailing one-second window, not an instantaneous rate.
        self.peak_1s_kbps = max(self.peak_1s_kbps,
                                self._trailing_bytes * 8.0 / 1000.0)

    def report(self, window_seconds: float) -> dict:
        active_seconds = (self.last_at - self.first_at
                          if self.first_at is not None and self.last_at is not None
                          else 0.0)
        return {
            "bytes": self.bytes,
            "peak_1s_kbps": round(self.peak_1s_kbps, 2),
            "session_average_kbps": round(
                self.bytes * 8.0 / (1000.0 * max(window_seconds, 0.001)), 2),
            "first_to_last_kbps": round(
                self.bytes * 8.0 / (1000.0 * max(active_seconds, 0.001)), 2),
            "pacing_wait_ms": round(self.pacing_wait_s * 1000.0, 2),
            "reader_blocked_ms": round(self.reader_blocked_s * 1000.0, 2),
            "peak_queue_chunks": self.peak_queue_chunks,
            "full_queue_events": self.full_queue_events,
        }


def sleep_until(deadline: float, stopping: threading.Event) -> None:
    """Deadline-based sleeping, immune to negative-delay clock races."""
    while not stopping.is_set():
        delay = bounded_sleep_seconds(deadline, time.monotonic())
        if delay <= 0.0:
            return
        time.sleep(delay)


class TcpShaper:
    """Full-duplex TCP proxy. Each direction preserves byte order and reliability."""

    def __init__(
        self, listen_port: int, target_port: int, *,
        rtt_ms: float, jitter_ms: float, kbps: float, seed: int,
        stall_after_s: float = -1, stall_s: float = 0,
        proxy_buffer_kb: int = 64, socket_buffer_kb: int = 32,
        burst_ms: float = 50.0,
    ):
        self.target_port = target_port
        self.one_way_s = rtt_ms / 2000.0
        self.jitter_s = jitter_ms / 2000.0
        self.bytes_per_s = kbps * 1000.0 / 8.0 if kbps else 0
        self.seed = seed
        self.stall_after_s = stall_after_s
        self.stall_s = stall_s
        self.proxy_queue_chunks = max(1, proxy_buffer_kb * 1024 // 4096)
        self.socket_buffer_bytes = socket_buffer_kb * 1024
        self.burst_ms = burst_ms
        self.observed_socket_recvbuf_bytes: list[int] = []
        self.stopping = threading.Event()
        self.listener = socket.socket()
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", listen_port))
        self.listener.listen(16)
        self.listener.settimeout(0.2)
        self.sockets: list[socket.socket] = []
        self.threads: list[threading.Thread] = []
        self.bytes_up = 0
        self.bytes_down = 0
        self.stats = {"up": TransferStats(), "down": TransferStats()}
        self._lock = threading.Lock()
        self.worker_errors: list[str] = []
        self.acceptor = threading.Thread(target=self._accept, daemon=True)
        self.acceptor.start()

    def _accept(self) -> None:
        while not self.stopping.is_set():
            try:
                incoming, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            try:
                upstream = socket.create_connection(
                    ("127.0.0.1", self.target_port), timeout=10)
                upstream.settimeout(None)  # Blocking TCP sendall; reader uses select().
                if self.socket_buffer_bytes:
                    # Bound both proxy kernel receive buffers to propagate
                    # backpressure instead of buffering megabytes invisibly.
                    # Actual sizes are OS-dependent and reported.
                    for endpoint in (incoming, upstream):
                        endpoint.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF,
                                            self.socket_buffer_bytes)
                        endpoint.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF,
                                            self.socket_buffer_bytes)
            except OSError as exc:
                with self._lock:
                    self.worker_errors.append(
                        f"proxy connection setup failed: {type(exc).__name__}: {exc}")
                incoming.close()
                if "upstream" in locals():
                    upstream.close()
                continue
            with self._lock:
                connection_id = len(self.sockets) // 2
                self.sockets.extend((incoming, upstream))
                self.observed_socket_recvbuf_bytes.append(
                    upstream.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF))
            start = time.monotonic()
            for direction, source, dest in (
                ("up", incoming, upstream),
                ("down", upstream, incoming),
            ):
                worker = threading.Thread(
                    target=self._forward,
                    args=(direction, source, dest, start, connection_id),
                    daemon=True,
                )
                self.threads.append(worker)
                worker.start()

    def _forward(
        self, direction: str, source: socket.socket,
        dest: socket.socket, start: float, connection_id: int,
    ) -> None:
        # Separate reader from sender: delaying each recv(4096) in sequence
        # would multiply 110ms of one-way latency by every 4KB of a big
        # WORLD_DELTA, badly overstating lag under projectile-heavy traffic.
        # Read promptly, schedule each chunk once and pace the output queue.
        rng = random.Random(self.seed + connection_id * 101
                            + (1 if direction == "down" else 0))
        pending: queue.Queue[tuple[float, bytes]] = queue.Queue(
            maxsize=self.proxy_queue_chunks)
        pacer = TokenBucketPacer(self.bytes_per_s * 8.0 / 1000.0,
                                 self.burst_ms)
        reader_done = threading.Event()
        local_stop = threading.Event()

        def read_loop() -> None:
            try:
                while not self.stopping.is_set() and not local_stop.is_set():
                    readable, _, _ = select.select([source], [], [], 0.2)
                    if not readable:
                        continue
                    data = source.recv(4096)
                    if not data:
                        break
                    delay = max(0.0, self.one_way_s
                                + rng.uniform(-self.jitter_s, self.jitter_s))
                    due = time.monotonic() + delay
                    while not self.stopping.is_set() and not local_stop.is_set():
                        full_before_put = pending.full()
                        entered = time.monotonic()
                        try:
                            pending.put((due, data), timeout=0.05)
                            with self._lock:
                                stats = self.stats[direction]
                                stats.peak_queue_chunks = max(
                                    stats.peak_queue_chunks, pending.qsize())
                                if full_before_put:
                                    stats.reader_blocked_s += time.monotonic() - entered
                            break
                        except queue.Full:
                            with self._lock:
                                stats = self.stats[direction]
                                stats.full_queue_events += 1
                                stats.reader_blocked_s += time.monotonic() - entered
            except (OSError, ValueError):
                # Closing the proxy may invalidate a socket in select().
                pass
            finally:
                reader_done.set()

        reader = threading.Thread(target=read_loop, daemon=True)
        reader.start()
        stall_at = start + self.stall_after_s
        stall_until = stall_at + self.stall_s
        try:
            while not self.stopping.is_set():
                try:
                    scheduled, data = pending.get(timeout=0.2)
                except queue.Empty:
                    if reader_done.is_set():
                        break
                    continue
                # Wait for per-chunk one-way latency once, not once per
                # 4096-byte recv after the previous chunk finished.
                due = scheduled
                if (direction == "down" and self.stall_s
                        and stall_at <= due < stall_until):
                    due = stall_until
                sleep_until(due, self.stopping)
                if self.stopping.is_set():
                    break
                # Clock-replenished tokens allow short catch-up bursts after
                # OS scheduler delays instead of accumulating delay on each
                # forwarded chunk. The two directions are independent.
                while not self.stopping.is_set():
                    wait_s = pacer.wait_seconds(len(data), time.monotonic())
                    if wait_s == 0:
                        break
                    entered = time.monotonic()
                    sleep_until(entered + wait_s, self.stopping)
                    with self._lock:
                        self.stats[direction].pacing_wait_s += (
                            time.monotonic() - entered)
                if self.stopping.is_set():
                    break
                # A stall can begin during bandwidth pacing. Check again
                # immediately before forwarding downstream bytes.
                if direction == "down" and self.stall_s:
                    now = time.monotonic()
                    if stall_at <= now < stall_until:
                        sleep_until(stall_until, self.stopping)
                if self.stopping.is_set():
                    break
                dest.sendall(data)
                finished = time.monotonic()
                with self._lock:
                    self.stats[direction].delivered(len(data), finished)
                    if direction == "up":
                        self.bytes_up += len(data)
                    else:
                        self.bytes_down += len(data)
        except OSError:
            # Peers can close during teardown or an actual disconnect test.
            pass
        except Exception as exc:
            # A shaper programming error must invalidate the test; otherwise
            # the game might keep running and report a misleading PASS.
            with self._lock:
                self.worker_errors.append(
                    f"proxy forwarding {direction} connection {connection_id}: "
                    f"{type(exc).__name__}: {exc}"
                )
        finally:
            local_stop.set()
            for sock in (source, dest):
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                try:
                    sock.close()
                except OSError:
                    pass
            reader.join(timeout=2)

    def report_stats(self, window_seconds: float) -> dict:
        with self._lock:
            return {
                "configured_kbps": round(self.bytes_per_s * 8.0 / 1000.0, 2),
                "observed_recvbuf_bytes": list(self.observed_socket_recvbuf_bytes),
                "up": self.stats["up"].report(window_seconds),
                "down": self.stats["down"].report(window_seconds),
            }

    def close(self) -> None:
        self.stopping.set()
        self.listener.close()
        with self._lock:
            sockets = list(self.sockets)
        for sock in sockets:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            sock.close()
        self.acceptor.join(timeout=2)
        for thread in list(self.threads):
            thread.join(timeout=2)


def resolve_runner(name: str) -> str:
    """Find a native launcher, giving an actionable Windows error if absent."""
    requested = Path(name).expanduser()
    candidates = [requested]
    if os.name == "nt" and requested.suffix.lower() != ".exe":
        # subprocess/CreateProcess does not reliably resolve the extension
        # for the Windows bin/run path documented by Linux instructions.
        candidates.insert(0, Path(str(requested) + ".exe"))
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate.resolve())
    # Allow bare commands discoverable on PATH, but not arbitrary missing
    # relative paths that would otherwise produce an opaque WinError 2.
    import shutil
    which = shutil.which(name)
    if which:
        return which
    tried = ", ".join(str(p) for p in candidates)
    raise FileNotFoundError(
        f"FVD test runner not found: {tried}. "
        "Build openage first, then pass the actual run.exe location using "
        "'--runner .\\bin\\run.exe' (PowerShell: Get-ChildItem -Recurse "
        "-Filter run.exe -File)."
    )


RESULT = re.compile(r"FVD_BENCH_RESULT (.+)")
CLIENT = re.compile(r"FVD_BENCH_CLIENT (.+)")


def parse_report_fields(line: str) -> dict[str, int | float | str]:
    """Parse C++ benchmark key=value fields, retaining textual metadata.

    Earlier versions tried int("independent") for mode=independent and
    crashed after a successful network run, discarding its useful report.
    """
    fields: dict[str, int | float | str] = {}
    for token in line.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        try:
            fields[key] = int(value)
            continue
        except ValueError:
            pass
        try:
            fields[key] = float(value)
        except ValueError:
            fields[key] = value
    return fields


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", default="bin/run",
                        help="Configured openage launcher, e.g. bin/run or bin/run.exe")
    parser.add_argument("--rtt-ms", type=float, default=180,
                        help="Added round-trip delay in milliseconds")
    parser.add_argument("--jitter-ms", type=float, default=30,
                        help="Maximum added variation of simulated RTT (ms)")
    parser.add_argument("--kbps", type=float, default=2000,
                        help="Per-direction bandwidth for each guest; zero means unlimited")
    parser.add_argument("--proxy-buffer-kb", type=int, default=64,
                        help="Maximum queued bytes per proxy direction (default 64 KiB)")
    parser.add_argument("--socket-buffer-kb", type=int, default=32,
                        help="Requested TCP socket send/receive buffer KiB; 0 leaves OS defaults")
    parser.add_argument("--burst-ms", type=float, default=50.0,
                        help="Token bucket burst capacity in milliseconds of link data")
    parser.add_argument("--slow-client", type=int, default=0, choices=range(0, 5),
                        help="1-4 selects one of four guest endpoints for a stall")
    parser.add_argument("--stall-after-s", type=float, default=6.5)
    parser.add_argument("--stall-s", type=float, default=0.0)
    parser.add_argument("--slow-kbps", type=float, default=0.0)
    parser.add_argument("--rounds", type=int, default=20,
                        help="Number of host world-update scheduling intervals")
    parser.add_argument("--update-ms", type=int, default=500,
                        help="Independent Hunter world-update interval in milliseconds")
    parser.add_argument("--command-ms", type=int, default=500,
                        help="Independent per-Builder command interval in milliseconds")
    parser.add_argument("--arrows", type=int, default=0,
                        help="Synthetic projectile records per recipient update (0-2048)")
    parser.add_argument("--seed", type=int, default=4242)
    parser.add_argument("--timeout-s", type=float, default=180)
    parser.add_argument("--json", type=Path, help="Save measured timings and traffic")
    args = parser.parse_args()

    if (args.rtt_ms < 0 or args.jitter_ms < 0 or args.kbps < 0
            or args.slow_kbps < 0 or args.stall_s < 0 or args.stall_after_s < 0
            or args.rounds < 1 or args.rounds > 200
            or args.update_ms < 1 or args.update_ms > 2000
            or args.command_ms < 1 or args.command_ms > 2000
            or args.arrows < 0 or args.arrows > 2048
            or not 4 <= args.proxy_buffer_kb <= 4096
            or not 0 <= args.socket_buffer_kb <= 4096
            or not 0 <= args.burst_ms <= 1000):
        parser.error("Negative network parameters or unsupported rounds/arrows")

    relay_port = free_port()
    base = free_port_block(4, args.seed, {relay_port})
    proxies = []
    began = time.monotonic()
    output = ""
    return_code = 1
    try:
        for i in range(4):
            slow = args.slow_client == i + 1
            proxies.append(TcpShaper(
                base + i, relay_port,
                rtt_ms=args.rtt_ms, jitter_ms=args.jitter_ms,
                kbps=args.slow_kbps if slow and args.slow_kbps else args.kbps,
                seed=args.seed + i * 10000,
                stall_after_s=args.stall_after_s if slow else -1,
                stall_s=args.stall_s if slow else 0,
                proxy_buffer_kb=args.proxy_buffer_kb,
                socket_buffer_kb=args.socket_buffer_kb,
                burst_ms=args.burst_ms,
            ))
        env = os.environ.copy()
        env.update({
            "FVD_BENCH_RELAY_PORT": str(relay_port),
            "FVD_BENCH_PROXY_BASE": str(base),
            "FVD_BENCH_ROUNDS": str(args.rounds),
            "FVD_BENCH_UPDATE_MS": str(args.update_ms),
            "FVD_BENCH_COMMAND_MS": str(args.command_ms),
            "FVD_BENCH_ARROWS": str(args.arrows),
        })
        print("Starting real 5-client FVD relay harness; "
              "proxy delay is per guest TCP connection", flush=True)
        cmd = [resolve_runner(args.runner), "test", "network::tests::lobby_latency"]
        try:
            completed = subprocess.run(
                cmd, env=env, text=True, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, timeout=args.timeout_s, check=False,
            )
            output = completed.stdout
            return_code = completed.returncode
        except subprocess.TimeoutExpired as exc:
            output = (exc.stdout or b"").decode("utf-8", "replace") \
                if isinstance(exc.stdout, bytes) else (exc.stdout or "")
            output += "\nBenchmark timed out"
            return_code = 124
        print(output, end="" if output.endswith("\n") else "\n")
    finally:
        for proxy in proxies:
            proxy.close()
    proxy_errors = [error for proxy in proxies for error in proxy.worker_errors]
    duration = time.monotonic() - began
    match = RESULT.search(output)
    measurements = parse_report_fields(match.group(1)) if match else {}
    clients = [parse_report_fields(m.group(1)) for m in CLIENT.finditer(output)]
    # Reject old compiled runners which produce plausible but misleading
    # results with random Hunter authority behind a shaped guest proxy.
    topology_verified = "FVD_BENCH_TOPOLOGY hunter=direct builders=proxied remote_builders=4" in output
    independently_scheduled = "FVD_BENCH_SCHEDULE mode=independent" in output
    report = {
        "pass": return_code == 0 and bool(match) and topology_verified
                and independently_scheduled and not proxy_errors,
        "proxy_errors": proxy_errors,
        "independently_scheduled": independently_scheduled,
        "topology_verified": topology_verified,
        "exit_code": return_code,
        "elapsed_s": round(duration, 3),
        "profile": {
            "rtt_ms": args.rtt_ms, "jitter_ms": args.jitter_ms,
            "kbps": args.kbps, "slow_client": args.slow_client,
            "stall_s": args.stall_s, "stall_after_s": args.stall_after_s,
            "slow_kbps": args.slow_kbps, "arrows": args.arrows,
            "rounds": args.rounds, "seed": args.seed,
            "update_ms": args.update_ms, "command_ms": args.command_ms,
            "proxy_buffer_kb": args.proxy_buffer_kb,
            "socket_buffer_kb": args.socket_buffer_kb,
            "burst_ms": args.burst_ms,
        },
        "measurements": measurements,
        "clients": [
            {**client,
             "proxy": int(client.get("player", 0)) - 1,
             "slow_client": int(client.get("player", 0)) - 1 == args.slow_client
                            and args.slow_client != 0}
            for client in clients
        ],
        "links": [
            {"client": i + 1, "bytes_up": p.bytes_up, "bytes_down": p.bytes_down,
             **p.report_stats(duration)}
            for i, p in enumerate(proxies)
        ],
        "warning": "Continuous scripted relay workers, not graphical play or authoritative combat. "
                   "World updates may be superseded before delivery; command_pending, "
                   "latest_applied_revision, world_skipped and state_age measure backlogs. "
                   "The proxy peak_1s_kbps measures forwarded application bytes, not "
                   "an end-to-end network goodput guarantee. Proxy buffers are "
                   "bounded but cannot fully control OS/game socket buffers. "
                   "Synthetic byte delays are not an OS-level packet-loss emulator.",
    }
    if proxy_errors:
        print("FVD benchmark proxy failure: " + "; ".join(proxy_errors),
              file=sys.stderr, flush=True)
    if match and not independently_scheduled:
        print("FVD benchmark runner is stale: continuous scheduling marker missing. "
              "Recompile PR #7's C++ test.", file=sys.stderr, flush=True)
    if match and not topology_verified:
        print("FVD benchmark runner is stale or topology was not verified. "
              "Recompile PR #7's C++ test before trusting performance results.",
              file=sys.stderr, flush=True)
    print("FVD_NETWORK_REPORT " + json.dumps(report, sort_keys=True), flush=True)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if report["pass"] else (return_code or 1)


if __name__ == "__main__":
    sys.exit(main())
