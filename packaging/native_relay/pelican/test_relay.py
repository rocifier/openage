# Copyright 2026 the openage authors. See copying.md for legal info.
"""Stdlib-only tests for stock Pelican Python Generic egg drop-in."""
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import unittest

import relay


class PelicanRelayTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / "bin").mkdir()
        self.machine = "x86_64"

    def create_binary(self, arch="amd64", content="#!/bin/sh\nexit 0\n"):
        binary = self.root / "bin" / ("openage-relay-linux-" + arch)
        binary.write_text(content, encoding="utf-8")
        binary.chmod(0o644)  # Simulate SFTP upload stripping executable bits.
        return binary

    def command(self, env=None, arch="x86_64"):
        self.create_binary("arm64" if arch == "aarch64" else "amd64")
        return relay.make_command(self.root, env or {}, arch)

    def test_default_and_panel_owned_port(self):
        path, command, port = self.command({"SERVER_PORT": "42001",
                                            "SERVER_IP": "203.0.113.1"})
        self.assertEqual(port, 42001)
        self.assertEqual(command[:5],
                         [str(path), "--listen", "0.0.0.0", "--port", "42001"])
        self.assertIn("--max-rooms", command)
        self.assertIn("--max-connections", command)

    def test_standalone_default(self):
        _, command, port = self.command()
        self.assertEqual(port, 40000)
        self.assertEqual(command[command.index("--port") + 1], "40000")

    def test_arm64_selection(self):
        binary, _, _ = self.command({"SERVER_PORT": "42101"}, "aarch64")
        self.assertEqual(binary.name, "openage-relay-linux-arm64")

    def test_config_file_and_private_tokens(self):
        token = "ad" * 32
        (self.root / "relay.json").write_text(json.dumps({
            "listen": "127.0.0.1", "port": 42009, "max_rooms": 3,
            "max_connections": 5, "player_tokens": [token]}))
        _, command, _ = self.command({"SERVER_PORT": "42011"})
        self.assertEqual(command[command.index("--port") + 1], "42011")
        self.assertEqual(command[command.index("--listen") + 1], "127.0.0.1")
        self.assertEqual(command[command.index("--max-rooms") + 1], "3")
        self.assertEqual(command[command.index("--max-connections") + 1], "5")
        self.assertEqual(command[-2:], ["--player-token", token])

    def test_invalid_ports_and_tokens(self):
        for port in ("0", "65536", "abc", "-1"):
            with self.subTest(port=port):
                with self.assertRaises(ValueError):
                    self.command({"SERVER_PORT": port})
        for invalid in ([0], ["ab"], ["00" * 32], ["a" * 64] * 2):
            with self.subTest(tokens=invalid):
                (self.root / "relay.json").write_text(
                    json.dumps({"player_tokens": invalid}))
                with self.assertRaises(ValueError):
                    self.command()

    def test_invalid_config_and_unsupported_cpu(self):
        self.create_binary()
        (self.root / "relay.json").write_text('{"unknown": 3}')
        with self.assertRaises(ValueError):
            relay.make_command(self.root, {}, "x86_64")
        (self.root / "relay.json").unlink()
        with self.assertRaises(ValueError):
            relay.make_command(self.root, {}, "i386")

    def test_missing_binary_gives_actionable_error(self):
        with self.assertRaisesRegex(FileNotFoundError, "Upload the complete"):
            relay.make_command(self.root, {}, "x86_64")

    @unittest.skipUnless(sys.platform == "linux", "Linux-only exec behavior")
    def test_stock_app_replaces_python_and_repairs_upload_mode(self):
        for name in ("app.py", "relay.py"):
            shutil.copyfile(Path(relay.__file__).parent / name, self.root / name)
        binary = self.create_binary(content=(
            "#!/bin/sh\n"
            "printf 'Native relay arguments: %s\\n' \"$*\"\n"
            "exit 17\n"))
        result = subprocess.run([sys.executable, str(self.root / "app.py")],
                                env={**os.environ, "SERVER_PORT": "45713"},
                                text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 17, result.stderr)
        self.assertIn("--listen 0.0.0.0 --port 45713", result.stdout)
        self.assertIn("[FVD/Pelican] Starting native relay", result.stdout)
        self.assertTrue(binary.stat().st_mode & 0o100)


if __name__ == "__main__":
    unittest.main()
