#!/usr/bin/env python3
"""Exercise real ASR/chat requests under a constrained TLS heap budget."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(rf"^static [^\n]+\b{name}\([^;]*?\)\n\{{.*?^\}}",
                      source, re.MULTILINE | re.DOTALL)
    assert match, name
    return match.group(0)


STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xigua_text.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_INVALID_RESPONSE 4
#define ESP_ERR_INVALID_CRC 5
#define ESP_ERR_HTTP_CONNECT 6
#define ESP_ERR_TIMEOUT 7
#define ESP_ERR_HTTP_WRITE_DATA 8
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_DMA 1
#define HTTP_METHOD_POST 1
#define HTTP_EVENT_ON_DATA 2
#define ESP_LOGI(tag,...) do { if(0) printf(__VA_ARGS__); } while(0)
#define ESP_LOGW(tag,...) do { if(0) printf(__VA_ARGS__); } while(0)
#define XIGUA_AI_BASE_URL "https://example.test/v1"
#define XIGUA_AI_API_KEY "test-only"
#define esp_crt_bundle_attach NULL
typedef struct { size_t size; } esp_partition_t;
typedef struct { int event_id, data_len; void *user_data; char *data; } esp_http_client_event_t;
typedef struct {
    const char *url; int method, timeout_ms, buffer_size, buffer_size_tx;
    bool keep_alive_enable; void *crt_bundle_attach;
    esp_err_t (*event_handler)(esp_http_client_event_t *); void *user_data;
} esp_http_client_config_t;
typedef struct { esp_http_client_config_t config; const char *post; int post_len; } client_t;
typedef client_t *esp_http_client_handle_t;
static client_t client;
static const esp_partition_t partition = { 2*1024*1024 };
static const esp_partition_t *voice_partition(void) { return &partition; }
static size_t live_bytes, peak_bytes, alloc_calls, fail_alloc_at, response_offset;
static bool opened, fail_open, fail_write, fail_read, fail_partition, oversized, zero_write;
static size_t upload_bytes;
static size_t max_write_bytes;
static bool constrained_write;
static bool body_in_headers, fail_headers, incomplete_body;
static size_t cached_bytes;
static size_t handshake_budget;
static unsigned close_count, cleanup_count;
typedef union { size_t size; max_align_t alignment; } alloc_header;
static void *tracked_malloc(size_t n) {
    if (++alloc_calls == fail_alloc_at) return NULL;
    /* TLS records/certificates remain resident after a successful handshake. */
    if (opened && live_bytes+n>2048) return NULL;
    alloc_header *h = malloc(sizeof(*h) + n);
    assert(h); h->size = n; live_bytes += n;
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
    return h + 1;
}
static void tracked_free(void *p) {
    if (!p) return;
    alloc_header *h = (alloc_header *)p - 1;
    assert(live_bytes >= h->size); live_bytes -= h->size; free(h);
}
static void *tracked_calloc(size_t n, size_t size) {
    void *p = tracked_malloc(n*size); if (p) memset(p, 0, n*size); return p;
}
static void *tracked_realloc(void *p, size_t n) {
    void *next = tracked_malloc(n);
    if (!next) return NULL;
    if (p) { alloc_header *h = (alloc_header *)p-1; memcpy(next,p,h->size<n?h->size:n); }
    tracked_free(p); return next;
}
#define malloc tracked_malloc
#define calloc tracked_calloc
#define realloc tracked_realloc
#define free tracked_free
static esp_err_t esp_partition_read(const esp_partition_t *p,size_t offset,void *data,size_t n) {
    assert(p==&partition && offset+n<=p->size);
    if (fail_partition) return ESP_FAIL;
    memset(data,'a',n); return ESP_OK;
}
static int mbedtls_base64_encode(unsigned char *out,size_t cap,size_t *length,
                                 const unsigned char *in,size_t n) {
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    *length=4*((n+2)/3); if(cap<=*length) return -1;
    size_t at=0;
    for(size_t i=0;i<n;i+=3) {
        unsigned v=(unsigned)in[i]<<16;
        if(i+1<n) v|=(unsigned)in[i+1]<<8;
        if(i+2<n) v|=in[i+2];
        out[at++]=alphabet[v>>18]; out[at++]=alphabet[(v>>12)&63];
        out[at++]=i+1<n?alphabet[(v>>6)&63]:'=';
        out[at++]=i+2<n?alphabet[v&63]:'=';
    }
    out[at]=0; return 0;
}
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c) {
    client.config=*c; client.post=NULL; client.post_len=0; return &client;
}
static void esp_http_client_set_header(client_t *c,const char *k,const char *v) { (void)c;(void)k;(void)v; }
static void esp_http_client_set_post_field(client_t *c,const char *p,int n) { assert(p && n>0); c->post=p; c->post_len=n; }
static esp_err_t esp_http_client_open(client_t *c,int n) {
    (void)c;(void)n;
    /* Certificate verification needs the heap before any upload/reply staging. */
    if(fail_open || live_bytes>handshake_budget) return ESP_ERR_HTTP_CONNECT;
    opened=true; return ESP_OK;
}
static int esp_http_client_write(client_t *c,const char *p,int n) {
    (void)c; assert(opened && p && n>0);
    if ((size_t)n>max_write_bytes) max_write_bytes=(size_t)n;
    /* With a resident TLS client, large plaintext writes exhaust TCP headroom. */
    if(constrained_write && n>512) return 0;
    if(zero_write) return 0;
    if(fail_write) return -1;
    int count=n>17?17:n; upload_bytes+=(size_t)count; return count;
}
static const char reply[]="{\"choices\":[{\"message\":{\"content\":\"hello\"}}]}";
static int esp_http_client_fetch_headers(client_t *c) {
    if(fail_headers) return -1;
    if(oversized && c->config.event_handler) {
        char huge[20000]={0};
        esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_DATA,.data=huge,
            .data_len=sizeof(huge),.user_data=c->config.user_data};
        (void)c->config.event_handler(&event);
    }
    if(body_in_headers && c->config.event_handler) {
        cached_bytes=13;
        esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_DATA,.data=(char *)reply,
            .data_len=(int)cached_bytes,.user_data=c->config.user_data};
        /* The parser emits these bytes once, before read returns its cache. */
        (void)c->config.event_handler(&event);
    }
    return sizeof(reply)-1;
}
static int esp_http_client_read(client_t *c,char *out,int n) {
    (void)c; if(fail_read) return -1;
    size_t count=strlen(reply)-response_offset;
    if(count>(size_t)n) count=(size_t)n;
    if(count>7) count=7;
    if(incomplete_body && response_offset>=7) return 0;
    if(cached_bytes && count>cached_bytes) count=cached_bytes;
    memcpy(out,reply+response_offset,count); response_offset+=count;
    if(cached_bytes) cached_bytes-=count;
    else if(c->config.event_handler && count) {
        esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_DATA,.data=out,
            .data_len=(int)count,.user_data=c->config.user_data};
        (void)c->config.event_handler(&event);
    }
    return (int)count;
}
static bool esp_http_client_is_complete_data_received(client_t *c) { (void)c; return response_offset==strlen(reply); }
static int esp_http_client_get_status_code(client_t *c) { (void)c; return opened?200:0; }
static esp_err_t esp_http_client_cleanup(client_t *c) { (void)c; opened=false; cleanup_count++; return ESP_OK; }
static esp_err_t esp_http_client_close(client_t *c) { (void)c; close_count++; return ESP_OK; }
static esp_err_t esp_http_client_perform(client_t *c) {
    esp_err_t err=esp_http_client_open(c,0); if(err) return err;
    int left=c->post_len, offset=0;
    while(left>0) {
        int n=esp_http_client_write(c,c->post+offset,left);
        if(n<=0) return ESP_ERR_HTTP_WRITE_DATA;
        left-=n;offset+=n;
    }
    esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_DATA,.user_data=c->config.user_data};
    char huge[20000]={0};
    if(oversized) { event.data=huge;event.data_len=sizeof(huge); }
    else { event.data=(char *)reply;event.data_len=sizeof(reply)-1; }
    /* IDF does not propagate the callback's return value from ON_DATA. */
    (void)c->config.event_handler(&event);
    return ESP_OK;
}
static size_t heap_caps_get_free_size(int c) { (void)c; return 52000-live_bytes; }
static size_t heap_caps_get_largest_free_block(int c) { return heap_caps_get_free_size(c); }
static const char *esp_err_to_name(esp_err_t e) { (void)e; return "test"; }
static void log_http_connect_diagnostics(client_t *c,const char *s,esp_err_t e) { (void)c;(void)s;(void)e; }
/* Parsing is outside this test's HTTP/memory boundary; validate complete input. */
typedef struct { char *valuestring; } cJSON;
static cJSON json={.valuestring="hello"};
static cJSON *cJSON_ParseWithLength(const char *s,size_t n) {
    assert(n==strlen(reply) && !memcmp(s,reply,n)); return &json;
}
static cJSON *cJSON_Parse(const char *s) { return cJSON_ParseWithLength(s,strlen(s)); }
static cJSON *cJSON_GetObjectItem(cJSON *p,const char *key) { return p && strcmp(key,"finish_reason")?&json:NULL; }
static cJSON *cJSON_GetArrayItem(cJSON *p,int i) { (void)i; return p; }
static bool cJSON_IsArray(cJSON *p) { return p!=NULL; }
static bool cJSON_IsObject(cJSON *p) { return p!=NULL; }
static bool cJSON_IsString(cJSON *p) { return p!=NULL; }
static void cJSON_Delete(cJSON *p) { (void)p; }
static bool s_response_truncated;
'''

CHECKS = r'''
static esp_err_t run_post(const char *data,char *text,size_t text_size,size_t capacity) {
    /* Production's cJSON output is an owned heap allocation. */
    handshake_budget=8192;
    char *payload=malloc(strlen(data)+1);
    if(!payload) return ESP_ERR_NO_MEM;
    strcpy(payload,data);
    esp_err_t err=post_json_internal(&payload,text,text_size,capacity);
    free(payload);return err;
}
static void setup(void) {
    assert(live_bytes==0); alloc_calls=peak_bytes=upload_bytes=response_offset=0;
    fail_alloc_at=SIZE_MAX; opened=fail_open=fail_write=fail_read=fail_partition=oversized=false;
    close_count=cleanup_count=0; zero_write=false; constrained_write=false;max_write_bytes=0;
    body_in_headers=fail_headers=incomplete_body=false;cached_bytes=0;handshake_budget=4096;
}
int main(void) {
    char text[100];
    setup();
    assert(asr_stream_internal(44,text,sizeof(text))==ESP_OK);
    assert(!strcmp(text,"hello") && live_bytes==0 && cleanup_count==1);
    assert(upload_bytes==strlen(s_asr_prefix)+60+strlen(s_asr_suffix));
    setup();
    assert(asr_stream_internal(6146,text,sizeof(text))==ESP_OK && live_bytes==0);
    assert(upload_bytes==strlen(s_asr_prefix)+8196+strlen(s_asr_suffix));
    /* Upload scratch must be retired before response storage. */
    assert(peak_bytes<9000);
    for(size_t i=1;i<=3;i++) {
        setup(); fail_alloc_at=i;
        assert(asr_stream_internal(44,text,sizeof(text))==ESP_ERR_NO_MEM);
        assert(live_bytes==0 && cleanup_count==1);
    }
    setup();fail_open=true;
    assert(asr_stream_internal(44,text,sizeof(text))==ESP_ERR_HTTP_CONNECT && live_bytes==0);
    setup();fail_write=true;
    assert(asr_stream_internal(44,text,sizeof(text))==ESP_FAIL && live_bytes==0);
    setup();zero_write=true;
    assert(asr_stream_internal(44,text,sizeof(text))==ESP_ERR_TIMEOUT && live_bytes==0);
    setup();fail_partition=true;
    assert(asr_stream_internal(44,text,sizeof(text))==ESP_FAIL && live_bytes==0);
    setup();fail_read=true;
    assert(asr_stream_internal(44,text,sizeof(text))==ESP_FAIL && live_bytes==0);
    setup();
    assert(run_post("{}",text,sizeof(text),16384)==ESP_OK);
    assert(!strcmp(text,"hello") && live_bytes==0 && peak_bytes<1024);
    setup();fail_alloc_at=2;
    assert(run_post("{}",text,sizeof(text),16384)==ESP_ERR_NO_MEM && live_bytes==0);
    setup();oversized=true;
    assert(run_post("{}",text,sizeof(text),16384)==ESP_ERR_NO_MEM && live_bytes==0);
    setup();constrained_write=true;
    char large_payload[5774]; memset(large_payload,'x',sizeof(large_payload)-1);large_payload[5773]=0;
    assert(run_post(large_payload,text,sizeof(text),16384)==ESP_OK);
    assert(upload_bytes==5773 && max_write_bytes<=512 && cleanup_count==1 && live_bytes==0);
    setup();fail_write=true;
    assert(run_post("{}",text,sizeof(text),16384)==ESP_FAIL && cleanup_count==1 && live_bytes==0);
    setup();zero_write=true;
    assert(run_post("{}",text,sizeof(text),16384)==ESP_ERR_TIMEOUT && cleanup_count==1 && live_bytes==0);
    setup();fail_read=true;
    assert(run_post("{}",text,sizeof(text),16384)==ESP_FAIL && cleanup_count==1 && live_bytes==0);
    setup();fail_open=true;
    assert(run_post("{}",text,sizeof(text),16384)==ESP_ERR_HTTP_CONNECT && cleanup_count==1 && live_bytes==0);
    setup();body_in_headers=true;
    assert(run_post("{}",text,sizeof(text),16384)==ESP_OK && !strcmp(text,"hello") && live_bytes==0);
    setup();fail_headers=true;
    assert(run_post("{}",text,sizeof(text),16384)==ESP_FAIL && cleanup_count==1 && live_bytes==0);
    setup();incomplete_body=true;
    assert(run_post("{}",text,sizeof(text),16384)==ESP_ERR_INVALID_RESPONSE && cleanup_count==1 && live_bytes==0);
    setup();handshake_budget=8192;
    char *owned_request=malloc(5783); assert(owned_request);
    memset(owned_request,'x',5782);owned_request[5782]=0;
    /* The sent request must retire before the constrained receive phase. */
    assert(post_json_internal(&owned_request,text,sizeof(text),16384)==ESP_OK);
    assert(owned_request==NULL && live_bytes==0 && cleanup_count==1);
    puts("Xigua ASR/chat TLS heap and cleanup tests: PASS");
}
'''


def main():
    source = (ROOT / "main/xigua_ai.c").read_text()
    body = re.search(r"typedef struct \{\n    char \*data;.*?\} xigua_ai_body_t;", source, re.S).group(0)
    defines = re.findall(r"^#define XIGUA_AI_(?:VOICE_FLASH_CHUNK_BYTES|VOICE_B64_CHUNK_BYTES|ASR_BODY_MAX|WAV_HEADER_BYTES) .*", source, re.M)
    defines.append("#define XIGUA_AI_VOICE_MAX_WAV_BYTES 1920044")
    framing = source[source.index("static const char s_asr_prefix"):source.index("static const esp_partition_t *voice_partition")]
    functions = "\n".join(function(source, n) for n in
                          ("http_event", "write_http_all", "post_json_internal", "asr_stream_internal"))
    with tempfile.TemporaryDirectory(prefix="xigua-http-test-") as d:
        path = Path(d)
        (path / "test.c").write_text(STUBS + "\n" + "\n".join(defines) + "\n" + body + framing + functions + CHECKS)
        compiler = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                                  "-I" + str(ROOT / "main"), str(path / "test.c"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)


if __name__ == "__main__":
    main()
