"""Exercise real bounded BLE framing and host-stop ownership on the host."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'main/xigua_ble.c').read_text()
start=source.index('static esp_err_t stop_host(void)')
end=source.index('\nstatic void process_command',start)
stop=source[start:end]
prefix=r'''
#include "xigua_ble_protocol.h"
#include <assert.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_TIMEOUT 2
#define BLE_HS_CONN_HANDLE_NONE 65535
#define BLE_ERR_REM_USER_CONN_TERM 19
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
static bool s_initialized,s_stop_requested,s_host_exited;
static void *s_host,*s_host_done;
static bool s_synced;
static unsigned s_connection;
static int stop_error,deinit_error,ack,deleted,stops,deinits;
static int ble_gap_adv_stop(void) {return 0;}
static int ble_gap_terminate(unsigned h,int reason) {(void)h;(void)reason;return 0;}
static int nimble_port_stop(void) {++stops;return stop_error;}
static int xSemaphoreTake(void *s,int timeout) {(void)s;(void)timeout;return ack;}
static void vTaskDelete(void *s) {(void)s;++deleted;}
static int nimble_port_deinit(void) {++deinits;return deinit_error;}
'''
main=r'''
int main(void) {
 const char *json="{\"op\":\"connect\",\"ssid\":\"测试Wi-Fi\",\"password\":\"abcdefgh\"}\n";
 for(size_t chunk=1;chunk<=128;++chunk) {
  xigua_ble_frame_t f={0};size_t n=strlen(json);
  for(size_t i=0;i<n;i+=chunk) {
   size_t size=n-i<chunk?n-i:chunk;
   assert(xigua_ble_frame_feed(&f,(const uint8_t *)json+i,size)==(i+size==n?XIGUA_BLE_COMPLETE:XIGUA_BLE_PARTIAL));
  }
  assert(f.length==n-1&&!memcmp(f.data,json,n-1));
 }
 xigua_ble_frame_t f={0};uint8_t full[512];memset(full,'a',sizeof(full));full[511]='\n';
 assert(xigua_ble_frame_feed(&f,full,sizeof(full))==XIGUA_BLE_COMPLETE&&f.length==511);
 memset(&f,0,sizeof(f));full[511]='a';assert(xigua_ble_frame_feed(&f,full,sizeof(full))==XIGUA_BLE_INVALID&&f.length==0);
 assert(xigua_ble_frame_feed(&f,(const uint8_t *)"{}\n{}\n",6)==XIGUA_BLE_INVALID&&f.length==0);
 assert(xigua_ble_frame_feed(&f,(const uint8_t *)"\n",1)==XIGUA_BLE_INVALID);
 uint8_t nul[]={123,0,125};assert(xigua_ble_frame_feed(&f,nul,3)==XIGUA_BLE_INVALID&&f.length==0);
 assert(xigua_ble_wifi_valid("测试Wi-Fi","abcdefgh"));assert(xigua_ble_wifi_valid("open",""));
 assert(!xigua_ble_wifi_valid("","abcdefgh"));assert(!xigua_ble_wifi_valid("ssid","short"));
 assert(!xigua_ble_wifi_valid("ssid\n","abcdefgh"));assert(!xigua_ble_wifi_valid("ssid","abcdefg\n"));
 char name[33];memset(name,'x',32);name[32]=0;assert(!xigua_ble_wifi_valid(name,"abcdefgh"));name[31]=0;assert(xigua_ble_wifi_valid(name,"abcdefgh"));
 assert(xigua_ble_window_valid(299999999,XIGUA_BLE_WINDOW_US));assert(!xigua_ble_window_valid(300000000,XIGUA_BLE_WINDOW_US));assert(!xigua_ble_window_valid(0,0));
 s_initialized=true;s_host=(void *)1;s_host_done=(void *)2;s_connection=1;
 stop_error=1;assert(stop_host()==ESP_FAIL&&s_host&&s_initialized&&!deleted&&!deinits);
 stop_error=0;assert(stop_host()==ESP_ERR_TIMEOUT&&s_host&&s_initialized&&!deleted&&!deinits);
 assert(stops==2);ack=1;deinit_error=1;assert(stop_host()==1&&!s_host&&s_initialized&&deleted==1);
 deinit_error=0;assert(stop_host()==0&&!s_initialized&&!s_stop_requested&&s_connection==65535);
 assert(stop_host()==0&&deleted==1&&deinits==2);
 puts("BLE fragmented UTF-8, overflow/NUL rejection, Wi-Fi validation, window expiry and stop failure/retry ownership: PASS");
}
'''
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder)
    (path/'test.c').write_text(prefix+stop+main)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                    '-I'+str(ROOT/'main'),str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
