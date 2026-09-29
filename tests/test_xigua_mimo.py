import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("mimo", ROOT / "tools/xigua_mimo.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
P = json.loads(m.PROFILE.read_text(encoding="utf-8"))


class MiMoTests(unittest.TestCase):
    def test_all_roles(self):
        self.assertEqual(len(P["roles"]), 8)
        for role in ("daily", "analysis", "analysis_backup", "multimodal"):
            body = m.payload(P, role, "hello")
            self.assertEqual(body["model"], P["roles"][role])
            self.assertNotIn("audio", body)

    def test_asr(self):
        body = m.payload(P, "asr", audio="data:audio/wav;base64,AA==")
        self.assertEqual(body["messages"][0]["content"][0]["type"], "input_audio")
        self.assertNotIn("thinking", body)
        with self.assertRaises(m.ClientError):
            m.payload(P, "asr")

    def test_tts_and_optional_voices(self):
        body = m.payload(P, "tts", "hello")
        self.assertEqual(body["messages"][-1]["role"], "assistant")
        self.assertEqual(body["audio"]["voice"], "mimo_default")
        body = m.payload(P, "voiceclone", "hello", "data:audio/wav;base64,AA==")
        self.assertEqual(body["audio"]["voice"], "AA==")
        body = m.payload(P, "voicedesign", "hello", style="a warm voice")
        self.assertNotIn("voice", body["audio"])
        self.assertEqual(body["messages"][0]["role"], "user")
        for role in ("voiceclone", "voicedesign"):
            with self.assertRaises(m.ClientError):
                m.payload(P, role, "hello")

    def test_https_and_credentials(self):
        with self.assertRaises(m.ClientError):
            m.request({**P, "base_url": "http://example.invalid/v1"}, "fake")
        with self.assertRaises(m.ClientError):
            m.request(P, "fake\nkey")
        self.assertIsNone(m.NoRedirect().redirect_request())

    def test_http_errors_are_redacted(self):
        error = m.urllib.error.HTTPError("https://example.invalid", 401, "SECRET", {}, None)
        with patch.object(m.urllib.request, "build_opener") as factory:
            factory.return_value.open.side_effect = error
            with self.assertRaisesRegex(m.ClientError, "^model service returned HTTP 401$"):
                m.request(P, "fake")


if __name__ == "__main__":
    unittest.main()
