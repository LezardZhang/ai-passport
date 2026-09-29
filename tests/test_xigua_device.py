#!/usr/bin/env python3
"""Fake-serial tests for the host USB configuration client."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("xigua_device", ROOT / "tools" / "xigua_device.py")
assert SPEC and SPEC.loader
DEVICE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DEVICE)


def wire_response(request_id: int, *, ok: bool = True, data: dict | None = None,
                  error: str | None = None) -> bytes:
    payload = {"id": request_id, "ok": ok}
    if data is not None:
        payload["data"] = data
    if error is not None:
        payload["error"] = error
    return b"XG1 " + json.dumps(payload, separators=(",", ":")).encode() + b"\n"


class FakeSerial:
    def __init__(self, incoming: bytes = b"") -> None:
        self.incoming = bytearray(incoming)
        self.writes: list[bytes] = []
        self.closed = False

    def read(self, size: int = 1) -> bytes:
        if not self.incoming:
            return b""
        result = bytes(self.incoming[:size])
        del self.incoming[:size]
        return result

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        return len(data)

    def close(self) -> None:
        self.closed = True


class DeviceProtocolTest(unittest.TestCase):
    def setUp(self) -> None:
        patcher = patch.object(DEVICE.secrets, "randbelow", return_value=0)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_session_ids_ignore_previous_client_response(self) -> None:
        serial_port = FakeSerial(wire_response(1, data={"stale": True}) + wire_response(91, data={"fresh": True}))
        with patch.object(DEVICE.secrets, "randbelow", return_value=90):
            response = DEVICE.DeviceClient(serial_port).request("status")
        self.assertEqual(response["data"], {"fresh": True})

    def test_short_write_is_not_retried(self) -> None:
        serial_port = FakeSerial()
        with patch.object(serial_port, "write", return_value=1) as write:
            with self.assertRaises(DEVICE.DeviceError):
                DEVICE.DeviceClient(serial_port).request("status")
        write.assert_called_once()

    def test_ignores_logs_and_other_request_ids_and_handles_partial_bytes(self) -> None:
        serial_port = FakeSerial(
            b"boot: starting\r\n"
            + wire_response(77, data={"wrong": True})
            + b"XG1 {\"id\":1,\"ok\":true,\"data\":{\"ready\":true}}\r\n"
        )
        response = DEVICE.DeviceClient(serial_port).request("status")
        self.assertEqual(response["data"], {"ready": True})
        self.assertEqual(len(serial_port.writes), 1)
        self.assertEqual(json.loads(serial_port.writes[0][4:]), {"id": 1, "op": "status"})

    def test_overlong_log_line_is_discarded_before_valid_response(self) -> None:
        serial_port = FakeSerial(b"z" * 16_500 + b"\n" + wire_response(1, data={"ok": 1}))
        response = DEVICE.DeviceClient(serial_port).request("status", timeout=1)
        self.assertEqual(response["data"], {"ok": 1})

    def test_duplicate_keys_and_trailing_json_are_rejected(self) -> None:
        self.assertIsNone(DEVICE.parse_response_line(b'XG1 {"id":1,"id":2,"ok":true}\n'))
        self.assertIsNone(DEVICE.parse_response_line(b'XG1 {"id":1,"ok":true} {}\n'))

    def test_timeout_does_not_resend_ambiguous_request(self) -> None:
        serial_port = FakeSerial()
        with self.assertRaisesRegex(DEVICE.DeviceError, "was not retried"):
            DEVICE.DeviceClient(serial_port).request("wifi.set", {"password": "hidden"}, timeout=0.01)
        self.assertEqual(len(serial_port.writes), 1)

    def test_response_and_cli_output_do_not_echo_secret(self) -> None:
        secret = "api-key-never-print"
        serial_port = FakeSerial(wire_response(1, data={"saved": True, "echo": secret}))
        args = DEVICE.build_parser().parse_args(
            ["--port", "COM9", "ai-set", "--endpoint", "https://example.invalid/v1/chat/completions", "--model", "m"]
        )
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = DEVICE.run(args, serial_opener=lambda _: serial_port,
                                password_prompt=lambda _: secret)
        self.assertEqual(result, 0)
        self.assertNotIn(secret, output.getvalue())
        self.assertIn("[REDACTED]", output.getvalue())
        request = json.loads(serial_port.writes[0][4:])
        self.assertEqual(request["key"], secret)
        self.assertTrue(serial_port.closed)

    def test_wifi_password_is_prompted_and_not_part_of_cli_arguments(self) -> None:
        secret = "wifi-private"
        serial_port = FakeSerial(wire_response(1, data={"saved": True}))
        args = DEVICE.build_parser().parse_args(
            ["--port", "COM9", "wifi-set", "--slot", "3", "--ssid", "Home", "--priority", "9"]
        )
        prompted: list[str] = []
        with contextlib.redirect_stdout(io.StringIO()):
            DEVICE.run(args, serial_opener=lambda _: serial_port,
                       password_prompt=lambda prompt: prompted.append(prompt) or secret)
        request = json.loads(serial_port.writes[0][4:])
        self.assertEqual(request["password"], secret)
        self.assertTrue(request["enabled"])
        self.assertEqual(request["priority"], 9)
        self.assertEqual(len(prompted), 1)

    def test_complete_export_includes_lossless_metadata_and_no_credentials(self) -> None:
        meta = {"format": DEVICE.EXPORT_FORMAT, "boot_id": 4, "revision": 9,
                "count": 1, "index": 0}
        clock = {"boot_id": 4, "monotonic_ms": "9007199254740993",
                 "unix_ms": "1780000000000", "quality": "trusted"}
        record = {"id": 17, "revision": 3, "type": "feed", "value": 120,
                  "active": False, "start": clock, "end": clock,
                  "duration_ms": "0", "duration_quality": "unknown"}
        stream = FakeSerial(wire_response(1, data=meta) +
                            wire_response(2, data={**meta, "record": record}) +
                            wire_response(3, data={**meta, "index": 1}))
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "records.json"
            args = DEVICE.build_parser().parse_args(
                ["--port", "COM9", "export", "--output", str(destination)])
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(DEVICE.run(args, serial_opener=lambda _: stream), 0)
            saved = json.loads(destination.read_text(encoding="utf-8"))
            self.assertEqual(saved["records"], [record])
            self.assertEqual(saved["count"], 1)
            self.assertNotIn("key", destination.read_text(encoding="utf-8"))
            self.assertEqual([json.loads(w[4:])["op"] for w in stream.writes],
                             ["records.begin", "records.item", "records.finish"])
            self.assertEqual(json.loads(stream.writes[2][4:])["index"], 1)
            self.assertTrue(stream.closed)

    def test_changed_revision_or_interrupted_transfer_never_publishes(self) -> None:
        begin = {"format": DEVICE.EXPORT_FORMAT, "boot_id": 4, "revision": 9,
                 "count": 0, "index": 0}
        stale = {**begin, "revision": 10}
        for replies in (wire_response(1, data=begin) + wire_response(2, data=stale),
                        wire_response(1, data=begin)):
            stream = FakeSerial(replies)
            with tempfile.TemporaryDirectory() as directory:
                destination = Path(directory) / "records.json"
                args = DEVICE.build_parser().parse_args(
                    ["--port", "COM9", "export", "--output", str(destination)])
                with self.assertRaises(DEVICE.DeviceError):
                    DEVICE.run(args, serial_opener=lambda _: stream)
                self.assertFalse(destination.exists())
                self.assertFalse(list(Path(directory).glob(".xigua-export-*")))

    def test_export_rejects_extra_fields_and_preserves_existing_file(self) -> None:
        meta = {"format": DEVICE.EXPORT_FORMAT, "boot_id": 4, "revision": 9,
                "count": 0, "index": 0, "api_key": "secret"}
        stream = FakeSerial(wire_response(1, data=meta))
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "records.json"
            args = DEVICE.build_parser().parse_args(
                ["--port", "COM9", "export", "--output", str(destination)])
            with self.assertRaises(DEVICE.DeviceError):
                DEVICE.run(args, serial_opener=lambda _: stream)
            self.assertFalse(destination.exists())
            destination.write_text("old", encoding="utf-8")
            with self.assertRaisesRegex(DEVICE.DeviceError, "already exists"):
                DEVICE.run(args, serial_opener=lambda _: stream)
            self.assertEqual(destination.read_text(encoding="utf-8"), "old")
            with self.assertRaisesRegex(DEVICE.DeviceError, "already exists"):
                DEVICE.write_export(destination, {"format": DEVICE.EXPORT_FORMAT})
            self.assertEqual(destination.read_text(encoding="utf-8"), "old")

    def test_export_checks_finish_after_nonempty_page(self) -> None:
        meta = {"format": DEVICE.EXPORT_FORMAT, "boot_id": 4, "revision": 9,
                "count": 1, "index": 0}
        clock = {"boot_id": 4, "monotonic_ms": "1", "unix_ms": "0", "quality": "unknown"}
        record = {"id": 17, "revision": 1, "type": "feed", "value": 120,
                  "active": False, "start": clock, "end": clock,
                  "duration_ms": "0", "duration_quality": "unknown"}
        stream = FakeSerial(wire_response(1, data=meta) +
                            wire_response(2, data={**meta, "record": record}) +
                            wire_response(3, ok=False, error="snapshot_changed"))
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "records.json"
            args = DEVICE.build_parser().parse_args(
                ["--port", "COM9", "export", "--output", str(destination)])
            with self.assertRaises(DEVICE.DeviceError):
                DEVICE.run(args, serial_opener=lambda _: stream)
            self.assertFalse(destination.exists())
            self.assertEqual(len(stream.writes), 3)

    def test_oversized_decimal_record_is_rejected(self) -> None:
        clock = {"boot_id": 1, "monotonic_ms": "1" * 5000,
                 "unix_ms": "0", "quality": "unknown"}
        record = {"id": 1, "revision": 1, "type": "feed", "value": 120,
                  "active": False, "start": clock, "end": clock,
                  "duration_ms": "0", "duration_quality": "unknown"}
        with self.assertRaises(DEVICE.DeviceError):
            DEVICE._record(record)

    def test_duplicate_record_ids_are_rejected_across_pages(self) -> None:
        meta = {"format": DEVICE.EXPORT_FORMAT, "boot_id": 4, "revision": 9,
                "count": 2, "index": 0}
        clock = {"boot_id": 4, "monotonic_ms": "1", "unix_ms": "0", "quality": "unknown"}
        record = {"id": 17, "revision": 1, "type": "feed", "value": 120,
                  "active": False, "start": clock, "end": clock,
                  "duration_ms": "0", "duration_quality": "unknown"}
        stream = FakeSerial(wire_response(1, data=meta) +
                            wire_response(2, data={**meta, "record": record}) +
                            wire_response(3, data={**meta, "index": 1, "record": record}))
        with self.assertRaises(DEVICE.DeviceError):
            DEVICE.export_records(DEVICE.DeviceClient(stream))
        self.assertEqual(len(stream.writes), 3)


if __name__ == "__main__":
    unittest.main()
