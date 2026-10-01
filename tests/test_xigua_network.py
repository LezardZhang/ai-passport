"""Run the real transaction wrappers concurrently with a pthread mutex."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def function(source, name):
    match = re.search(r'^static [^\n]+\b' + name + r'\([^;]*?\)\n\{.*?^\}', source, re.M | re.S)
    assert match, name
    return match.group(0)

AI = (ROOT / 'main/xigua_ai.c').read_text()
CLOUD = (ROOT / 'main/xigua_backend.c').read_text()
HARNESS = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include "xigua_network.h"
static int occupied, peak, cloud_calls, model_calls, fail_create;
static void delay(void) { struct timespec t={0,1000000}; nanosleep(&t,NULL); }
void *xSemaphoreCreateMutex(void) {
 if(fail_create) return NULL;
 pthread_mutex_t *p=malloc(sizeof(*p)); assert(!pthread_mutex_init(p,NULL)); return p;
}
int xSemaphoreTake(void *p,unsigned ms) {
 for(unsigned i=0;;++i) { if(!pthread_mutex_trylock(p))return 1; if(i>=ms)return 0; delay(); }
}
void xSemaphoreGive(void *p) { assert(!pthread_mutex_unlock(p)); }
static volatile bool s_http_busy;
static bool xigua_ble_active(void) {return false;}
static void enter(void) {assert(++occupied==1); if(occupied>peak)peak=occupied;delay();}
static void leave(void) {assert(occupied==1);--occupied;}
static char *request_internal(const char *p,const char *b) {(void)p;(void)b;enter();++cloud_calls;leave();return (char *)"ok";}
static esp_err_t post_json_internal(const char *p,char *r,size_t n,size_t b) {(void)p;(void)r;(void)n;(void)b;enter();++model_calls;leave();return 3;}
static bool probe_public_https_internal(void) {enter();leave();return false;}
static esp_err_t asr_stream_internal(size_t w,char *r,size_t n) {(void)w;(void)r;(void)n;enter();leave();return 3;}
'''
CASES = r'''
static void *cloud(void *p) {(void)p;for(int i=0;i<100;++i){request("/snapshot",NULL);delay();}return NULL;}
static void *model(void *p) {(void)p;for(int i=0;i<30;++i)assert(post_json("{}",NULL,0,0)==3);return NULL;}
int main(void) {
 fail_create=1;assert(xigua_network_init()==2);assert(!xigua_network_take(0));
 fail_create=0;assert(!xigua_network_init());assert(!xigua_network_init());
 assert(xigua_network_take(0));assert(!request("/snapshot",NULL));
 assert(post_json("{}",NULL,0,0)==1);assert(!probe_public_https());assert(asr_stream(0,NULL,0)==1);
 xigua_network_give();
 assert(!probe_public_https());assert(xigua_network_take(0));xigua_network_give();
 assert(asr_stream(0,NULL,0)==3);assert(xigua_network_take(0));xigua_network_give();
 pthread_t a,b;assert(!pthread_create(&a,NULL,cloud,NULL));assert(!pthread_create(&b,NULL,model,NULL));
 pthread_join(a,NULL);pthread_join(b,NULL);
 assert(peak==1&&!occupied&&!s_http_busy&&cloud_calls>0&&model_calls==30);
 puts("HTTP/ASR exclusion, nonblocking cloud deferral and error/timeout lock release: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='xigua-network-') as folder:
    path=Path(folder);(path/'freertos').mkdir()
    (path/'esp_err.h').write_text('typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_TIMEOUT 1\n#define ESP_ERR_NO_MEM 2\n')
    (path/'freertos/FreeRTOS.h').write_text('#define pdTRUE 1\n#define pdMS_TO_TICKS(x) ((x)/1000)\n')
    (path/'freertos/semphr.h').write_text('typedef void *SemaphoreHandle_t;\nvoid *xSemaphoreCreateMutex(void);\nint xSemaphoreTake(void *,unsigned);\nvoid xSemaphoreGive(void *);\n')
    wrappers='\n'.join([function(AI,n) for n in ['post_json','probe_public_https','asr_stream']]+[function(CLOUD,'request')])
    (path/'test.c').write_text(HARNESS+wrappers+CASES)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-pthread',
                    '-I'+str(path),'-I'+str(ROOT/'main'),str(path/'test.c'),str(ROOT/'main/xigua_network.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True,timeout=40)
