#!/usr/bin/env python3
"""Contract checks for the story-only MiMo TTS path."""

from pathlib import Path
import re


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
        '"audio"',
        'xigua_tts_stream_feed',
        'xigua_audio_output_prepare(XIGUA_TTS_PLAYBACK_HZ, XIGUA_AI_TTS_BITS',
        'bsp_audio_write(playback->pcm, bytes)',
        's_story_system_prompt',
        'XIGUA_AI_VOICE_SPEAKING',
    )
    for marker in required_source:
        assert marker in SOURCE, marker
    assert 'XIGUA_AI_TTS_HZ 24000' in SOURCE
    assert 'tts_playback_task' in SOURCE
    assert 'xigua_adpcm_encode' in SOURCE and 'xigua_adpcm_decode' in SOURCE
    assert 'xigua_tts_downsample_push' in SOURCE
    tts = SOURCE.split('static esp_err_t tts_stream', 1)[1].split('static esp_err_t record_voice', 1)[0]
    assert tts.index('esp_http_client_fetch_headers') < tts.index('xTaskCreate(tts_playback_task')
    assert 'xigua_tts_cache_publish' in SOURCE
    assert 'xigua_tts_cache_advance' in SOURCE
    assert 'tts_play_cached' in SOURCE
    assert 'CONFIG_I2S_ISR_IRAM_SAFE=y' in (ROOT / 'sdkconfig.defaults').read_text()
    for control in ('xigua_ai_pause_voice', 'xigua_ai_resume_voice', 'xigua_ai_restart_voice'):
        assert control in HEADER and control in APP
    assert 'tts_playback_destroy(playback)' in SOURCE
    assert '正在缓冲语音' in APP
    assert 'XIGUA_AI_TTS_LINE_MAX' not in SOURCE
    defaults = (ROOT / 'sdkconfig.defaults').read_text(encoding='utf-8')
    # A smaller TLS RX limit rejects the real service's 16 KiB records before
    # the incremental SSE decoder can see them. TX can remain smaller.
    incoming = re.search(r'^CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=(\d+)$', defaults, re.M)
    assert incoming and int(incoming.group(1)) == 16384
    # Idle replay failed when dynamic RX tried to allocate a full record mid-play.
    assert 'CONFIG_MBEDTLS_DYNAMIC_BUFFER=n' in defaults
    assert 'CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=y' in defaults
    assert 'CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096' in defaults
    assert 'xigua_ai_read_reply' in HEADER
    assert 'xigua_ai_read_reply(s_ai_reply)' in APP
    assert 'xigua_ai_take_audio_state' in APP
    task = SOURCE.split('static void ai_task', 1)[1]
    assert task.index('xQueueOverwrite(s_results, &result)') < task.index('tts_stream(result.text)')
    inventory = {int(cp, 16) for cp in (ROOT / 'assets/fonts/xigua_font_full20.codepoints.txt').read_text().splitlines()}
    labels = '暂停朗读继续朗读开始朗读从头重读继续查看再讲一个正在朗读已暂停朗读朗读失败可重试已停止朗读朗读完毕朗读未启动请重试朗读操作未完成'
    assert set(map(ord, labels)) <= inventory
    care_labels = '照护交接提醒到了确认修改尚未修改原记录原新时间待校准暂不触发编号已完成分钟后再提醒尚未取得云端数据本地仅保留最近条未记录不代表未发生给下一位照护者的留言朗读回复'
    assert set(map(ord, care_labels)) <= inventory
    assert 's_xigua_font.fallback = &xigua_font_full20' in APP
    assert 's_xigua_font20.fallback = &xigua_font_full20' in APP
    assert 'xigua_ai_request_story' in HEADER
    assert 'xigua_ai_request_story()' in APP
    assert '"讲故事"' in APP
    print("Xigua story-mode TTS contract: PASS")


if __name__ == "__main__":
    main()
