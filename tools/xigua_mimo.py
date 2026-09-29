#!/usr/bin/env python3
"""Host-side MiMo role configuration and bounded cloud integration checks.

This tool does not implement microphone capture or speech playback on the board.
Credentials come from MIMO_API_KEY or a hidden prompt, never this profile file.
"""
from __future__ import annotations
import argparse
import base64
import getpass
import json
import os
from pathlib import Path
import sys
import urllib.error
import urllib.request
from urllib.parse import urlsplit

PROFILE = Path(__file__).with_name("xigua_mimo_profiles.json")
MAX_AUDIO = 1_000_000
MAX_RESPONSE = 2_000_000


class ClientError(Exception):
    """A fixed diagnostic safe to print without credentials or server bodies."""


def audio_data(path: Path) -> str:
    if path.suffix.lower() not in (".wav", ".mp3"):
        raise ClientError("audio must be WAV or MP3")
    with path.open("rb") as source:
        data = source.read(MAX_AUDIO + 1)
    if not data or len(data) > MAX_AUDIO:
        raise ClientError("audio must be 1..1000000 bytes")
    mime = "audio/wav" if path.suffix.lower() == ".wav" else "audio/mpeg"
    return "data:" + mime + ";base64," + base64.b64encode(data).decode("ascii")


def payload(profile: dict, role: str, text: str = "", audio: str | None = None,
            style: str = "") -> dict:
    if role not in profile["roles"]:
        raise ClientError("unknown model role")
    body = {"model": profile["roles"][role], "stream": False}
    if role == "asr":
        if not audio:
            raise ClientError("ASR requires an audio file")
        body.update(messages=[{"role": "user", "content": [
            {"type": "input_audio", "input_audio": {"data": audio}}]}],
            asr_options={"language": profile["asr_language"]})
    elif role in ("tts", "voiceclone", "voicedesign"):
        if not text.strip():
            raise ClientError("speech synthesis requires text")
        messages = []
        if role == "voicedesign" and not style.strip():
            raise ClientError("voice design requires a voice description")
        if style:
            messages.append({"role": "user", "content": style})
        messages.append({"role": "assistant", "content": text})
        options = {"format": profile["audio_format"]}
        if role == "tts":
            options["voice"] = profile["voice"]
        elif role == "voiceclone":
            if not audio:
                raise ClientError("voice cloning requires a reference audio file")
            # The voice field takes raw Base64, unlike ASR's input_audio data URL.
            options["voice"] = audio.split(",", 1)[-1]
        body.update(messages=messages, audio=options)
    else:
        if not text.strip():
            raise ClientError("text role requires a prompt")
        body.update(messages=[{"role": "user", "content": text}],
                    max_completion_tokens=profile["max_completion_tokens"],
                    thinking={"type": "disabled"})
    return body


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def request(profile: dict, key: str, body: dict | None = None) -> dict:
    base = profile["base_url"].rstrip("/")
    parsed = urlsplit(base)
    if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password:
        raise ClientError("base URL must use HTTPS without embedded credentials")
    if not key or any(ord(c) < 33 or ord(c) > 126 for c in key):
        raise ClientError("invalid API credential")
    path = "/models" if body is None else "/chat/completions"
    wire = None if body is None else json.dumps(body, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(base + path, data=wire,
        headers={"api-key": key, "Content-Type": "application/json", "Accept": "application/json"})
    try:
        with urllib.request.build_opener(NoRedirect).open(req,
                timeout=profile["timeout_seconds"]) as response:
            raw = response.read(MAX_RESPONSE + 1)
        if len(raw) > MAX_RESPONSE:
            raise ClientError("response exceeds configured size limit")
        result = json.loads(raw)
        if not isinstance(result, dict) or "error" in result:
            raise ClientError("invalid model response")
        return result
    except urllib.error.HTTPError as exc:
        raise ClientError("model service returned HTTP " + str(exc.code)) from None
    except (urllib.error.URLError, TimeoutError, ValueError):
        raise ClientError("model request failed or returned invalid JSON") from None


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("role", choices=["models", "daily", "analysis", "analysis_backup",
                        "multimodal", "asr", "tts", "voiceclone", "voicedesign"])
    parser.add_argument("--text", default="")
    parser.add_argument("--audio", type=Path)
    parser.add_argument("--style", default="")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        profile = json.loads(PROFILE.read_text(encoding="utf-8"))
        speech = args.role in ("tts", "voiceclone", "voicedesign")
        if speech and (args.output is None or args.output.exists()):
            raise ClientError("speech requires a new --output WAV path")
        body = None if args.role == "models" else payload(profile, args.role, args.text,
            audio_data(args.audio) if args.audio else None, args.style)
        key = os.environ.get("MIMO_API_KEY") or getpass.getpass("MiMo API key (hidden): ")
        result = request(profile, key, body)
        key = None
        if args.role == "models":
            print("\n".join(m["id"] for m in result["data"]))
        elif speech:
            encoded = result["choices"][0]["message"]["audio"]["data"]
            audio = base64.b64decode(encoded, validate=True)
            if not audio.startswith(b"RIFF") or audio[8:12] != b"WAVE":
                raise ClientError("service did not return a WAV file")
            with args.output.open("xb") as destination:
                destination.write(audio)
            print("Speech saved:", args.output)
        else:
            print(result["choices"][0]["message"]["content"])
        return 0
    except ClientError as exc:
        print(str(exc), file=sys.stderr)
    except (OSError, KeyError, IndexError, TypeError, ValueError):
        print("invalid profile, response, or local file", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
