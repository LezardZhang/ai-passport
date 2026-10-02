#!/usr/bin/env python3
"""Run the actual TTS transaction with mutually exclusive TLS/PCM resources.

Catches early audio/reader allocation, failed downloads becoming playable,
and resource leaks across errors and cached retries. HTTP, hardware and the
request JSON builder are boundary doubles; SSE, ADPCM, cache and workers are real.
"""
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
#include "xigua_tts_stream.h"
typedef int esp_err_t;
typedef int portMUX_TYPE;
typedef struct { size_t size; } esp_partition_t;
typedef enum { XIGUA_AI_AUDIO_BUFFERING, XIGUA_AI_AUDIO_PLAYING, XIGUA_AI_AUDIO_PAUSED } xigua_ai_audio_state_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_INVALID_RESPONSE 3
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_HTTP_CONNECT 5
#define ESP_ERR_INVALID_STATE 6
#define ESP_ERR_NOT_FOUND 7
#define ESP_ERR_TIMEOUT 8
#define ESP_ERR_HTTP_EAGAIN 9
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_DMA 1
#define XIGUA_AI_TTS_BITS 16
#define XIGUA_AI_TTS_CHANNELS 1
#define XIGUA_AI_TTS_MODEL "test-tts"
#define XIGUA_AI_BASE_URL "https://example.test/v1"
#define XIGUA_AI_API_KEY "test-only"
#define HTTP_METHOD_POST 1
#define esp_crt_bundle_attach NULL
#define ESP_LOGI(tag,...) do { if(0) printf(__VA_ARGS__); } while(0)
#define ESP_LOGW(tag,...) do { if(0) printf(__VA_ARGS__); } while(0)
#define ESP_LOGE(tag,...) do { if(0) printf(__VA_ARGS__); } while(0)
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...) ((void)0)
#define portMUX_INITIALIZER_UNLOCKED 0
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
typedef struct {
    const char *url; int method, timeout_ms, buffer_size, buffer_size_tx;
    bool keep_alive_enable; void *crt_bundle_attach;
} esp_http_client_config_t;
typedef struct { esp_http_client_config_t config; } client_t;
typedef client_t *esp_http_client_handle_t;
static size_t live_bytes, peak_bytes, allocations, fail_allocation;
static bool tls_open, audio_ready;
static bool network_locked, fail_lock;
typedef union { size_t size; max_align_t align; } header_t;
static void *tracked_malloc(size_t n) {
    if(++allocations==fail_allocation || (tls_open && live_bytes+n>20000)) return NULL;
    header_t *h=malloc(sizeof(*h)+n); assert(h); h->size=n;
    live_bytes+=n; if(live_bytes>peak_bytes) peak_bytes=live_bytes;
    return h+1;
}
static void tracked_free(void *p) {
    if(!p) return;
    header_t *h=(header_t *)p-1; assert(live_bytes>=h->size);
    live_bytes-=h->size; free(h);
}
static void *tracked_calloc(size_t n,size_t size) {
    void *p=tracked_malloc(n*size); if(p) memset(p,0,n*size); return p;
}
#define malloc tracked_malloc
#define calloc tracked_calloc
#define free tracked_free
'''
STUBS = r'''
static bool s_voice_stop;
static int s_audio_states;
static const esp_partition_t partition={2*1024*1024};
static uint8_t flash[2*1024*1024];
static size_t played, response_offset, upload_bytes;
static unsigned opens, cleanups, now_ms;
static bool fail_open, fail_headers, fail_upload, fail_read, fail_flash;
static bool fail_audio, fail_pcm, fail_task, fail_release, cancel_read, malformed;
static int response_status;
static void *audio_dma;
static client_t client;
static const char response[]="data: {\"choices\":[{\"delta\":{\"audio\":{\"data\":\"AAAAAAAAAAA=\"}}}]}\n\ndata: [DONE]\n\n";
static const esp_partition_t *voice_partition(void) { return &partition; }
static bool xigua_network_take(unsigned ms) { assert(ms==10000 && !network_locked); if(fail_lock) return false; network_locked=true;return true; }
static void xigua_network_give(void) { assert(network_locked); network_locked=false; }
static unsigned uxTaskGetStackHighWaterMark(void *arg) { (void)arg; return 2048; }
static void vTaskDelay(unsigned ms) { now_ms+=ms; assert(now_ms<5000); }
static void vTaskDelete(void *arg) { (void)arg; }
static int64_t esp_timer_get_time(void) { return (int64_t)now_ms*1000; }
static void xQueueOverwrite(int q,const xigua_ai_audio_state_t *state) { (void)q;(void)state; }
static size_t heap_caps_get_free_size(int cap) { (void)cap; return 64000-live_bytes; }
static size_t heap_caps_get_largest_free_block(int cap) { return heap_caps_get_free_size(cap); }
static const char *esp_err_to_name(esp_err_t err) { (void)err; return "test"; }
static void log_http_connect_diagnostics(client_t *c,const char *where,esp_err_t err) { (void)c;(void)where;(void)err; }
static esp_err_t esp_partition_erase_range(const esp_partition_t *p,size_t off,size_t n) {
    assert(p==&partition && off+n<=p->size);
    memset(flash+off,0xff,n); return ESP_OK;
}
static esp_err_t esp_partition_write(const esp_partition_t *p,size_t off,const void *data,size_t n) {
    if(fail_flash) return ESP_FAIL;
    assert(p==&partition && off+n<=p->size);
    const uint8_t *src=data;
    for(size_t i=0;i<n;i++) { assert((flash[off+i]&src[i])==src[i]); flash[off+i]=src[i]; }
    return ESP_OK;
}
static esp_err_t esp_partition_read(const esp_partition_t *p,size_t off,void *data,size_t n) {
    assert(p==&partition && off+n<=p->size); memcpy(data,flash+off,n); return ESP_OK;
}
esp_err_t bsp_audio_release(void) {
    if(fail_release) return ESP_FAIL;
    free(audio_dma); audio_dma=NULL; audio_ready=false; return ESP_OK;
}
static esp_err_t bsp_audio_sleep(void) { audio_ready=false; return ESP_OK; }
static esp_err_t bsp_audio_wake(void) { audio_ready=true; return ESP_OK; }
static void bsp_audio_set_volume(unsigned volume) { assert(volume==80); }
static esp_err_t xigua_audio_output_prepare(unsigned hz,unsigned bits,unsigned channels) {
    assert(hz==12000 && bits==16 && channels==1);
    if(fail_audio || tls_open) return ESP_ERR_NO_MEM;
    if(!audio_dma) audio_dma=malloc(6000);
    if(!audio_dma) return ESP_ERR_NO_MEM;
    audio_ready=true; return ESP_OK;
}
static esp_err_t bsp_audio_write(const void *pcm,size_t n) {
    assert(audio_ready && !tls_open);
    if(fail_pcm) return ESP_FAIL;
    for(size_t i=0;i<n;i++) assert(((const uint8_t *)pcm)[i]==0);
    played+=n; return ESP_OK;
}
static int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle) {
    (void)name;(void)priority;(void)handle;
    assert(!tls_open && ((xigua_tts_playback_t *)arg)->producer_done);
    if(fail_task) return 0;
    void *memory=malloc(stack); if(!memory) return 0;
    task(arg); free(memory); return pdPASS;
}
static int mbedtls_sha256(const unsigned char *data,size_t n,unsigned char *digest,int mode) {
    (void)mode; unsigned sum=0; for(size_t i=0;i<n;i++) sum=sum*31+data[i];
    for(unsigned i=0;i<32;i++) digest[i]=(uint8_t)(sum>>(8*(i%4)));
    return 0;
}
/* Only the request builder is replaced. Preserve its owned payload lifetime
 * and real story-size pressure; JSON format is covered by the TTS contract. */
typedef struct { int unused; } cJSON;
static cJSON node;
static const char *story;
static cJSON *cJSON_CreateObject(void) { return &node; }
static cJSON *cJSON_CreateArray(void) { return &node; }
static void cJSON_AddStringToObject(cJSON *p,const char *key,const char *value) { (void)p; if(!strcmp(key,"content") && strncmp(value,"请",3)) story=value; }
static void cJSON_AddBoolToObject(cJSON *p,const char *key,bool v) { (void)p;(void)key;(void)v; }
static void cJSON_AddItemToArray(cJSON *p,cJSON *item) { (void)p;(void)item; }
static void cJSON_AddItemToObject(cJSON *p,const char *key,cJSON *item) { (void)p;(void)key;(void)item; }
static void cJSON_Delete(cJSON *p) { (void)p; }
static char *cJSON_PrintUnformatted(cJSON *p) {
    (void)p; size_t n=strlen(story)+256; char *out=malloc(n);
    if(out) { memset(out,'x',n-1);out[n-1]=0; } return out;
}
static client_t *esp_http_client_init(const esp_http_client_config_t *config) { client.config=*config; return &client; }
static void esp_http_client_set_header(client_t *c,const char *key,const char *value) { (void)c;(void)key;(void)value; }
static esp_err_t esp_http_client_open(client_t *c,int bytes) {
    (void)c; assert(bytes>0); ++opens;
    /* RSA verification needs the audio DMA and reader state to be absent. */
    if(!network_locked || fail_open || audio_ready || live_bytes>5200) return ESP_ERR_HTTP_CONNECT;
    tls_open=true; return ESP_OK;
}
static int esp_http_client_write(client_t *c,const char *data,int n) {
    (void)c;(void)data; assert(n<=512); if(fail_upload) return -1;
    int count=n>17?17:n; upload_bytes+=(size_t)count; return count;
}
static int esp_http_client_fetch_headers(client_t *c) {
    (void)c; assert(live_bytes==0); return fail_headers?-1:0;
}
static int esp_http_client_get_status_code(client_t *c) { (void)c; return tls_open?response_status:0; }
static void esp_http_client_set_timeout_ms(client_t *c,int ms) { (void)c;assert(ms==1000); }
static int esp_http_client_read(client_t *c,char *out,int n) {
    (void)c; if(fail_read) return -1; if(cancel_read) { s_voice_stop=true; return -1; }
    const char *r=malformed?"data: [DONE]\n\n":response;
    size_t count=strlen(r)-response_offset; if(count>(size_t)n) count=(size_t)n;
    if(count>3) count=3; memcpy(out,r+response_offset,count); response_offset+=count;
    return (int)count;
}
static void esp_http_client_close(client_t *c) { (void)c; }
static void esp_http_client_cleanup(client_t *c) { (void)c; tls_open=false;++cleanups; }
'''
CHECKS = r'''
static void setup(void) {
    assert(live_bytes==0 && !tls_open && !audio_dma && !s_tts_playback && !network_locked);
    peak_bytes=allocations=0;fail_allocation=SIZE_MAX;
    s_tts_cached_valid=s_voice_stop=s_tts_paused=s_tts_restart=false;
    fail_open=fail_headers=fail_upload=fail_read=fail_flash=false;
    fail_audio=fail_pcm=fail_task=fail_release=cancel_read=malformed=false;
    fail_lock=false;
    played=response_offset=upload_bytes=0;opens=cleanups=now_ms=0;
    response_status=200;
}
static void released(void) { assert(live_bytes==0 && !tls_open && !audio_dma && !s_tts_playback && !network_locked); }
int main(void) {
    char long_story[4096]; memset(long_story,'s',4095);long_story[4095]=0;
    setup();
    assert(tts_stream(long_story)==ESP_OK); released();
    assert(played==4 && s_tts_cached_valid && opens==1 && cleanups==1);
    /* Replay must use the verified Flash cache, even with network unavailable. */
    played=0;fail_open=true;
    assert(tts_stream(long_story)==ESP_OK);released();assert(played==4 && opens==1);
    /* Pre-existing idle PCM resources must be retired before a fresh request. */
    setup();audio_dma=malloc(6000);audio_ready=true;
    assert(tts_stream(long_story)==ESP_OK);released();assert(played==4);
    for(size_t i=1;i<=6;i++) {
        setup();fail_allocation=i;
        assert(tts_stream(long_story)==ESP_ERR_NO_MEM);released();assert(played==0);
    }
    setup();fail_open=true;
    assert(tts_stream(long_story)==ESP_ERR_HTTP_CONNECT);released();assert(!s_tts_cached_valid && played==0);
    setup();fail_lock=true;
    assert(tts_stream(long_story)==ESP_ERR_TIMEOUT);released();assert(opens==0 && played==0);
    setup();fail_headers=true;
    assert(tts_stream(long_story)!=ESP_OK);released();assert(!s_tts_cached_valid && played==0);
    setup();fail_upload=true;
    assert(tts_stream(long_story)!=ESP_OK);released();assert(!s_tts_cached_valid && played==0);
    setup();fail_read=true;
    assert(tts_stream(long_story)!=ESP_OK);released();assert(!s_tts_cached_valid && played==0);
    setup();malformed=true;
    assert(tts_stream(long_story)==ESP_ERR_INVALID_RESPONSE);released();assert(!s_tts_cached_valid && played==0);
    setup();response_status=403;
    assert(tts_stream(long_story)!=ESP_OK);released();assert(!s_tts_cached_valid && played==0);
    setup();fail_flash=true;
    assert(tts_stream(long_story)!=ESP_OK);released();assert(!s_tts_cached_valid && played==0);
    setup();cancel_read=true;
    assert(tts_stream(long_story)==ESP_ERR_INVALID_STATE);released();assert(!s_tts_cached_valid && played==0);
    setup();fail_audio=true;
    assert(tts_stream(long_story)==ESP_ERR_NO_MEM);released();assert(s_tts_cached_valid && played==0);
    setup();fail_task=true;
    assert(tts_stream(long_story)==ESP_ERR_NO_MEM);released();assert(s_tts_cached_valid && played==0);
    setup();fail_pcm=true;
    assert(tts_stream(long_story)==ESP_FAIL);released();assert(s_tts_cached_valid && played==0);
    setup();fail_release=true;
    assert(tts_stream(long_story)==ESP_FAIL && opens==0);fail_release=false;released();
    puts("Actual TTS: TLS/PCM separation, full story, cached replay, cancellation and error cleanup: PASS");
}
'''


def main():
    source = (ROOT / 'main/xigua_ai.c').read_text()
    struct = re.search(r'typedef struct \{\n    const esp_partition_t \*partition;.*?\} xigua_tts_playback_t;', source, re.S).group(0)
    globals_ = source.split('static xigua_tts_playback_t *s_tts_playback;', 1)[1].split('static bool tts_drain_dma', 1)[0]
    names = ('write_http_all', 'tts_drain_dma', 'tts_playback_task', 'tts_enqueue_block',
             'tts_queue_pcm', 'tts_playback_destroy', 'tts_play_cached', 'tts_stream')
    functions = [re.search(rf'^static [^\n]+\b{name}\([^;]*?\)\n\{{.*?^\}}', source, re.M | re.S).group(0) for name in names]
    with tempfile.TemporaryDirectory(prefix='xigua-tts-memory-') as temp:
        c = Path(temp) / 'tts.c'
        exe = Path(temp) / 'tts'
        c.write_text(PREFIX + struct + '\nstatic xigua_tts_playback_t *s_tts_playback;\n' + globals_ + STUBS + '\n'.join(functions) + CHECKS)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-Imain', str(c),
                        'main/xigua_adpcm.c', 'main/xigua_tts_downsample.c', 'main/xigua_tts_stream.c', '-o', str(exe)], cwd=ROOT, check=True)
        subprocess.run([str(exe)], check=True, timeout=10)


if __name__ == '__main__':
    main()
