# Copyright 2026 the openage authors. See copying.md for legal info.
"""Regression tests for the stdlib-only FVD network shaper timing helpers."""

import socket
import threading
import time
import unittest

from .fvd_network_benchmark import (
    TokenBucketPacer, TransferStats, TcpShaper,
    bounded_sleep_seconds, free_port, parse_report_fields,
)


class FVDNetworkShaperTimingTests(unittest.TestCase):
    def test_future_deadline_is_bounded(self):
        self.assertAlmostEqual(bounded_sleep_seconds(100.01, 100.0), 0.01)
        self.assertAlmostEqual(bounded_sleep_seconds(101.0, 100.0), 0.02)

    def test_elapsed_deadline_never_produces_negative_sleep(self):
        self.assertEqual(bounded_sleep_seconds(100.0, 100.0), 0.0)
        self.assertEqual(bounded_sleep_seconds(99.999999, 100.0), 0.0)
        self.assertEqual(bounded_sleep_seconds(99.0, 100.0), 0.0)


class FVDNetworkReportParsingTests(unittest.TestCase):
    def test_continuous_mode_and_numeric_metrics(self):
        fields = parse_report_fields(
            "players=5 remote=4 mode=independent "
            "command_p95_ms=328.46 command_offered=80 "
            "world_p50_ms=0.00 world_samples=79"
        )
        self.assertEqual(fields["players"], 5)
        self.assertEqual(fields["remote"], 4)
        self.assertEqual(fields["mode"], "independent")
        self.assertEqual(fields["command_p95_ms"], 328.46)
        self.assertEqual(fields["command_offered"], 80)
        self.assertEqual(fields["world_p50_ms"], 0.0)
        self.assertEqual(fields["world_samples"], 79)

    def test_unknown_text_fields_do_not_crash_reporting(self):
        fields = parse_report_fields("mode=independent status=congested extra flag=ok")
        self.assertEqual(fields, {
            "mode": "independent", "status": "congested", "flag": "ok"
        })



class FVDTokenBucketTests(unittest.TestCase):
    def test_2_mbps_budget_with_late_clock_wakeups(self):
        pacer = TokenBucketPacer(2000, burst_ms=50, now=0.0)
        for _ in range(3):
            self.assertEqual(pacer.wait_seconds(4096, 0.0), 0.0)
        self.assertGreater(pacer.wait_seconds(4096, 0.0), 0.0)
        # A 100ms scheduling pause refills only the bounded burst allowance.
        for _ in range(3):
            self.assertEqual(pacer.wait_seconds(4096, 0.1), 0.0)
        self.assertGreater(pacer.wait_seconds(4096, 0.1), 0.0)

    def test_sustained_rate_is_close_to_requested_bandwidth(self):
        pacer = TokenBucketPacer(2000, burst_ms=50, now=0.0)
        now = 0.0
        total = 0
        # Deterministic virtual-clock throughput check, no OS scheduler.
        while total < 250_000:
            wait = pacer.wait_seconds(4096, now)
            if wait:
                now += max(wait, 0.000001)
                continue
            total += 4096
        self.assertGreater(now, 0.9)
        self.assertLess(now, 1.1)
        self.assertAlmostEqual(total * 8 / (now * 1000), 2000, delta=200)

    def test_unlimited_connection_does_not_rate_limit(self):
        pacer = TokenBucketPacer(0, now=0.0)
        self.assertEqual(pacer.wait_seconds(4096, 0.0), 0.0)
        self.assertEqual(pacer.wait_seconds(4096, 0.0), 0.0)

    def test_transfer_stats_distinguish_session_from_peak_rate(self):
        stats = TransferStats()
        stats.delivered(1000, 0.0)
        stats.delivered(2000, 0.5)
        stats.delivered(1000, 2.0)
        result = stats.report(4.0)
        self.assertEqual(result["bytes"], 4000)
        self.assertEqual(result["peak_1s_kbps"], 24.0)
        self.assertEqual(result["session_average_kbps"], 8.0)


class FVDTcpShaperLoopbackTests(unittest.TestCase):
    def test_bounded_byte_stream_forwards_all_bytes_at_measured_rate(self):
        # A real localhost proxy regression, but independent of the FVD
        # binary or any external services. Accept a small cross-platform
        # scheduling tolerance instead of enforcing OS-specific precision.
        source = socket.socket()
        source.bind(("127.0.0.1", 0))
        source.listen(1)
        source.settimeout(5.0)
        proxy = TcpShaper(
            free_port(), source.getsockname()[1],
            rtt_ms=0, jitter_ms=0, kbps=1000, seed=42,
            proxy_buffer_kb=8, socket_buffer_kb=8, burst_ms=25,
        )
        payload = b"fvd-test!" * 8192  # 73,728 bytes
        errors = []

        def send_payload():
            try:
                peer, _ = source.accept()
                with peer:
                    peer.settimeout(5.0)
                    peer.sendall(payload)
                    peer.shutdown(socket.SHUT_WR)
            except Exception as exc:
                errors.append(exc)

        server_thread = threading.Thread(target=send_payload, daemon=True)
        server_thread.start()
        try:
            started = time.monotonic()
            # The listener reports the assigned port, which remains stable.
            with socket.create_connection(
                ("127.0.0.1", proxy.listen_port), timeout=5.0
            ) as receiver:
                receiver.settimeout(5.0)
                received = bytearray()
                while len(received) < len(payload):
                    chunk = receiver.recv(16384)
                    if not chunk:
                        break
                    received.extend(chunk)
            duration = time.monotonic() - started
            self.assertEqual(bytes(received), payload)
            stats = proxy.report_stats(duration)["down"]
            self.assertEqual(stats["bytes"], len(payload))
            self.assertGreater(stats["pacing_wait_ms"], 0)
            self.assertGreater(duration, 0.35)
            self.assertLess(duration, 4.0)
            self.assertGreater(stats["peak_1s_kbps"], 450)
            self.assertLess(stats["peak_1s_kbps"], 1600)
        finally:
            proxy.close()
            source.close()
            server_thread.join(timeout=5)
        self.assertEqual(errors, [])



if __name__ == "__main__":
    unittest.main()
