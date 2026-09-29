#!/usr/bin/env python3
"""Configure and query an Xigua device over its USB serial console."""

from __future__ import annotations

import argparse
import getpass
import json
import os
from pathlib import Path
import re
import secrets
import sys
import tempfile
import time
from typing import Any, Callable
from urllib.parse import urlsplit


PREFIX = b"XG1 "
MAX_LINE_BYTES = 16_384
MAX_ASK_BYTES = 512
MAX_REQUEST_ID = 2_147_483_647
SERIAL_BAUD = 115_200
SERIAL_POLL_TIMEOUT = 0.05
ASK_TIMEOUT = 50.0
EXPORT_FORMAT = "xigua-records-v1"
MAX_RECORDS = 128


class DeviceError(Exception):
    """Safe-to-display client error; messages never include request contents."""


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def _reject_non_json_constant(_: str) -> None:
    raise ValueError("invalid JSON constant")


def parse_response_line(line: bytes) -> dict[str, Any] | None:
    """Parse one protocol line, ignoring boot and log output."""
    if not line.startswith(PREFIX):
        return None
    try:
        payload = json.loads(
            line[len(PREFIX):].decode("utf-8", errors="strict"),
            object_pairs_hook=_reject_duplicate_keys,
            parse_constant=_reject_non_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError):
        return None
    if not isinstance(payload, dict):
        return None
    response_id = payload.get("id")
    if type(response_id) is not int or not 1 <= response_id <= MAX_REQUEST_ID:
        return None
    if type(payload.get("ok")) is not bool:
        return None
    if "error" in payload and not isinstance(payload["error"], str):
        return None
    if "data" in payload and not isinstance(payload["data"], dict):
        return None
    return payload


class DeviceClient:
    def __init__(self, serial_port: Any, *, line_limit: int = MAX_LINE_BYTES):
        self._serial = serial_port
        self._line_limit = min(max(1, line_limit), MAX_LINE_BYTES)
        self._next_id = secrets.randbelow(MAX_REQUEST_ID) + 1

    def _read_line(self, deadline: float) -> bytes | None:
        line = bytearray()
        discarding = False
        while time.monotonic() < deadline:
            byte = self._serial.read(1)
            if not byte:
                continue
            if byte == b"\n":
                if discarding:
                    return b""
                return bytes(line[:-1] if line.endswith(b"\r") else line)
            if not discarding:
                if len(line) >= self._line_limit:
                    discarding = True
                    line.clear()
                else:
                    line.extend(byte)
        return None

    def request(self, operation: str, fields: dict[str, Any] | None = None,
                *, timeout: float = 5.0) -> dict[str, Any]:
        request_id = self._next_id
        self._next_id = 1 if request_id >= MAX_REQUEST_ID else request_id + 1
        request = {"id": request_id, "op": operation}
        if fields:
            if "id" in fields or "op" in fields:
                raise DeviceError("reserved request field")
            request.update(fields)
        try:
            encoded = PREFIX + json.dumps(
                request, ensure_ascii=False, separators=(",", ":"),
            ).encode("utf-8") + b"\n"
        except (TypeError, ValueError, UnicodeEncodeError):
            raise DeviceError("request contains invalid data") from None
        if len(encoded) - len(PREFIX) - 1 > 2048:
            raise DeviceError("request exceeds device input limit")
        try:
            if self._serial.write(encoded) != len(encoded):
                raise DeviceError("short write")
        except Exception:
            raise DeviceError("could not send request over serial") from None

        deadline = time.monotonic() + max(0.0, timeout)
        while time.monotonic() < deadline:
            line = self._read_line(deadline)
            if line is None:
                break
            response = parse_response_line(line)
            if response is None or response["id"] != request_id:
                continue
            if response["ok"]:
                return response
            # Only stable machine-readable error codes are shown; raw server
            # messages could contain credentials or other private values.
            code = response.get("error", "device_error")
            safe_code = code if code and all(c.isascii() and (c.isalnum() or c in "_.-") for c in code) else "device_error"
            details = response.get("data", {})
            status = details.get("http_status")
            suffix = f", HTTP {status}" if type(status) is int and 100 <= status <= 599 else ""
            raise DeviceError(f"device rejected request ({safe_code}{suffix})")
        raise DeviceError("response timed out; the request may have been applied and was not retried")


def open_serial(port: str) -> Any:
    """Open lazily so help and argument validation work without pyserial."""
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError:
        raise DeviceError("pyserial is required; install it with python -m pip install pyserial") from None
    try:
        link = serial.Serial(port=None, baudrate=SERIAL_BAUD, timeout=SERIAL_POLL_TIMEOUT,
                             write_timeout=1.0)
        # Set line states before opening: some boards reset when DTR/RTS changes.
        link.dtr = False
        link.rts = False
        link.port = port
        link.open()
        return link
    except Exception:
        raise DeviceError("could not open the selected serial port") from None


def _bounded_int(value: Any, low: int, high: int) -> int:
    if type(value) is not int or not low <= value <= high:
        raise DeviceError("invalid record export response")
    return value


def _decimal(value: Any, *, signed: bool = False) -> str:
    pattern = r"-?(?:0|[1-9][0-9]*)" if signed else r"(?:0|[1-9][0-9]*)"
    if type(value) is not str or len(value) > 20 or not re.fullmatch(pattern, value):
        raise DeviceError("invalid record export response")
    number = int(value)
    valid = (-(1 << 63) <= number < (1 << 63)) if signed else (0 <= number < (1 << 64))
    if not valid:
        raise DeviceError("invalid record export response")
    return value


def _clock(value: Any) -> dict[str, Any]:
    if type(value) is not dict or set(value) != {"boot_id", "monotonic_ms", "unix_ms", "quality"}:
        raise DeviceError("invalid record export response")
    quality = value["quality"]
    if quality not in ("unknown", "trusted", "reconstructed"):
        raise DeviceError("invalid record export response")
    return {"boot_id": _bounded_int(value["boot_id"], 0, 0xffffffff),
            "monotonic_ms": _decimal(value["monotonic_ms"]),
            "unix_ms": _decimal(value["unix_ms"], signed=True), "quality": quality}


def _record(value: Any) -> dict[str, Any]:
    keys = {"id", "revision", "type", "value", "active", "start", "end",
            "duration_ms", "duration_quality"}
    if type(value) is not dict or set(value) != keys:
        raise DeviceError("invalid record export response")
    kind = value["type"]
    duration_quality = value["duration_quality"]
    if kind not in ("feed", "sleep", "diaper", "bath", "tummy") or \
            duration_quality not in ("unknown", "monotonic", "utc") or \
            type(value["active"]) is not bool:
        raise DeviceError("invalid record export response")
    return {"id": _bounded_int(value["id"], 1, 0xffffffff),
            "revision": _bounded_int(value["revision"], 0, 0xffffffff),
            "type": kind, "value": _bounded_int(value["value"], 0, 0xffffffff),
            "active": value["active"], "start": _clock(value["start"]),
            "end": _clock(value["end"]), "duration_ms": _decimal(value["duration_ms"]),
            "duration_quality": duration_quality}


def export_records(client: DeviceClient) -> dict[str, Any]:
    def metadata(response: dict[str, Any], *, index: int, token: tuple[int, int, int] | None = None) -> tuple[int, int, int]:
        data = response.get("data")
        if type(data) is not dict or data.get("format") != EXPORT_FORMAT or \
                _bounded_int(data.get("index"), 0, MAX_RECORDS) != index:
            raise DeviceError("invalid record export response")
        current = (_bounded_int(data.get("boot_id"), 1, 0xffffffff),
                   _bounded_int(data.get("revision"), 0, 0xffffffff),
                   _bounded_int(data.get("count"), 0, MAX_RECORDS))
        if token is not None and current != token:
            raise DeviceError("record export changed during transfer")
        return current

    begin = client.request("records.begin")
    token = metadata(begin, index=0)
    if set(begin["data"]) != {"format", "boot_id", "revision", "count", "index"}:
        raise DeviceError("invalid record export response")
    fields = {"boot_id": token[0], "revision": token[1], "count": token[2]}
    records = []
    seen_ids: set[int] = set()
    for index in range(token[2]):
        response = client.request("records.item", {**fields, "index": index})
        metadata(response, index=index, token=token)
        if set(response["data"]) != {"format", "boot_id", "revision", "count", "index", "record"}:
            raise DeviceError("invalid record export response")
        record = _record(response["data"]["record"])
        if record["id"] in seen_ids:
            raise DeviceError("invalid record export response")
        seen_ids.add(record["id"])
        records.append(record)
    finish = client.request("records.finish", {**fields, "index": token[2]})
    metadata(finish, index=token[2], token=token)
    if set(finish["data"]) != {"format", "boot_id", "revision", "count", "index"}:
        raise DeviceError("invalid record export response")
    return {"format": EXPORT_FORMAT, "boot_id": token[0], "revision": token[1],
            "count": token[2], "records": records}


def write_export(path: Path, document: dict[str, Any]) -> None:
    """Publish a complete export with atomic no-overwrite creation."""
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", newline="\n",
                                         dir=path.parent, prefix=".xigua-export-",
                                         suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(document, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.link(temporary, path)
    except FileExistsError:
        raise DeviceError("output file already exists") from None
    except OSError:
        raise DeviceError("could not write export file") from None
    finally:
        if temporary is not None:
            try:
                temporary.unlink(missing_ok=True)
            except OSError:
                pass


def _slot(value: str) -> int:
    try:
        parsed = int(value, 10)
    except ValueError:
        raise argparse.ArgumentTypeError("slot must be an integer from 0 to 7") from None
    if not 0 <= parsed <= 7:
        raise argparse.ArgumentTypeError("slot must be an integer from 0 to 7")
    return parsed


def _priority(value: str) -> int:
    try:
        parsed = int(value, 10)
    except ValueError:
        raise argparse.ArgumentTypeError("priority must be an integer from 0 to 255") from None
    if not 0 <= parsed <= 255:
        raise argparse.ArgumentTypeError("priority must be an integer from 0 to 255")
    return parsed


def _ask_text(value: str) -> str:
    if len(value.encode("utf-8")) > MAX_ASK_BYTES:
        raise argparse.ArgumentTypeError("text must be at most 512 UTF-8 bytes")
    return value


def _https_endpoint(value: str) -> str:
    try:
        parsed = urlsplit(value)
    except ValueError:
        raise argparse.ArgumentTypeError("endpoint must be a full https URL") from None
    if parsed.scheme != "https" or not parsed.hostname or not parsed.path:
        raise argparse.ArgumentTypeError("endpoint must be a full https URL")
    if parsed.username is not None or parsed.password is not None:
        raise argparse.ArgumentTypeError("do not embed credentials in the endpoint URL")
    return value


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="USB serial port, such as COM4")
    commands = parser.add_subparsers(dest="command", required=True)

    commands.add_parser("status", help="show masked device status")

    wifi_set = commands.add_parser("wifi-set", help="save a Wi-Fi profile")
    wifi_set.add_argument("--slot", required=True, type=_slot)
    wifi_set.add_argument("--ssid", required=True)
    wifi_set.add_argument("--priority", type=_priority, default=0)
    wifi_set.add_argument("--disabled", action="store_true", help="save the profile disabled")

    wifi_clear = commands.add_parser("wifi-clear", help="clear a Wi-Fi profile")
    wifi_clear.add_argument("--slot", required=True, type=_slot)

    ai_set = commands.add_parser("ai-set", help="save an AI endpoint and model")
    ai_set.add_argument("--endpoint", required=True, type=_https_endpoint)
    ai_set.add_argument("--model", required=True)

    commands.add_parser("ai-clear", help="clear AI endpoint credentials")

    time_set = commands.add_parser("time-set", help="set the POSIX timezone")
    time_set.add_argument("--timezone", required=True)

    ask = commands.add_parser("ask", help="send a paid AI request")
    ask.add_argument("text", type=_ask_text)
    commands.add_parser("test-ai", help="send the paid connectivity test prompt")
    export = commands.add_parser("export", help="export all record metadata to JSON")
    export.add_argument("--output", required=True, type=Path, help="new JSON file; never overwritten")
    return parser


def _output_text(response: dict[str, Any], secrets: list[str]) -> str:
    rendered = json.dumps(response.get("data", {}), ensure_ascii=False, indent=2)
    for secret in secrets:
        if secret:
            rendered = rendered.replace(secret, "[REDACTED]")
    return rendered


def run(args: argparse.Namespace, *, serial_opener: Callable[[str], Any] = open_serial,
        password_prompt: Callable[[str], str] = getpass.getpass) -> int:
    secrets: list[str] = []
    fields: dict[str, Any] = {}
    operation = ""
    timeout = 5.0
    if args.command == "status":
        operation = "status"
    elif args.command == "export":
        if args.output.exists():
            raise DeviceError("output file already exists")
        operation = "records.begin"
    elif args.command == "wifi-set":
        password = password_prompt("Wi-Fi password (input hidden): ")
        secrets.append(password)
        operation = "wifi.set"
        fields = {"slot": args.slot, "ssid": args.ssid, "password": password,
                  "enabled": not args.disabled, "priority": args.priority}
    elif args.command == "wifi-clear":
        operation, fields = "wifi.clear", {"slot": args.slot}
    elif args.command == "ai-set":
        key = password_prompt("AI API key (input hidden): ")
        secrets.append(key)
        operation = "ai.set"
        fields = {"endpoint": args.endpoint, "key": key, "model": args.model}
    elif args.command == "ai-clear":
        operation = "ai.clear"
    elif args.command == "time-set":
        operation, fields = "time.set", {"timezone": args.timezone}
    elif args.command in ("ask", "test-ai"):
        operation = "ai.ask"
        fields = {"text": args.text if args.command == "ask" else "Reply OK only."}
        timeout = ASK_TIMEOUT
    else:
        raise DeviceError("unsupported command")

    serial_port = serial_opener(args.port)
    try:
        client = DeviceClient(serial_port)
        if args.command == "export":
            document = export_records(client)
        else:
            response = client.request(operation, fields, timeout=timeout)
    finally:
        try:
            serial_port.close()
        except Exception:
            pass
    if args.command == "export":
        write_export(args.output, document)
        print(f"Exported {document['count']} records to {args.output}")
    else:
        print(_output_text(response, secrets))
    return 0


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return run(args)
    except DeviceError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
