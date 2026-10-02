#include "xigua_backend.h"
#include "xigua_backend_config.h"
#include "xigua_app.h"
#include "xigua_catalog.h"
#include "xigua_ai.h"
#include "xigua_wifi.h"
#include "xigua_ble.h"
#include "xigua_network.h"
#include "xigua_text.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "xigua_cloud";
static TaskHandle_t s_task;
static SemaphoreHandle_t s_lock;
static volatile bool s_running;
static volatile bool s_http_busy;
bool xigua_backend_network_idle(void) { return !s_http_busy; }
/* Bounded cache; selection is copied before refresh can reorder tracks. */
static xigua_catalog_track_t s_catalog[XIGUA_CATALOG_CAPACITY];
static size_t s_catalog_count;
static bool s_catalog_refresh = true;
static char s_play_url[512];
static char s_status[96] = "云端未配置";
static char s_last_command[64];
static char s_care_context[3072];
static char s_handoff[1536];
static uint64_t s_care_revision;
static void refresh_care_context(void);
typedef struct { char id[64],status[12],error[96]; } playback_ack_t;
static QueueHandle_t s_acks;
static void status_text(const char *text)
{
    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        snprintf(s_status, sizeof(s_status), "%s", text);
        xSemaphoreGive(s_lock);
    }
}
void xigua_backend_status(char *out, size_t capacity)
{
    if (!out || !capacity) return;
    if (xigua_ai_backend_paused()) { snprintf(out,capacity,"音频已暂停"); return; }
    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        snprintf(out, capacity, "%s", s_status);
        xSemaphoreGive(s_lock);
    } else snprintf(out, capacity, "云端等待连接");
}

typedef struct { char *data; size_t length,capacity,maximum; bool overflow; } reply_t;
static esp_err_t response_event(esp_http_client_event_t *event)
{
    reply_t *r = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || !r || event->data_len <= 0) return ESP_OK;
    size_t needed=r->length+(size_t)event->data_len+1;
    if (needed>r->capacity) {
        if (needed>r->maximum) { r->overflow=true; return ESP_FAIL; }
        size_t capacity=r->capacity;
        while (capacity<needed && capacity<r->maximum) capacity*=2;
        if (capacity>r->maximum) capacity=r->maximum;
        char *larger=realloc(r->data,capacity);
        if (!larger) { r->overflow=true; return ESP_FAIL; }
        r->data=larger; r->capacity=capacity;
    }
    memcpy(r->data+r->length, event->data, (size_t)event->data_len);
    r->length += (size_t)event->data_len; r->data[r->length] = 0;
    return ESP_OK;
}
static char *request_internal(const char *path, const char *payload)
{
    if (!XIGUA_BACKEND_URL[0] || !XIGUA_BACKEND_DEVICE_TOKEN[0]) return NULL;
    char url[512],auth[256];
    if (snprintf(url,sizeof(url),"%s%s",XIGUA_BACKEND_URL,path)>=(int)sizeof(url)) return NULL;
    if (snprintf(auth,sizeof(auth),"Bearer %s",XIGUA_BACKEND_DEVICE_TOKEN)>=(int)sizeof(auth)) return NULL;
    reply_t r = { .capacity=1024, .maximum=strstr(path,"/v1/audio/tracks")==path?16384:8192,
        .data=calloc(1,1024) };
    if (!r.data) return NULL;
    esp_http_client_config_t cfg = { .url=url, .method=payload?HTTP_METHOD_POST:HTTP_METHOD_GET,
        .timeout_ms=8000, .buffer_size=1024, .buffer_size_tx=1024,
        .crt_bundle_attach=esp_crt_bundle_attach, .disable_auto_redirect=true,
        .event_handler=response_event, .user_data=&r };
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if (!client) { free(r.data); return NULL; }
    esp_http_client_set_header(client,"Authorization",auth);
    esp_http_client_set_header(client,"Content-Type","application/json");
    if (payload) esp_http_client_set_post_field(client,payload,(int)strlen(payload));
    esp_err_t err=xigua_ble_active()?ESP_ERR_INVALID_STATE:esp_http_client_perform(client);
    int code=esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err!=ESP_OK || code!=200 || r.overflow) {
        ESP_LOGW(TAG,"request failed: status=%d error=%s",code,esp_err_to_name(err));
        free(r.data); return NULL;
    }
    return r.data;
}
static char *request(const char *path,const char *payload)
{
    if (!xigua_network_take(0)) return NULL;
    s_http_busy=true;
    char *result=xigua_ble_active()?NULL:request_internal(path,payload);
    s_http_busy=false;
    xigua_network_give();
    return result;
}
void xigua_backend_report_playback(const char *id,const char *status,const char *error)
{
    if (!id || !id[0] || !s_acks) return;
    playback_ack_t ack={0};
    snprintf(ack.id,sizeof(ack.id),"%s",id);
    snprintf(ack.status,sizeof(ack.status),"%s",status);
    if (error) snprintf(ack.error,sizeof(ack.error),"%s",error);
    if (xQueueSend(s_acks,&ack,0)!=pdPASS) ESP_LOGW(TAG,"playback ACK queue full");
}
/* Network acknowledgement never holds the PCM/AI worker during pause or handoff. */
static void flush_playback_acks(void)
{
    playback_ack_t ack;
    for (unsigned i=0; i<4 && s_running && xQueuePeek(s_acks,&ack,0)==pdTRUE; ++i) {
        char path[160]; snprintf(path,sizeof(path),"/v1/device/command/%s",ack.id);
        cJSON *obj=cJSON_CreateObject(); if (!obj) return;
        cJSON_AddStringToObject(obj,"status",ack.status);
        if (ack.error[0]) cJSON_AddStringToObject(obj,"error",ack.error);
        char *payload=cJSON_PrintUnformatted(obj); cJSON_Delete(obj); if (!payload) return;
        char *reply=request(path,payload); free(payload);
        if (!reply) return; /* Retain the head for an offline retry. */
        free(reply);
        (void)xQueueReceive(s_acks,&ack,0);
        ESP_LOGI(TAG,"playback ACK: %s",ack.status);
    }
}
static void poll_command(void)
{
    char *data=request("/v1/device/command",NULL);
    if (!data) return;
    cJSON *root=cJSON_Parse(data); free(data);
    cJSON *cmd=cJSON_GetObjectItemCaseSensitive(root,"command");
    cJSON *id=cJSON_GetObjectItemCaseSensitive(cmd,"id");
    cJSON *action=cJSON_GetObjectItemCaseSensitive(cmd,"action");
    if (cJSON_IsString(id) && cJSON_IsString(action) && strcmp(id->valuestring,s_last_command)) {
        if (!strcmp(action->valuestring,"stop")) {
            esp_err_t err=xigua_ai_stop_backend(id->valuestring);
            if (err!=ESP_OK) xigua_backend_report_playback(id->valuestring,"error","cannot stop audio");
            snprintf(s_last_command,sizeof(s_last_command),"%s",id->valuestring);
            status_text("已停止云端音频");
        } else if (!strcmp(action->valuestring,"pause") || !strcmp(action->valuestring,"resume")) {
            bool pause=!strcmp(action->valuestring,"pause");
            esp_err_t err=pause?xigua_ai_pause_backend(id->valuestring):xigua_ai_resume_backend(id->valuestring);
            if (err!=ESP_OK) xigua_backend_report_playback(id->valuestring,"error",
                pause?"no playing song to pause":"no paused song to resume");
            snprintf(s_last_command,sizeof(s_last_command),"%s",id->valuestring);
        } else if (!strcmp(action->valuestring,"play")) {
            cJSON *track=cJSON_GetObjectItemCaseSensitive(root,"track");
            cJSON *url=cJSON_GetObjectItemCaseSensitive(track,"play_url");
            if (cJSON_IsString(url)) {
                esp_err_t err=xigua_ai_play_backend(url->valuestring,XIGUA_BACKEND_DEVICE_TOKEN,id->valuestring);
                if (err==ESP_OK) {
                    snprintf(s_last_command,sizeof(s_last_command),"%s",id->valuestring);
                    status_text("正在播放云端音频");
                } else if (err!=ESP_ERR_INVALID_STATE) {
                    xigua_backend_report_playback(id->valuestring,"error","cannot queue audio");
                }
            }
        } else if (!strcmp(action->valuestring,"clear_records")) {
            esp_err_t err=xigua_app_clear_local_records();
            xigua_backend_report_playback(id->valuestring,err==ESP_OK?"complete":"error",
                err==ESP_OK?NULL:esp_err_to_name(err));
            snprintf(s_last_command,sizeof(s_last_command),"%s",id->valuestring);
            status_text(err==ESP_OK?"本地记录已清除":"本地记录清除失败");
        }
    }
    cJSON_Delete(root);
}
static void refresh_catalog(void)
{
    xigua_catalog_track_t *tracks=calloc(XIGUA_CATALOG_CAPACITY,sizeof(*tracks));
    if (!tracks) return;
    size_t count=0;
    bool valid=true;
    static const char *const categories[] = { "song", "story", "classical", "white_noise" };
    for (size_t category_index=0; category_index<sizeof(categories)/sizeof(categories[0]); ++category_index) {
        char path[96];
        snprintf(path,sizeof(path),"/v1/audio/tracks?category=%s",categories[category_index]);
        char *data=request(path,NULL);
        if (!data) { valid=false; break; }
        cJSON *root=cJSON_Parse(data); free(data);
        if (!root) { valid=false; break; }
        cJSON *items=cJSON_GetObjectItemCaseSensitive(root,"items"),*item;
        if (!cJSON_IsArray(items)) { valid=false; cJSON_Delete(root); break; }
        cJSON_ArrayForEach(item,items) {
            cJSON *url=cJSON_GetObjectItemCaseSensitive(item,"play_url");
            cJSON *title=cJSON_GetObjectItemCaseSensitive(item,"title");
            cJSON *mime=cJSON_GetObjectItemCaseSensitive(item,"mime_type");
            cJSON *category=cJSON_GetObjectItemCaseSensitive(item,"category");
            if (count==XIGUA_CATALOG_CAPACITY) break;
            if (!cJSON_IsString(url) || !cJSON_IsString(title) || !cJSON_IsString(mime) ||
                strcmp(mime->valuestring,"audio/wav")) continue;
            const char *label = cJSON_IsString(category) ? category->valuestring : categories[category_index];
            if (xigua_catalog_track_category(&tracks[count],XIGUA_BACKEND_URL,title->valuestring,
                                             url->valuestring,mime->valuestring,label)) ++count;
        }
        cJSON_Delete(root);
    }
    if (valid && xSemaphoreTake(s_lock,pdMS_TO_TICKS(100))==pdTRUE) {
        memcpy(s_catalog,tracks,sizeof(s_catalog)); s_catalog_count=count;
        xSemaphoreGive(s_lock);
        status_text(count?"上/下选择  确认键播放":"暂无可播放音频");
        ESP_LOGI(TAG,"catalog refreshed: %u tracks",(unsigned)count);
    } else status_text("音频目录读取失败");
    free(tracks);
}
size_t xigua_backend_catalog_item_category(size_t index,char *title,size_t capacity,
                                            char *category,size_t category_capacity)
{
    size_t count=0;
    if (title && capacity) title[0]=0;
    if (category && category_capacity) category[0]=0;
    if (s_lock && xSemaphoreTake(s_lock,pdMS_TO_TICKS(20))==pdTRUE) {
        count=s_catalog_count;
        if (index<count) {
            if (title && capacity) snprintf(title,capacity,"%s",s_catalog[index].title);
            if (category && category_capacity) snprintf(category,category_capacity,"%s",s_catalog[index].category);
        }
        xSemaphoreGive(s_lock);
    }
    return count;
}
size_t xigua_backend_catalog_item(size_t index,char *title,size_t capacity,bool *white)
{
    char category[XIGUA_CATALOG_CATEGORY_CAPACITY] = {0};
    size_t count=xigua_backend_catalog_item_category(index,title,capacity,category,sizeof(category));
    if (white) *white=!strcmp(category,"white_noise");
    return count;
}
void xigua_backend_refresh_catalog(void)
{
    if (s_lock && xSemaphoreTake(s_lock,0)==pdTRUE) {
        s_catalog_refresh=true; xSemaphoreGive(s_lock);
    }
}
void xigua_backend_play_track(size_t index)
{
    if (s_lock && xSemaphoreTake(s_lock,0)==pdTRUE) {
        if (index<s_catalog_count) snprintf(s_play_url,sizeof(s_play_url),"%s",s_catalog[index].url);
        xSemaphoreGive(s_lock);
    }
}
static void cloud_task(void *arg)
{
    (void)arg;
    unsigned ticks=0;
    while (s_running) {
        if (!xigua_ble_active() && xigua_wifi_state()==XIGUA_WIFI_CONNECTED && xigua_ai_voice_phase()!=XIGUA_AI_VOICE_RECORDING &&
            xigua_ai_voice_phase()!=XIGUA_AI_VOICE_TRANSCRIBING && xigua_ai_voice_phase()!=XIGUA_AI_VOICE_THINKING) {
            if (ticks++ % 6 == 0) {
                char *snapshot=xigua_app_cloud_snapshot();
                if (snapshot) {
                    char *reply=request("/v1/device/snapshot",snapshot); free(snapshot);
                    if (reply) { status_text("记录已同步到云端"); ESP_LOGI(TAG,"snapshot acknowledged"); }
                    else status_text("同步失败，稍后重试");
                    free(reply);
                }
            }
            if (ticks % 6 == 1) refresh_care_context();
            bool refresh=false;
            char play_url[512]={0};
            if (xSemaphoreTake(s_lock,pdMS_TO_TICKS(50))==pdTRUE) {
                refresh=s_catalog_refresh || ticks % 12 == 1;
                s_catalog_refresh=false;
                xSemaphoreGive(s_lock);
            }
            if (refresh) refresh_catalog();
            /* Read pending selection after network refresh: stop can cancel it while HTTP waits. */
            if (xSemaphoreTake(s_lock,pdMS_TO_TICKS(50))==pdTRUE) {
                snprintf(play_url,sizeof(play_url),"%s",s_play_url); s_play_url[0]=0;
                xSemaphoreGive(s_lock);
            }
            if (play_url[0]) {
                esp_err_t err=xigua_ai_play_backend(play_url,XIGUA_BACKEND_DEVICE_TOKEN,"");
                status_text(err==ESP_OK?"正在播放云端音频":"暂无可播放音频或语音忙");
            }
            poll_command();
            flush_playback_acks();
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    s_task=NULL;
    vTaskDelete(NULL);
}
esp_err_t xigua_backend_start(void)
{
    if (!XIGUA_BACKEND_URL[0] || !XIGUA_BACKEND_DEVICE_TOKEN[0]) return ESP_OK;
    if (s_task) return ESP_ERR_INVALID_STATE;
    if (xigua_network_init()!=ESP_OK) return ESP_ERR_NO_MEM;
    if (!s_lock) s_lock=xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    if (!s_acks) s_acks=xQueueCreate(8,sizeof(playback_ack_t));
    if (!s_acks) return ESP_ERR_NO_MEM;
    xQueueReset(s_acks);
    s_last_command[0]=0;
    s_catalog_refresh=true;
    s_running=true;
    status_text("云端等待连接");
    if (xTaskCreate(cloud_task,"xigua_cloud",6144,NULL,3,&s_task)!=pdPASS) { s_running=false; return ESP_ERR_NO_MEM; }
    return ESP_OK;
}
void xigua_backend_stop(void)
{
    s_running=false;
    /* Wait for bounded HTTP requests; do not close NVS under a live snapshot reader. */
    while (s_task) vTaskDelay(pdMS_TO_TICKS(20));
}
void xigua_backend_stop_sound(void)
{
    if (s_lock && xSemaphoreTake(s_lock,0)==pdTRUE) { s_play_url[0]=0; xSemaphoreGive(s_lock); }
    (void)xigua_ai_stop_backend(""); status_text("已停止云端音频");
}

void xigua_backend_toggle_pause(void)
{
    esp_err_t err=xigua_ai_backend_paused()?xigua_ai_resume_backend(""):xigua_ai_pause_backend("");
    if (err!=ESP_OK) status_text("暂无可播放音频或语音忙");
}

bool xigua_backend_care_context(char *text, size_t capacity, uint64_t *revision)
{
    if (!text || !capacity) return false;
    text[0] = 0;
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    xigua_text_copy(text, capacity, s_care_context);
    if (revision) *revision = s_care_revision;
    xSemaphoreGive(s_lock);
    return text[0] != 0;
}

bool xigua_backend_handoff(char *text, size_t capacity)
{
    if (!text || !capacity) return false;
    text[0] = 0;
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    xigua_text_copy(text, capacity, s_handoff);
    xSemaphoreGive(s_lock);
    return text[0] != 0;
}

static void refresh_care_context(void)
{
    char *data = request("/v1/device/context", NULL);
    if (!data) return;
    cJSON *root = cJSON_Parse(data);
    free(data);
    cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
    cJSON *handoff = cJSON_GetObjectItemCaseSensitive(root, "handoff");
    cJSON *revision = cJSON_GetObjectItemCaseSensitive(root, "revision");
    if (cJSON_IsString(text) && cJSON_IsString(handoff) && cJSON_IsNumber(revision) &&
        revision->valuedouble >= 0 && revision->valuedouble <= 9007199254740991.0 &&
        strlen(text->valuestring) < sizeof(s_care_context) &&
        strlen(handoff->valuestring) < sizeof(s_handoff) &&
        xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        xigua_text_copy(s_care_context, sizeof(s_care_context), text->valuestring);
        xigua_text_copy(s_handoff, sizeof(s_handoff), handoff->valuestring);
        s_care_revision = (uint64_t)revision->valuedouble;
        xSemaphoreGive(s_lock);
    }
    cJSON_Delete(root);
}
