#!/usr/bin/env python3
"""Exercise the actual AI result consumer across chat/story mode switches."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'main/xigua_ai.c').read_text()
app=(ROOT/'main/xigua_app.c').read_text()
entry=re.search(r'^static void ui_ai_enter_mode\([^;]*?\)\n\{.*?^\}',app,re.M|re.S).group(0)
result=re.search(r'typedef struct \{\n    esp_err_t error;.*?\} xigua_ai_result_t;',source,re.S).group(0)
consumer=re.search(r'^bool xigua_ai_take_text\([^;]*?\)\n\{.*?^\}',source,re.M|re.S).group(0)
stub=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "xigua_ai.h"
#include "xigua_ai_ui.h"
#include "xigua_text.h"
#define pdTRUE 1
#define ESP_LOGI(...) ((void)0)
static void *s_results=(void *)1;
static x_ai_ui_t s_ai_ui;
static char s_ai_reply[XIGUA_AI_REPLY_BYTES],s_ai_error[64],s_ai_audio_status[64];
static bool s_ai_truncated;
static x_ai_view_t s_ai_rendered_view;
'''
queue=r'''
static xigua_ai_result_t queued;
static bool available;
static int xQueueReceive(void *q,void *out,unsigned ticks) {
 (void)q;(void)ticks;if(!available)return 0;
 memcpy(out,&queued,sizeof(queued));available=false;return 1;
}
'''
checks=r'''
int main(void) {
 s_ai_ui=(x_ai_ui_t){.view=X_AI_READING,.has_reply=true,.pages=3,.page=2};
 strcpy(s_ai_reply,"chat answer");strcpy(s_ai_error,"old error");strcpy(s_ai_audio_status,"old audio");s_ai_truncated=true;
 ui_ai_enter_mode(true);
 assert(s_ai_ui.story_reply && s_ai_ui.view==X_AI_READY && !s_ai_ui.has_reply);
 assert(!s_ai_reply[0] && !s_ai_error[0] && !s_ai_audio_status[0] && !s_ai_truncated);
 strcpy(s_ai_reply,"story answer");s_ai_ui.has_reply=true;s_ai_ui.view=X_AI_READING;
 ui_ai_enter_mode(true);assert(!strcmp(s_ai_reply,"story answer") && s_ai_ui.has_reply);
 ui_ai_enter_mode(false);assert(!s_ai_reply[0] && !s_ai_ui.has_reply && !s_ai_ui.story_reply);
 char text[128]="current story";esp_err_t error=9;bool truncated=true,audio_failed=true,matched=true;
 queued=(xigua_ai_result_t){.error=ESP_OK,.story=false};strcpy(queued.text,"late chat answer");available=true;
 assert(xigua_ai_take_text(text,sizeof(text),&error,&truncated,&audio_failed,true,&matched));
 assert(!matched && !available && !strcmp(text,"current story"));
 assert(error==9 && truncated && audio_failed);
 queued=(xigua_ai_result_t){.error=ESP_OK,.story=true};strcpy(queued.text,"new story");available=true;
 assert(xigua_ai_take_text(text,sizeof(text),&error,&truncated,&audio_failed,true,&matched));
 assert(matched && error==ESP_OK && !truncated && !audio_failed && !strcmp(text,"new story"));
 queued.error=ESP_FAIL;available=true;
 assert(xigua_ai_take_text(text,sizeof(text),&error,&truncated,&audio_failed,true,&matched));
 assert(matched && error==ESP_FAIL && !strcmp(text,"new story"));
 queued=(xigua_ai_result_t){.error=ESP_OK,.story=true};strcpy(queued.text,"late story");available=true;
 assert(xigua_ai_take_text(text,sizeof(text),&error,&truncated,&audio_failed,false,&matched));
 assert(!matched && !strcmp(text,"new story"));
 queued=(xigua_ai_result_t){.error=ESP_OK,.story=false};strcpy(queued.text,"new chat answer");available=true;
 assert(xigua_ai_take_text(text,sizeof(text),&error,&truncated,&audio_failed,false,&matched));
 assert(matched && !strcmp(text,"new chat answer"));
 assert(!xigua_ai_take_text(text,sizeof(text),&error,&truncated,&audio_failed,false,&matched));
 puts("AI result provenance, stale-mode rejection and failure preservation: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='xigua-result-modes-') as folder:
 path=Path(folder)
 (path/'esp_err.h').write_text('typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n')
 (path/'test.c').write_text(stub+result+queue+consumer+entry+checks)
 subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                 '-I'+str(path),'-I'+str(ROOT/'main'),str(path/'test.c'),'-o',str(path/'test')],check=True)
 subprocess.run([str(path/'test')],check=True)
