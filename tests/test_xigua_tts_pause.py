#!/usr/bin/env python3
"""Run the actual PCM/cache worker against deterministic Flash and timing doubles."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xigua_tts_cache.h"
#include "xigua_tts_buffer.h"
#include "xigua_tts_downsample.h"
typedef int esp_err_t;
typedef int portMUX_TYPE;
typedef struct { size_t size; } esp_partition_t;
typedef enum { XIGUA_AI_AUDIO_BUFFERING, XIGUA_AI_AUDIO_PLAYING, XIGUA_AI_AUDIO_PAUSED } xigua_ai_audio_state_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_SIZE 1
#define ESP_ERR_INVALID_RESPONSE 2
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
'''
STUBS = r'''
static bool s_voice_stop, s_tts_paused, s_tts_restart;
static int s_audio_states;
static esp_partition_t partition = { 2*1024*1024 };
static uint8_t flash[2*1024*1024];
static uint8_t input[100][1024], expected[100*1024], output[103*1024];
static size_t lengths[100], expected_bytes, output_bytes, sent_blocks;
static unsigned now_ms, pause_at, action, write_calls, read_calls;
static unsigned sleeps, wakes, fail_read_at, fail_pcm_at, fail_flash_at, flash_calls;
static bool pause_sent, resumed, was_paused;
static xigua_tts_playback_t *current;
static bool tts_enqueue_block(xigua_tts_playback_t *, const uint8_t *, size_t);
static void xQueueOverwrite(int queue, const xigua_ai_audio_state_t *state) { (void)queue; (void)state; }
static unsigned uxTaskGetStackHighWaterMark(void *arg) { (void)arg; return 2048; }
static void vTaskDelete(void *arg) { (void)arg; }
static int64_t esp_timer_get_time(void) { return (int64_t)now_ms*1000; }
static esp_err_t esp_partition_write(const esp_partition_t *p, size_t offset, const void *data, size_t bytes) {
    ++flash_calls;
    if (flash_calls == fail_flash_at) return ESP_FAIL;
    assert(offset+bytes <= p->size);
    const uint8_t *src=data;
    for (size_t i=0;i<bytes;++i) { assert((flash[offset+i]&src[i])==src[i]); flash[offset+i]=src[i]; }
    return ESP_OK;
}
static esp_err_t esp_partition_read(const esp_partition_t *p, size_t offset, void *data, size_t bytes) {
    if (++read_calls == fail_read_at) return ESP_FAIL;
    assert(offset+bytes<=p->size);
    memcpy(data,flash+offset,bytes);
    return ESP_OK;
}
static void publish_until(unsigned blocks) {
    while (sent_blocks<blocks) {
        assert(tts_enqueue_block(current,input[sent_blocks],lengths[sent_blocks]));
        ++sent_blocks;
    }
}
static void vTaskDelay(unsigned ms) {
    now_ms+=ms;
    if (s_tts_paused) {
        if (!was_paused) { pause_at=now_ms; was_paused=true; }
        assert(current->cache.read_pcm==3*1024);
        assert(output_bytes==3*1024);
        /* Reception continues while the PCM consumer remains paused. */
        if (now_ms>=pause_at+500 && !current->producer_done) {
            publish_until(100); current->producer_done=true;
        }
        if (now_ms>=pause_at+60000) {
            if (action==3) s_voice_stop=true;
            else {
                s_tts_paused=false; resumed=true;
                if (action==2) s_tts_restart=true;
            }
        }
    }
}
static esp_err_t bsp_audio_sleep(void) { ++sleeps; return ESP_OK; }
static esp_err_t bsp_audio_wake(void) { ++wakes; return ESP_OK; }
static esp_err_t bsp_audio_write(const void *data,size_t bytes) {
    if (++write_calls==fail_pcm_at) return ESP_FAIL;
    assert(output_bytes+bytes<=sizeof(output));
    memcpy(output+output_bytes,data,bytes); output_bytes+=bytes;
    now_ms+=(unsigned)(bytes*1000/(12000*2));
    if (action && !pause_sent && write_calls==3) { s_tts_paused=true; pause_sent=true; }
    return ESP_OK;
}
'''
CHECKS = r'''
static void setup(xigua_tts_playback_t *player,unsigned control) {
    memset(player,0,sizeof(*player)); memset(flash,0xff,sizeof(flash));
    player->partition=&partition; current=player;
    xigua_tts_cache_init(&player->cache,partition.size);
    s_voice_stop=s_tts_paused=s_tts_restart=false;
    pause_sent=resumed=was_paused=false;
    now_ms=pause_at=write_calls=read_calls=sleeps=wakes=0;
    fail_read_at=fail_pcm_at=fail_flash_at=flash_calls=0;
    expected_bytes=output_bytes=sent_blocks=0; action=control;
    xigua_adpcm_state_t reference={0};
    for (unsigned b=0;b<100;++b) {
        lengths[b]=b==99?14:1024;
        for (unsigned i=0;i<lengths[b];i+=2) {
            int16_t v=(int16_t)((b*512+i/2)*379);
            input[b][i]=(uint8_t)(uint16_t)v; input[b][i+1]=(uint8_t)((uint16_t)v>>8);
        }
        xigua_adpcm_block_t packet;
        assert(xigua_adpcm_encode(&reference,input[b],lengths[b],&packet));
        assert(xigua_adpcm_decode(&packet,expected+expected_bytes,sizeof(expected)-expected_bytes));
        expected_bytes+=lengths[b];
    }
}
int main(void) {
    xigua_tts_playback_t player;
    /* A one-minute pause retains the exact next block, including the tail. */
    setup(&player,1); publish_until(75); tts_playback_task(&player);
    assert(player.finished && !player.error && resumed && sleeps==1 && wakes==1);
    assert(output_bytes==expected_bytes && !memcmp(output,expected,expected_bytes));
    assert(player.cache.read_pcm==expected_bytes && player.buffering.underruns==0);
    /* Restart from pause resets only the reader, never the received audio. */
    setup(&player,2); publish_until(75); tts_playback_task(&player);
    assert(player.finished && !player.error && resumed);
    assert(output_bytes==3*1024+expected_bytes);
    assert(!memcmp(output,expected,3*1024));
    assert(!memcmp(output+3*1024,expected,expected_bytes));
    /* Leaving a paused page cancels promptly without a wake/new request. */
    setup(&player,3); publish_until(75); tts_playback_task(&player);
    assert(player.finished && s_voice_stop && wakes==0 && output_bytes==3*1024);
    /* A failed read or PCM write never advances the playback cursor. */
    setup(&player,0); publish_until(100); player.producer_done=true; fail_read_at=4;
    tts_playback_task(&player); assert(player.finished && player.error==ESP_FAIL && player.cache.read_pcm==3*1024);
    setup(&player,0); publish_until(100); player.producer_done=true; fail_pcm_at=4;
    tts_playback_task(&player); assert(player.finished && player.error==ESP_FAIL && player.cache.read_pcm==3*1024);
    /* Failed Flash publication and a full cache do not expose partial data. */
    setup(&player,0); fail_flash_at=1;
    assert(!tts_enqueue_block(&player,input[0],1024) && player.cache.written_blocks==0);
    setup(&player,0); xigua_tts_cache_init(&player.cache,2*sizeof(xigua_adpcm_block_t)+1);
    publish_until(2);
    assert(!tts_enqueue_block(&player,input[2],1024) && player.cache.written_blocks==2);
    /* A completed short clip drains without waiting for three seconds. */
    setup(&player,0); lengths[1]=14; publish_until(2); player.producer_done=true;
    tts_playback_task(&player); assert(player.finished && !player.error && output_bytes==1038);
    puts("Actual TTS worker: pause/continue cursor, background download, restart, cancellation, NOR bounds and failures: PASS");
    return 0;
}
'''

def main():
    source=(ROOT/'main/xigua_ai.c').read_text()
    struct=re.search(r'typedef struct \{\n    const esp_partition_t \*partition;.*?\} xigua_tts_playback_t;',source,re.S).group(0)
    functions=[]
    for name in ('tts_drain_dma','tts_playback_task','tts_enqueue_block'):
        functions.append(re.search(rf'^static [^\n]+\b{name}\([^;]*?\)\n\{{.*?^\}}',source,re.M|re.S).group(0))
    # Keep printf arguments checked without printing private/device data.
    prefix=PREFIX.replace('#define ESP_LOGI(...) ((void)0)', '#define ESP_LOGI(tag, fmt, ...) do { if (0) printf(fmt, ##__VA_ARGS__); } while(0)')
    with tempfile.TemporaryDirectory(prefix='xigua-pause-test-') as temp:
        c=Path(temp)/'pause.c'; exe=Path(temp)/'pause'
        c.write_text(prefix+struct+STUBS+'\n'.join(functions)+CHECKS)
        subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-Imain',str(c),'main/xigua_adpcm.c','-o',str(exe)],cwd=ROOT,check=True)
        subprocess.run([str(exe)],check=True)

if __name__=='__main__':main()
