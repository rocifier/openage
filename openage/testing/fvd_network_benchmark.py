# Copyright 2026 the openage authors. See copying.md for legal info.
"""Opt-in FVD five-player relay benchmark with per-guest TCP impairment.

This is an application-level TCP byte-stream shaper, not an OS packet emulator:
it delays and rate-limits the bytes before handing them to a real relay.
See doc/code/network/FVD_SIMULATED_NETWORK_TESTING.md.
"""

from __future__ import annotations

import argparse
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


class TcpShaper:
    """Full-duplex TCP proxy. Each direction preserves byte order and reliability."""

    def __init__(
        self, listen_port: int, target_port: int, *,
        rtt_ms: float, jitter_ms: float, kbps: float, seed: int,
        stall_after_s: float = -1, stall_s: float = 0,
    ):
        self.target_port = target_port
        self.one_way_s = rtt_ms / 2000.0
        self.jitter_s = jitter_ms / 2000.0
        self.bytes_per_s = kbps * 1000.0 / 8.0 if kbps else 0
        self.seed = seed
        self.stall_after_s = stall_after_s
        self.stall_s = stall_s
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
        self._lock = threading.Lock()
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
            except OSError:
                incoming.close()
                continue
            with self._lock:
                connection_id = len(self.sockets) // 2
                self.sockets.extend((incoming, upstream))
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
        pending: queue.Queue[tuple[float, bytes]] = queue.Queue(maxsize=256)
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
                        try:
                            pending.put((due, data), timeout=0.2)
                            break
                        except queue.Full:
                            continue
            except OSError:
                pass
            finally:
                reader_done.set()

        reader = threading.Thread(target=read_loop, daemon=True)
        reader.start()
        next_send = 0.0
        try:
            while not self.stopping.is_set():
                try:
                    scheduled, data = pending.get(timeout=0.2)
                except queue.Empty:
                    if reader_done.is_set():
                        break
                    continue
                # Applies only to the selected client's downstream link.
                if direction == "down" and self.stall_after_s >= 0 and self.stall_s:
                    stall_at = start + self.stall_after_s
                    if time.monotonic() >= stall_at and time.monotonic() < stall_at + self.stall_s:
                        scheduled = max(scheduled, stall_at + self.stall_s)
                deliver_at = max(scheduled, next_send)
                while not self.stopping.is_set() and time.monotonic() < deliver_at:
                    time.sleep(min(0.02, deliver_at - time.monotonic()))
                if self.stopping.is_set():
                    break
                dest.sendall(data)
                if self.bytes_per_s:
                    next_send = max(deliver_at, time.monotonic()) + len(data) / self.bytes_per_s
                with self._lock:
                    if direction == "up":
                        self.bytes_up += len(data)
                    else:
                        self.bytes_down += len(data)
        except OSError:
            pass
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


RESULT = re.compile(r"FVD_BENCH_RESULT (.+)")
CLIENT = re.compile(r"FVD_BENCH_CLIENT (.+)")


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
    parser.add_argument("--slow-client", type=int, default=0, choices=range(0, 5),
                        help="1-4 selects one of four guest endpoints for a stall")
    parser.add_argument("--stall-after-s", type=float, default=6.5)
    parser.add_argument("--stall-s", type=float, default=0.0)
    parser.add_argument("--slow-kbps", type=float, default=0.0)
    parser.add_argument("--rounds", type=int, default=12)
    parser.add_argument("--arrows", type=int, default=0,
                        help="Synthetic projectile records per recipient update (0-2048)")
    parser.add_argument("--seed", type=int, default=4242)
    parser.add_argument("--timeout-s", type=float, default=180)
    parser.add_argument("--json", type=Path, help="Save measured timings and traffic")
    args = parser.parse_args()

    if (args.rtt_ms < 0 or args.jitter_ms < 0 or args.kbps < 0
            or args.slow_kbps < 0 or args.stall_s < 0 or args.stall_after_s < 0
            or args.rounds < 1 or args.rounds > 200
            or args.arrows < 0 or args.arrows > 2048):
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
            ))
        env = os.environ.copy()
        env.update({
            "FVD_BENCH_RELAY_PORT": str(relay_port),
            "FVD_BENCH_PROXY_BASE": str(base),
            "FVD_BENCH_ROUNDS": str(args.rounds),
            "FVD_BENCH_ARROWS": str(args.arrows),
        })
        print("Starting real 5-client FVD relay harness; "
              "proxy delay is per guest TCP connection", flush=True)
        cmd = [args.runner, "test", "network::tests::lobby_latency"]
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
    match = RESULT.search(output)
    def parse_numbers(text: str) -> dict:
        values = {}
        for token in text.split():
            if "=" in token:
                key, value = token.split("=", 1)
                values[key] = float(value) if "." in value else int(value)
        return values

    measurements = parse_numbers(match.group(1)) if match else {}
    clients = [parse_numbers(m.group(1)) for m in CLIENT.finditer(output)]
    report = {
        "pass": return_code == 0 and bool(match),
        "exit_code": return_code,
        "elapsed_s": round(time.monotonic() - began, 3),
        "profile": {
            "rtt_ms": args.rtt_ms, "jitter_ms": args.jitter_ms,
            "kbps": args.kbps, "slow_client": args.slow_client,
            "stall_s": args.stall_s, "stall_after_s": args.stall_after_s,
            "slow_kbps": args.slow_kbps, "arrows": args.arrows,
            "rounds": args.rounds, "seed": args.seed,
        },
        "measurements": measurements,
        "clients": clients,
        "links": [
            {"client": i + 1, "bytes_up": p.bytes_up, "bytes_down": p.bytes_down}
            for i, p in enumerate(proxies)
        ],
        "warning": "Scripted relay workers, not graphical play or authoritative combat. "
                   "Synthetic byte delays are not an OS-level packet-loss emulator.",
    }
    print("FVD_NETWORK_REPORT " + json.dumps(report, sort_keys=True), flush=True)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if report["pass"] else (return_code or 1)


if __name__ == "__main__":
    sys.exit(main())
