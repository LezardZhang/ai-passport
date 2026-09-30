#!/usr/bin/env python3
"""Contract checks for the story-only MiMo TTS path."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/xigua_ai.c").read_text(encoding="utf-8")
HEADER = (ROOT / "main/xigua_ai.h").read_text(encoding="utf-8")
APP = (ROOT / "main/xigua_app.c").read_text(encoding="utf-8")


def main() -> None:
    required_source = (
        'XIGUA_AI_TTS_MODEL',
        '"pcm16"',
        '"mimo_default"',
        '"stream", true',
        '"delta"',
        '"audio"',
        'mbedtls_base64_decode',
        'bsp_audio_set_format(XIGUA_AI_TTS_HZ, XIGUA_AI_TTS_BITS',
        'bsp_audio_write(pcm, decoded)',
        's_story_system_prompt',
        'XIGUA_AI_VOICE_SPEAKING',
    )
    for marker in required_source:
        assert marker in SOURCE, marker
    assert 'XIGUA_AI_TTS_HZ 24000' in SOURCE
    assert 'xigua_ai_request_story' in HEADER
    assert 'xigua_ai_request_story()' in APP
    assert '"讲故事"' in APP
    print("Xigua story-mode TTS contract: PASS")


if __name__ == "__main__":
    main()
