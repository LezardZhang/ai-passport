#!/usr/bin/env python3
"""Execute the real cloud WAV stream against deterministic HTTP/codec doubles."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'main/xigua_ai.c').read_text()
claim=source.split('static bool claim_voice(',1)[1].split('static bool claim_backend(',1)[0]
backend_claim='static bool claim_backend('+source.split('static bool claim_backend(',1)[1].split('static volatile bool s_speech_cancelled',1)[0]
stream=source.split('typedef struct { char range[96]; } backend_headers_t;',1)[1].split('static void reminder_tone(',1)[0]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "xigua_wav.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_INVALID_RESPONSE 4
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...) ((void)0)
typedef enum { XIGUA_AI_VOICE_IDLE, XIGUA_AI_VOICE_RECORDING, XIGUA_AI_VOICE_THINKING, XIGUA_AI_VOICE_SPEAKING } xigua_ai_voice_phase_t;
static xigua_ai_voice_phase_t s_voice_phase;
static bool s_backend_active;static uint32_t s_backend_generation;
static volatile bool s_backend_owned,s_backend_paused,s_backend_pause,s_backend_cancel,s_backend_handoff;
static char *s_backend_session;
static uint32_t s_backend_total,s_backend_offset;
static char s_backend_original_id[64],s_backend_resume_id[64],s_backend_pause_id[64];
typedef struct { char range[96]; } backend_headers_t;
typedef struct { char *valuestring; } cJSON;
static cJSON root,url={"http://cloud/media/test.wav"},token={"test-token"};
static cJSON *cJSON_Parse(const char *s) { (void)s; return &root; }
static cJSON *cJSON_GetObjectItemCaseSensitive(cJSON *r,const char *key) { (void)r; return !strcmp(key,"url")?&url:&token; }
static bool cJSON_IsString(cJSON *v) { return v!=NULL; }
static void cJSON_Delete(cJSON *v) { (void)v; }
static const char *esp_err_to_name(int e) { return e?"failure":"ESP_OK"; }
static char ack_ids[30][64],ack_states[30][20]; static unsigned acks;
static void xigua_backend_report_playback(const char *id,const char *status,const char *error) {
 (void)error;if (!id[0])return;assert(acks<30);
 snprintf(ack_ids[acks],64,"%s",id);snprintf(ack_states[acks++],20,"%s",status);
}
#define HTTP_EVENT_ON_HEADER 1
typedef struct { void *user_data; int event_id; char *header_key,*header_value; } esp_http_client_event_t;
typedef struct { const char *url; int timeout_ms,buffer_size; void *crt_bundle_attach; bool disable_auto_redirect;
 esp_err_t (*event_handler)(esp_http_client_event_t *); void *user_data; } esp_http_client_config_t;
static void *esp_crt_bundle_attach;
static uint8_t wav[10044],output[10000];
static size_t cursor,output_bytes;static unsigned writes,pause_at,takeover_at,switch_at;static bool hardware,closed,bad_range;
static int http_status;static esp_http_client_config_t config;
typedef void *esp_http_client_handle_t;
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c) {config=*c;cursor=0;http_status=200;closed=false;return &config;}
static void esp_http_client_set_header(esp_http_client_handle_t c,const char *key,const char *value) {
 (void)c;if(!strcmp(key,"Range")) { unsigned pos;assert(sscanf(value,"bytes=%u-",&pos)==1);cursor=pos;http_status=206; }
}
static int esp_http_client_open(esp_http_client_handle_t c,int n) {(void)c;(void)n;return ESP_OK;}
static int esp_http_client_fetch_headers(esp_http_client_handle_t c) {
 (void)c; if(http_status==206) {
  char range[96];snprintf(range,96,"bytes %u-10043/10044",(unsigned)cursor+(bad_range?2:0));
  esp_http_client_event_t e={.user_data=config.user_data,.event_id=HTTP_EVENT_ON_HEADER,.header_key="Content-Range",.header_value=range};config.event_handler(&e);
 }return (int)(sizeof(wav)-cursor);
}
static int esp_http_client_get_status_code(esp_http_client_handle_t c) {(void)c;return http_status;}
static int esp_http_client_read(esp_http_client_handle_t c,char *out,int n) {
 (void)c;if(n>301)n=301;if((size_t)n>sizeof(wav)-cursor)n=(int)(sizeof(wav)-cursor);
 memcpy(out,wav+cursor,(size_t)n);cursor+=(size_t)n;return n;
}
static void esp_http_client_close(esp_http_client_handle_t c) {(void)c;closed=true;}
static void esp_http_client_cleanup(esp_http_client_handle_t c) {(void)c;assert(closed);}
static int xigua_audio_output_prepare(unsigned hz,unsigned bits,unsigned ch) {assert(hz==12000&&bits==16&&ch==1);assert(!hardware);hardware=true;return ESP_OK;}
static void bsp_audio_set_volume(int v) {(void)v;}
static int bsp_audio_sleep(void) {hardware=false;return ESP_OK;}
static bool claim_voice(xigua_ai_voice_phase_t phase);
static bool ble_active;
static bool xigua_ble_active(void) {return ble_active;}
static bool claim_backend(uint32_t *generation);
static int bsp_audio_write(const void *pcm,size_t n) {
 assert(hardware);assert(output_bytes+n<=sizeof(output));memcpy(output+output_bytes,pcm,n);output_bytes+=n;
 if(++writes==pause_at)s_backend_pause=true;
 if(writes==takeover_at)assert(claim_voice(XIGUA_AI_VOICE_RECORDING));
 if(writes==switch_at) { uint32_t generation;assert(claim_backend(&generation));assert(generation==s_backend_generation); }
 return ESP_OK;
}
'''
main=r'''
static void begin(void) {
 backend_dispose(NULL);s_backend_session=malloc(3);assert(s_backend_session);memcpy(s_backend_session,"{}",3);
 strcpy(s_backend_original_id,"original");strcpy(s_backend_pause_id,"pause");
 s_backend_owned=true;s_backend_cancel=s_backend_pause=s_backend_paused=s_backend_handoff=false;
 s_voice_phase=XIGUA_AI_VOICE_SPEAKING;output_bytes=writes=acks=0;pause_at=2;takeover_at=switch_at=0;bad_range=false;
}
int main(void) {
 memcpy(wav,"RIFF",4);memcpy(wav+8,"WAVEfmt ",8);memcpy(wav+36,"data",4);
 uint32_t n=10036;memcpy(wav+4,&n,4);n=16;memcpy(wav+16,&n,4);wav[20]=1;wav[22]=1;
 n=12000;memcpy(wav+24,&n,4);n=24000;memcpy(wav+28,&n,4);wav[32]=2;wav[34]=16;n=10000;memcpy(wav+40,&n,4);
 for(size_t i=44;i<sizeof(wav);++i)wav[i]=(uint8_t)(i*37);
 begin();assert(backend_wav()==ESP_OK);assert(s_backend_paused&&s_backend_session&&s_backend_offset==4096);
 assert(!hardware&&closed);assert(acks==3&&!strcmp(ack_states[1],"paused")&&!strcmp(ack_states[2],"paused"));
 pause_at=0;s_backend_pause=false;s_backend_paused=false;strcpy(s_backend_resume_id,"resume");
 assert(backend_wav()==ESP_OK);assert(!hardware&&!s_backend_session&&!s_backend_owned);
 assert(output_bytes==10000&&!memcmp(output,wav+44,10000));assert(!strcmp(ack_states[acks-1],"complete"));
 begin();assert(backend_wav()==ESP_OK);s_backend_pause=false;s_backend_paused=false;bad_range=true;
 assert(backend_wav()==ESP_ERR_INVALID_RESPONSE);assert(output_bytes==4096&&!s_backend_session&&!hardware);
 begin();assert(backend_wav()==ESP_OK);assert(claim_voice(XIGUA_AI_VOICE_RECORDING));
 assert(s_backend_cancel&&s_backend_handoff&&s_voice_phase==XIGUA_AI_VOICE_RECORDING);backend_dispose("stopped");assert(!s_backend_owned);
 begin();pause_at=0;takeover_at=2;assert(backend_wav()==ESP_OK);
 assert(s_voice_phase==XIGUA_AI_VOICE_RECORDING&&s_backend_handoff&&!s_backend_session&&!hardware);
 assert(output_bytes==4096&&!strcmp(ack_states[acks-1],"stopped"));
 begin();pause_at=0;s_backend_active=true;switch_at=2;uint32_t generation;
 assert(backend_wav()==ESP_OK);assert(s_backend_cancel&&s_backend_handoff);
 assert(output_bytes==4096&&!hardware&&!s_backend_session);s_backend_active=false;s_backend_handoff=false;
 begin();assert(backend_wav()==ESP_OK);s_voice_phase=XIGUA_AI_VOICE_IDLE;
 assert(claim_backend(&generation));assert(!s_backend_handoff&&s_voice_phase==XIGUA_AI_VOICE_SPEAKING);
 backend_dispose("stopped");assert(!s_backend_owned&&!hardware);
 ble_active=true;s_voice_phase=XIGUA_AI_VOICE_IDLE;
 assert(!claim_voice(XIGUA_AI_VOICE_RECORDING));assert(s_voice_phase==XIGUA_AI_VOICE_IDLE);
 ble_active=false;assert(claim_voice(XIGUA_AI_VOICE_RECORDING));
 puts("Actual cloud stream: pause releases HTTP/codec, exact Range resume, range rejection and recording handoff: PASS");
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(prefix+'static bool claim_voice('+claim+backend_claim+stream+main)
 subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'main'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
