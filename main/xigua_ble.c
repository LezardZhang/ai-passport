#include "xigua_ble.h"
#include "xigua_ble_protocol.h"
#include "xigua_wifi.h"
#include "xigua_ai.h"
#include "xigua_backend.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG="xigua_ble";
/* 7e24a7f0-9b52-4f36-a7f8-84e9d9b00001, commands 00002, response 00003. */
#define UUID_BYTES(n) n,0x00,0xb0,0xd9,0xe9,0x84,0xf8,0xa7,0x36,0x4f,0x52,0x9b,0xf0,0xa7,0x24,0x7e
static const ble_uuid128_t s_service_uuid=BLE_UUID128_INIT(UUID_BYTES(0x01));
static const ble_uuid128_t s_command_uuid=BLE_UUID128_INIT(UUID_BYTES(0x02));
static const ble_uuid128_t s_status_uuid=BLE_UUID128_INIT(UUID_BYTES(0x03));
static TaskHandle_t s_manager,s_host;
static QueueHandle_t s_commands;
static SemaphoreHandle_t s_host_done;
static volatile bool s_active,s_close,s_shutdown,s_synced,s_host_exited;
static bool s_initialized,s_stop_requested;
static volatile uint16_t s_connection=BLE_HS_CONN_HANDLE_NONE;
static volatile int s_error;
static uint8_t s_addr_type;
static uint32_t s_pin;
static int64_t s_deadline;
static char s_name[24],s_reply[512]="{}",s_phase[32]="off";
static uint32_t s_request_id;
static int s_list_index=-1,s_builtin_index=-1;
static bool s_joining;
static int64_t s_join_deadline;
static xigua_ble_frame_t s_frame;
static portMUX_TYPE s_mux=portMUX_INITIALIZER_UNLOCKED;

bool xigua_ble_active(void) { return s_active; }

static void publish(void)
{
    char ssid[33],ip[16]; xigua_wifi_status(ssid,sizeof(ssid),ip,sizeof(ip));
    cJSON *root=cJSON_CreateObject(); if (!root) return;
    cJSON_AddStringToObject(root,"schema","xigua-ble-wifi-v1");
    cJSON_AddNumberToObject(root,"id",s_request_id);
    cJSON_AddStringToObject(root,"phase",s_phase);
    cJSON_AddStringToObject(root,"wifi",xigua_wifi_state()==XIGUA_WIFI_CONNECTED?"connected":
                            xigua_wifi_state()==XIGUA_WIFI_CONNECTING?"connecting":"disconnected");
    cJSON_AddStringToObject(root,"ssid",ssid); cJSON_AddStringToObject(root,"ip",ip);
    cJSON_AddBoolToObject(root,"saved",xigua_wifi_credentials_saved());
    cJSON_AddBoolToObject(root,"scanning",xigua_wifi_scan_in_progress());
    cJSON_AddNumberToObject(root,"total",xigua_wifi_scan_count());
    cJSON_AddNumberToObject(root,"builtin_total",xigua_wifi_builtin_count());
    if (s_list_index>=0 && (size_t)s_list_index<xigua_wifi_scan_count()) {
        cJSON_AddNumberToObject(root,"index",s_list_index);
        cJSON_AddStringToObject(root,"network",xigua_wifi_scan_ssid((size_t)s_list_index));
        cJSON_AddNumberToObject(root,"rssi",xigua_wifi_scan_rssi((size_t)s_list_index));
    }
    if (s_builtin_index>=0 && (size_t)s_builtin_index<xigua_wifi_builtin_count()) {
        cJSON_AddNumberToObject(root,"builtin_index",s_builtin_index);
        cJSON_AddStringToObject(root,"builtin",xigua_wifi_builtin_ssid((size_t)s_builtin_index));
    }
    char *text=cJSON_PrintUnformatted(root); cJSON_Delete(root);
    if (text) {
        portENTER_CRITICAL(&s_mux);
        snprintf(s_reply,sizeof(s_reply),"%s",strlen(text)<sizeof(s_reply)?text:
                 "{\"phase\":\"response_too_large\"}");
        portEXIT_CRITICAL(&s_mux); free(text);
    }
}

static int access_gatt(uint16_t conn, uint16_t handle, struct ble_gatt_access_ctxt *ctx,void *arg)
{
    (void)handle;(void)arg;
    if (!s_active || s_close || !xigua_ble_window_valid(esp_timer_get_time(),s_deadline))
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn,&desc) || !desc.sec_state.encrypted || !desc.sec_state.authenticated)
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (ctx->op==BLE_GATT_ACCESS_OP_READ_CHR) {
        char text[512]; portENTER_CRITICAL(&s_mux); memcpy(text,s_reply,sizeof(text));portEXIT_CRITICAL(&s_mux);
        return os_mbuf_append(ctx->om,text,strlen(text))==0?0:BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctx->op!=BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    uint8_t bytes[128]; uint16_t size=0;
    if (OS_MBUF_PKTLEN(ctx->om)>sizeof(bytes) || ble_hs_mbuf_to_flat(ctx->om,bytes,sizeof(bytes),&size))
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    xigua_ble_frame_result_t result=xigua_ble_frame_feed(&s_frame,bytes,size);
    if (result==XIGUA_BLE_INVALID) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (result==XIGUA_BLE_COMPLETE) {
        bool queued=xQueueSend(s_commands,s_frame.data,0)==pdPASS;
        memset(&s_frame,0,sizeof(s_frame)); memset(bytes,0,sizeof(bytes));
        return queued?0:BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    memset(bytes,0,sizeof(bytes)); return 0;
}

static const struct ble_gatt_svc_def s_services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&s_service_uuid.u,
     .characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=&s_command_uuid.u,.access_cb=access_gatt,
         .flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_ENC|BLE_GATT_CHR_F_WRITE_AUTHEN},
        {.uuid=&s_status_uuid.u,.access_cb=access_gatt,
         .flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_READ_AUTHEN},
        {0}}}, {0}};

static int gap_event(struct ble_gap_event *event,void *arg);
static int advertise(void)
{
    if (s_close || !s_active) return 0;
    struct ble_hs_adv_fields fields={0};
    fields.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128=(ble_uuid128_t *)&s_service_uuid;fields.num_uuids128=1;fields.uuids128_is_complete=1;
    int rc=ble_gap_adv_set_fields(&fields); if (rc) return rc;
    struct ble_hs_adv_fields scan={0};scan.name=(const uint8_t *)s_name;
    scan.name_len=strlen(s_name);scan.name_is_complete=1;
    rc=ble_gap_adv_rsp_set_fields(&scan);if (rc) return rc;
    struct ble_gap_adv_params params={0};params.conn_mode=BLE_GAP_CONN_MODE_UND;
    params.disc_mode=BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(s_addr_type,NULL,BLE_HS_FOREVER,&params,gap_event,NULL);
}
static int gap_event(struct ble_gap_event *event,void *arg)
{
    (void)arg;
    if (event->type==BLE_GAP_EVENT_CONNECT) {
        if (event->connect.status) s_error=advertise();
        else { s_connection=event->connect.conn_handle;int rc=ble_gap_security_initiate(s_connection);
               if (rc && rc!=BLE_HS_EALREADY) s_error=rc; }
    } else if (event->type==BLE_GAP_EVENT_DISCONNECT) {
        s_connection=BLE_HS_CONN_HANDLE_NONE;memset(&s_frame,0,sizeof(s_frame));
        xQueueReset(s_commands);s_error=advertise();
    } else if (event->type==BLE_GAP_EVENT_PASSKEY_ACTION && event->passkey.params.action==BLE_SM_IOACT_DISP) {
        struct ble_sm_io io={.action=BLE_SM_IOACT_DISP,.passkey=s_pin};
        s_error=ble_sm_inject_io(event->passkey.conn_handle,&io);
    } else if (event->type==BLE_GAP_EVENT_ENC_CHANGE && event->enc_change.status) {
        (void)ble_gap_terminate(event->enc_change.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
    }
    return 0;
}
static void on_reset(int reason) { s_error=reason; }
static void on_sync(void)
{
    int rc=ble_hs_util_ensure_addr(0);if (!rc) rc=ble_hs_id_infer_auto(0,&s_addr_type);
    if (!rc) rc=advertise();
    s_error=rc;s_synced=rc==0;
}
static void host_task(void *arg)
{
    (void)arg;nimble_port_run();s_host_exited=true;xSemaphoreGive(s_host_done);
    for (;;) vTaskSuspend(NULL);
}
static esp_err_t start_host(void)
{
    esp_err_t err=nimble_port_init(); if (err!=ESP_OK) return err;s_initialized=true;
    ble_svc_gap_init();ble_svc_gatt_init();
    int rc=ble_svc_gap_device_name_set(s_name);
    if (!rc) rc=ble_gatts_count_cfg(s_services);
    if (!rc) rc=ble_gatts_add_svcs(s_services);
    if (rc) return ESP_FAIL;
    ble_hs_cfg.reset_cb=on_reset;ble_hs_cfg.sync_cb=on_sync;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_DISPLAY_ONLY;ble_hs_cfg.sm_mitm=1;
    ble_hs_cfg.sm_sc=1;ble_hs_cfg.sm_bonding=0;
    return xTaskCreatePinnedToCore(host_task,"xigua_ble_host",NIMBLE_HS_STACK_SIZE,NULL,
                                  configMAX_PRIORITIES-4,&s_host,NIMBLE_CORE)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
static esp_err_t stop_host(void)
{
    if (!s_initialized) return ESP_OK;
    if (s_host && !s_stop_requested) {
        (void)ble_gap_adv_stop();
        if (s_connection!=BLE_HS_CONN_HANDLE_NONE) (void)ble_gap_terminate(s_connection,BLE_ERR_REM_USER_CONN_TERM);
        int rc=nimble_port_stop();if (rc) return ESP_FAIL;s_stop_requested=true;
    }
    if (s_host && !s_host_exited && xSemaphoreTake(s_host_done,pdMS_TO_TICKS(2000))!=pdTRUE) return ESP_ERR_TIMEOUT;
    if (s_host) { vTaskDelete(s_host);s_host=NULL; }
    esp_err_t err=nimble_port_deinit();if (err!=ESP_OK) return err;
    s_initialized=false;s_stop_requested=false;s_synced=false;s_host_exited=false;
    s_connection=BLE_HS_CONN_HANDLE_NONE;return ESP_OK;
}

static void process_command(char *text)
{
    const char *end=NULL;
    cJSON *root=strstr(text,"\\u0000")?NULL:cJSON_ParseWithOpts(text,&end,1);
    cJSON *id=cJSON_GetObjectItemCaseSensitive(root,"id"),*op=cJSON_GetObjectItemCaseSensitive(root,"op");
    if (!cJSON_IsObject(root) || !cJSON_IsNumber(id) || id->valuedouble<1 || id->valuedouble>2147483647 ||
        id->valuedouble!=(double)id->valueint || !cJSON_IsString(op)) {
        snprintf(s_phase,sizeof(s_phase),"invalid_command");cJSON_Delete(root);return;
    }
    s_request_id=(uint32_t)id->valueint;s_list_index=s_builtin_index=-1;
    esp_err_t err=ESP_OK;bool begin_join=false;
    cJSON *index=cJSON_GetObjectItemCaseSensitive(root,"index");
    bool valid_index=cJSON_IsNumber(index) && index->valuedouble>=0 && index->valuedouble<16 &&
                     index->valuedouble==(double)index->valueint;
    if (!strcmp(op->valuestring,"status")) { /* Keep the last connection result. */ }
    else if (!strcmp(op->valuestring,"finish")) {
        snprintf(s_phase,sizeof(s_phase),"closing");s_deadline=esp_timer_get_time()+2000000;
    }
    else if (s_joining) snprintf(s_phase,sizeof(s_phase),"busy");
    else if (!strcmp(op->valuestring,"scan")) {
        err=xigua_wifi_scan();snprintf(s_phase,sizeof(s_phase),err==ESP_OK?"scanning":"scan_failed");
    } else if (!strcmp(op->valuestring,"network") && valid_index && (size_t)index->valueint<xigua_wifi_scan_count()) {
        s_list_index=index->valueint;
    } else if (!strcmp(op->valuestring,"preset") && valid_index && (size_t)index->valueint<xigua_wifi_builtin_count()) {
        s_builtin_index=index->valueint;
    } else if (!strcmp(op->valuestring,"connect_preset") && valid_index && (size_t)index->valueint<xigua_wifi_builtin_count()) {
        err=xigua_wifi_connect_builtin((size_t)index->valueint);s_joining=err==ESP_OK;begin_join=s_joining;
        snprintf(s_phase,sizeof(s_phase),s_joining?"connecting":"connect_failed");
    } else if (!strcmp(op->valuestring,"connect")) {
        cJSON *ssid=cJSON_GetObjectItemCaseSensitive(root,"ssid"),*password=cJSON_GetObjectItemCaseSensitive(root,"password");
        if (cJSON_IsString(ssid) && cJSON_IsString(password) && xigua_ble_wifi_valid(ssid->valuestring,password->valuestring)) {
            err=xigua_wifi_set_credentials(ssid->valuestring,password->valuestring);
            if (err==ESP_OK) err=xigua_wifi_connect();
            s_joining=err==ESP_OK;begin_join=s_joining;
            snprintf(s_phase,sizeof(s_phase),s_joining?"connecting":"connect_failed");
        } else snprintf(s_phase,sizeof(s_phase),"invalid_wifi");
        if (cJSON_IsString(password)) memset(password->valuestring,0,strlen(password->valuestring));
    } else snprintf(s_phase,sizeof(s_phase),"invalid_command");
    if (begin_join) s_join_deadline=esp_timer_get_time()+45000000;
    cJSON_Delete(root);
}

static void manager_task(void *arg)
{
    (void)arg;char command[XIGUA_BLE_FRAME_BYTES];
    int64_t wait_until=esp_timer_get_time()+30000000;
    while (!s_close && (!xigua_ai_network_idle() || !xigua_backend_network_idle())) {
        if (esp_timer_get_time()>=wait_until) { s_error=ESP_ERR_TIMEOUT;s_close=true;break; }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!s_close) { s_error=start_host();if (s_error) s_close=true; }
    while (!s_close && xigua_ble_window_valid(esp_timer_get_time(),s_deadline)) {
        if (s_synced && !strcmp(s_phase,"starting")) {
            snprintf(s_phase,sizeof(s_phase),"ready");
            ESP_LOGI(TAG,"provisioning ready heap=%u largest=%u",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        }
        if (s_error) { snprintf(s_phase,sizeof(s_phase),"ble_failed");s_close=true;break; }
        if (xQueueReceive(s_commands,command,pdMS_TO_TICKS(200))==pdTRUE) {
            if (s_connection!=BLE_HS_CONN_HANDLE_NONE) process_command(command);
            memset(command,0,sizeof(command));
        }
        if (!strcmp(s_phase,"scanning") && !xigua_wifi_scan_in_progress()) snprintf(s_phase,sizeof(s_phase),"scan_complete");
        if (s_joining && xigua_wifi_state()==XIGUA_WIFI_CONNECTED) {
            s_joining=false;snprintf(s_phase,sizeof(s_phase),xigua_wifi_credentials_saved()?"connected":"connected_unsaved");
        } else if (s_joining && (xigua_wifi_state()!=XIGUA_WIFI_CONNECTING || esp_timer_get_time()>=s_join_deadline)) {
            s_joining=false;snprintf(s_phase,sizeof(s_phase),"connect_failed");
        }
        publish();
    }
    s_close=true;
    /* Keep ownership and retry cleanup; never free queues below a live host. */
    while (stop_host()!=ESP_OK) { ESP_LOGW(TAG,"BLE cleanup pending");vTaskDelay(pdMS_TO_TICKS(500)); }
    memset(&s_frame,0,sizeof(s_frame));s_pin=0;
    vQueueDelete(s_commands);s_commands=NULL;vSemaphoreDelete(s_host_done);s_host_done=NULL;
    s_active=false;snprintf(s_phase,sizeof(s_phase),s_error?"ble_failed":"off");
    ESP_LOGI(TAG,"provisioning closed heap=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    s_manager=NULL;vTaskDelete(NULL);
}

esp_err_t xigua_ble_begin(void)
{
    if (s_manager || s_active || s_shutdown) return ESP_ERR_INVALID_STATE;
    uint8_t mac[6];if (esp_read_mac(mac,ESP_MAC_BT)!=ESP_OK) return ESP_FAIL;
    snprintf(s_name,sizeof(s_name),"Xigua-%02X%02X%02X",mac[3],mac[4],mac[5]);
    s_commands=xQueueCreate(2,XIGUA_BLE_FRAME_BYTES);s_host_done=xSemaphoreCreateBinary();
    if (!s_commands || !s_host_done) {
        if (s_commands) vQueueDelete(s_commands);
        if (s_host_done) vSemaphoreDelete(s_host_done);
        s_commands=NULL;s_host_done=NULL;return ESP_ERR_NO_MEM;
    }
    s_pin=esp_random()%1000000;s_deadline=esp_timer_get_time()+XIGUA_BLE_WINDOW_US;
    s_close=false;s_error=0;s_joining=false;s_request_id=0;s_list_index=s_builtin_index=-1;
    snprintf(s_phase,sizeof(s_phase),"starting");s_active=true;publish();
    if (xTaskCreate(manager_task,"xigua_ble",4096,NULL,4,&s_manager)!=pdPASS) {
        s_active=false;vQueueDelete(s_commands);vSemaphoreDelete(s_host_done);
        s_commands=NULL;s_host_done=NULL;return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
void xigua_ble_end(void) { s_close=true; }
esp_err_t xigua_ble_shutdown(void)
{
    s_shutdown=true;s_close=true;int64_t deadline=esp_timer_get_time()+5000000;
    while (s_manager && esp_timer_get_time()<deadline) vTaskDelay(pdMS_TO_TICKS(20));
    if (s_manager) return ESP_ERR_TIMEOUT;
    s_shutdown=false;return ESP_OK;
}
void xigua_ble_screen(char *out,size_t capacity)
{
    const char *status=!s_active?"已关闭":!s_synced?"正在启动":s_joining?"正在连接 Wi-Fi":
        s_connection!=BLE_HS_CONN_HANDLE_NONE?"手机已连接":"等待手机连接";
    if (s_error) status="启动失败，请重试";
    long seconds=s_active?(long)((s_deadline-esp_timer_get_time())/1000000):0;
    snprintf(out,capacity,"%s\nBLE: %s\n配对码：%06lu\n剩余 %ld 秒\n\n手机打开管理页面\n选择连接设备",
             status,s_name,(unsigned long)s_pin,seconds>0?seconds:0);
}
