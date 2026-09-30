#include "xigua_ai.h"
#include "xigua_wifi.h"

#include "bsp_audio.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "apps/esp_sntp.h"
#include "mbedtls/base64.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>

#if defined(__has_include)
#if __has_include("xigua_ai_credentials_local.h")
#include "xigua_ai_credentials_local.h"
#else
#include "xigua_ai_credentials.h"
#endif
#else
#include "xigua_ai_credentials.h"
#endif

#ifndef XIGUA_AI_BASE_URL
#define XIGUA_AI_BASE_URL ""
#endif
#ifndef XIGUA_AI_API_KEY
#define XIGUA_AI_API_KEY ""
#endif
#ifndef XIGUA_AI_CHAT_MODEL
#define XIGUA_AI_CHAT_MODEL "mimo-v2.6-flash"
#endif
#ifndef XIGUA_AI_CHAT_PRO_MODEL
#define XIGUA_AI_CHAT_PRO_MODEL "mimo-v2.6-pro"
#endif
#ifndef XIGUA_AI_CHAT_LEGACY_PRO_MODEL
#define XIGUA_AI_CHAT_LEGACY_PRO_MODEL "mimo-v2.5-pro"
#endif
#ifndef XIGUA_AI_CHAT_LEGACY_MODEL
#define XIGUA_AI_CHAT_LEGACY_MODEL "mimo-v2.5"
#endif
#ifndef XIGUA_AI_ASR_MODEL
#define XIGUA_AI_ASR_MODEL "mimo-v2.5-asr"
#endif
#ifndef XIGUA_AI_TTS_MODEL
#define XIGUA_AI_TTS_MODEL "mimo-v2.5-tts"
#endif
#ifndef XIGUA_AI_TTS_VOICECLONE_MODEL
#define XIGUA_AI_TTS_VOICECLONE_MODEL "mimo-v2.5-tts-voiceclone"
#endif
#ifndef XIGUA_AI_TTS_VOICEDESIGN_MODEL
#define XIGUA_AI_TTS_VOICEDESIGN_MODEL "mimo-v2.5-tts-voicedesign"
#endif

#define XIGUA_AI_PROMPT_MAX 384
#define XIGUA_AI_RESPONSE_MAX 512
#define XIGUA_AI_HTTP_BODY_MAX 16384
#define XIGUA_AI_ASR_BODY_MAX 4096
#define XIGUA_AI_TASK_STACK 6144
#define XIGUA_AI_VOICE_MAX_SECONDS 60
#define XIGUA_AI_VOICE_MIN_SECONDS 1
#define XIGUA_AI_VOICE_HZ 16000
#define XIGUA_AI_VOICE_BITS 16
#define XIGUA_AI_VOICE_CHANNELS 1
#define XIGUA_AI_VOICE_MAX_PCM_BYTES (XIGUA_AI_VOICE_MAX_SECONDS * XIGUA_AI_VOICE_HZ * \
                                  (XIGUA_AI_VOICE_BITS / 8) * XIGUA_AI_VOICE_CHANNELS)
#define XIGUA_AI_WAV_HEADER_BYTES 44
#define XIGUA_AI_VOICE_MAX_WAV_BYTES (XIGUA_AI_WAV_HEADER_BYTES + XIGUA_AI_VOICE_MAX_PCM_BYTES)
#define XIGUA_AI_AUDIO_CHUNK_BYTES 2048
#define XIGUA_AI_VOICE_PARTITION_LABEL "voice_tmp"
#define XIGUA_AI_VOICE_FLASH_CHUNK_BYTES 3072
#define XIGUA_AI_VOICE_B64_CHUNK_BYTES 4100

static const char *TAG = "xigua_ai";
static const char *const s_models[XIGUA_AI_MODEL_COUNT] = {
    XIGUA_AI_CHAT_MODEL,
    XIGUA_AI_CHAT_PRO_MODEL,
    XIGUA_AI_CHAT_LEGACY_PRO_MODEL,
    XIGUA_AI_CHAT_LEGACY_MODEL,
    XIGUA_AI_ASR_MODEL,
    XIGUA_AI_TTS_MODEL,
    XIGUA_AI_TTS_VOICECLONE_MODEL,
    XIGUA_AI_TTS_VOICEDESIGN_MODEL,
};

typedef struct {
    enum {
        XIGUA_AI_REQUEST_TEXT = 0,
        XIGUA_AI_REQUEST_VOICE,
    } kind;
    char prompt[XIGUA_AI_PROMPT_MAX];
} xigua_ai_request_t;

typedef struct {
    esp_err_t error;
    char text[XIGUA_AI_RESPONSE_MAX];
} xigua_ai_result_t;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} xigua_ai_body_t;

static void log_http_connect_diagnostics(esp_http_client_handle_t client, const char *stage,
                                         esp_err_t err)
{
    int tls_code = 0;
    int tls_flags = 0;
    esp_err_t tls_err = client ? esp_http_client_get_and_clear_last_tls_error(
        client, &tls_code, &tls_flags) : ESP_ERR_INVALID_ARG;
    int socket_errno = client ? esp_http_client_get_errno(client) : -1;
    ESP_LOGE(TAG, "MiMo HTTP %s failed err=%s socket_errno=%d(%s) tls=%s code=0x%x flags=0x%x",
             stage ? stage : "request", esp_err_to_name(err), socket_errno,
             socket_errno > 0 ? strerror(socket_errno) : "none",
             esp_err_to_name(tls_err), (unsigned)tls_code, (unsigned)tls_flags);
}

static QueueHandle_t s_requests;
static QueueHandle_t s_results;
static TaskHandle_t s_task;
static volatile xigua_ai_voice_phase_t s_voice_phase;
static volatile bool s_voice_stop;
static volatile xigua_ai_health_t s_health = XIGUA_AI_HEALTH_OFFLINE;

#define XIGUA_AI_HEALTH_SUCCESS_INTERVAL_US (6LL * 60 * 60 * 1000000)
#define XIGUA_AI_HEALTH_RETRY_INTERVAL_US (2LL * 60 * 1000000)

static esp_err_t http_event(esp_http_client_event_t *event)
{
    xigua_ai_body_t *body = event ? event->user_data : NULL;
    if (!body || event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
    size_t incoming = (size_t)event->data_len;
    if (body->length + incoming + 1 > body->capacity) return ESP_ERR_NO_MEM;
    memcpy(body->data + body->length, event->data, incoming);
    body->length += incoming;
    body->data[body->length] = '\0';
    return ESP_OK;
}

static esp_err_t build_request(const char *prompt, char **payload)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *messages = cJSON_CreateArray();
    cJSON *system = cJSON_CreateObject();
    cJSON *user = cJSON_CreateObject();
    if (!root || !messages || !system || !user) {
        cJSON_Delete(root); cJSON_Delete(messages); cJSON_Delete(system); cJSON_Delete(user);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "model", XIGUA_AI_CHAT_MODEL);
    cJSON_AddNumberToObject(root, "max_tokens", 256);
    cJSON_AddBoolToObject(root, "enable_thinking", false);
    cJSON_AddBoolToObject(root, "stream", false);
    cJSON_AddStringToObject(system, "role", "system");
    cJSON_AddStringToObject(system, "content",
                            "你是育儿助手。请简短、清楚地回答；不要声称执行了设备未报告成功的操作。");
    cJSON_AddStringToObject(user, "role", "user");
    cJSON_AddStringToObject(user, "content", prompt);
    cJSON_AddItemToArray(messages, system);
    cJSON_AddItemToArray(messages, user);
    cJSON_AddItemToObject(root, "messages", messages);
    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return *payload ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t post_json(const char *payload, char *response, size_t response_size,
                           size_t body_capacity)
{
    if (!payload || !response || response_size == 0) return ESP_ERR_INVALID_ARG;
    ESP_LOGI(TAG, "MiMo request bytes=%u free=%u largest=%u",
             (unsigned)strlen(payload),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (body_capacity == 0) return ESP_ERR_INVALID_ARG;
    char *body_data = calloc(1, body_capacity);
    if (!body_data) return ESP_ERR_NO_MEM;
    xigua_ai_body_t body = { .data = body_data, .capacity = body_capacity };
    esp_http_client_handle_t client = NULL;
    char url[256];
    int written = snprintf(url, sizeof(url), "%s/chat/completions", XIGUA_AI_BASE_URL);
    if (written < 0 || (size_t)written >= sizeof(url)) {
        free(body_data);
        return ESP_ERR_INVALID_SIZE;
    }
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .keep_alive_enable = false,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event,
        .user_data = &body,
    };
    client = esp_http_client_init(&config);
    if (!client) { free(body_data); return ESP_ERR_NO_MEM; }
    // MiMo deployments use the standard OpenAI bearer header. Keep the
    // legacy api-key header as well so older token-plan gateways continue to
    // accept existing device credentials.
    char auth[320];
    int auth_len = snprintf(auth, sizeof(auth), "Bearer %s", XIGUA_AI_API_KEY);
    if (auth_len > 0 && (size_t)auth_len < sizeof(auth)) {
        esp_http_client_set_header(client, "Authorization", auth);
    }
    esp_http_client_set_header(client, "api-key", XIGUA_AI_API_KEY);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, (int)strlen(payload));
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (err != ESP_OK) log_http_connect_diagnostics(client, "perform", err);
    ESP_LOGI(TAG, "MiMo response status=%d err=%s body=%u",
             status, esp_err_to_name(err), (unsigned)body.length);
    if (err == ESP_OK && (status < 200 || status >= 300)) {
        ESP_LOGW(TAG, "MiMo HTTP status %d", status);
        if (body.length > 0) {
            size_t preview = body.length > 240 ? 240 : body.length;
            ESP_LOGW(TAG, "MiMo error body=%.*s", (int)preview, body.data);
        }
        err = status == 401 || status == 403 ? ESP_ERR_INVALID_CRC : ESP_FAIL;
    }
    if (err == ESP_OK) {
        cJSON *root = cJSON_ParseWithLength(body.data, body.length);
        cJSON *choices = root ? cJSON_GetObjectItem(root, "choices") : NULL;
        cJSON *choice = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
        cJSON *message = choice ? cJSON_GetObjectItem(choice, "message") : NULL;
        cJSON *content = message ? cJSON_GetObjectItem(message, "content") : NULL;
        if (!cJSON_IsString(content) || !content->valuestring) {
            err = ESP_ERR_INVALID_RESPONSE;
        } else {
            snprintf(response, response_size, "%s", content->valuestring);
        }
        cJSON_Delete(root);
    }
    esp_http_client_cleanup(client);
    free(body_data);
    return err;
}

static esp_err_t request_once_sized(const char *prompt, char *response, size_t response_size,
                                    size_t body_capacity)
{
    char *payload = NULL;
    esp_err_t err = build_request(prompt, &payload);
    if (err != ESP_OK) return err;
    err = post_json(payload, response, response_size, body_capacity);
    free(payload);
    return err;
}

static esp_err_t request_once(const char *prompt, char *response, size_t response_size)
{
    return request_once_sized(prompt, response, response_size, XIGUA_AI_HTTP_BODY_MAX);
}

static bool probe_public_https(void)
{
    esp_http_client_config_t config = {
        .url = "https://www.baidu.com/", .method = HTTP_METHOD_HEAD,
        .timeout_ms = 8000, .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 512, .buffer_size_tx = 512, .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (err != ESP_OK) log_http_connect_diagnostics(client, "public probe", err);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "public HTTPS probe status=%d err=%s", status, esp_err_to_name(err));
    return err == ESP_OK && status >= 200 && status < 500;
}

static void wait_for_clock_sync(void)
{
    time_t now = time(NULL);
    if (now >= 1700000000) return;
    ESP_LOGI(TAG, "waiting for SNTP before HTTPS probe (epoch=%ld status=%d)",
             (long)now, (int)esp_sntp_get_sync_status());
    const TickType_t step = pdMS_TO_TICKS(1000);
    for (int i = 0; i < 10; ++i) {
        if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED ||
            time(NULL) >= 1700000000) {
            ESP_LOGI(TAG, "SNTP time ready epoch=%ld", (long)time(NULL));
            return;
        }
        vTaskDelay(step);
    }
    ESP_LOGW(TAG, "SNTP time not ready; continuing HTTPS probe epoch=%ld",
             (long)time(NULL));
}

static xigua_ai_health_t run_health_check(void)
{
    ESP_LOGI(TAG, "background network/model self-check started free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "TLS heap internal free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    wait_for_clock_sync();
    if (!probe_public_https()) return XIGUA_AI_HEALTH_NETWORK_FAILED;
    if (!xigua_ai_configured()) return XIGUA_AI_HEALTH_MODEL_FAILED;
    char answer[64] = { 0 };
    esp_err_t err = request_once_sized("只回复OK", answer, sizeof(answer), 2048);
    ESP_LOGI(TAG, "MiMo text self-check err=%s answer_bytes=%u", esp_err_to_name(err),
             (unsigned)strlen(answer));
    return err == ESP_OK && answer[0] ? XIGUA_AI_HEALTH_READY : XIGUA_AI_HEALTH_MODEL_FAILED;
}

static void wav_put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xffU);
    dst[1] = (uint8_t)(value >> 8);
}

static void wav_put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xffU);
    dst[1] = (uint8_t)((value >> 8) & 0xffU);
    dst[2] = (uint8_t)((value >> 16) & 0xffU);
    dst[3] = (uint8_t)(value >> 24);
}

static void wav_header(uint8_t *wav, size_t pcm_bytes)
{
    memcpy(wav, "RIFF", 4);
    wav_put_u32(wav + 4, (uint32_t)(36 + pcm_bytes));
    memcpy(wav + 8, "WAVEfmt ", 8);
    wav_put_u32(wav + 16, 16);
    wav_put_u16(wav + 20, 1);
    wav_put_u16(wav + 22, XIGUA_AI_VOICE_CHANNELS);
    wav_put_u32(wav + 24, XIGUA_AI_VOICE_HZ);
    wav_put_u32(wav + 28, XIGUA_AI_VOICE_HZ * XIGUA_AI_VOICE_CHANNELS *
                         (XIGUA_AI_VOICE_BITS / 8));
    wav_put_u16(wav + 32, XIGUA_AI_VOICE_CHANNELS * (XIGUA_AI_VOICE_BITS / 8));
    wav_put_u16(wav + 34, XIGUA_AI_VOICE_BITS);
    memcpy(wav + 36, "data", 4);
    wav_put_u32(wav + 40, (uint32_t)pcm_bytes);
}

static const char s_asr_prefix[] =
    "{\"model\":\"mimo-v2.5-asr\",\"messages\":[{\"role\":\"user\"," 
    "\"content\":[{\"type\":\"input_audio\",\"input_audio\":{" 
    "\"data\":\"data:audio/wav;base64,";
static const char s_asr_suffix[] =
    "\"}}]}],\"asr_options\":{\"language\":\"zh\"},\"stream\":false}";

static const esp_partition_t *voice_partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                    XIGUA_AI_VOICE_PARTITION_LABEL);
}

static esp_err_t write_http_all(esp_http_client_handle_t client, const char *data, size_t length)
{
    while (length > 0) {
        int written = esp_http_client_write(client, data, (int)length);
        if (written <= 0) return ESP_FAIL;
        data += written;
        length -= (size_t)written;
    }
    return ESP_OK;
}

static esp_err_t record_voice(size_t *wav_bytes_out)
{
    if (!wav_bytes_out) return ESP_ERR_INVALID_ARG;
    const esp_partition_t *partition = voice_partition();
    if (!partition || partition->size < XIGUA_AI_VOICE_MAX_WAV_BYTES) {
        ESP_LOGE(TAG, "voice flash partition unavailable size=%u",
                 partition ? (unsigned)partition->size : 0U);
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "voice record start max_seconds=%u free=%u largest=%u",
             XIGUA_AI_VOICE_MAX_SECONDS,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    size_t erase_bytes = (XIGUA_AI_VOICE_MAX_WAV_BYTES + 0xFFFU) & ~0xFFFU;
    esp_err_t err = esp_partition_erase_range(partition, 0, erase_bytes);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "voice flash erase failed: %s", esp_err_to_name(err));
        return err;
    }
    err = bsp_audio_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "microphone init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = bsp_audio_set_format(XIGUA_AI_VOICE_HZ, XIGUA_AI_VOICE_BITS,
                               XIGUA_AI_VOICE_CHANNELS);
    if (err == ESP_ERR_INVALID_STATE) {
        err = bsp_audio_wake();
        if (err == ESP_OK) {
            err = bsp_audio_set_format(XIGUA_AI_VOICE_HZ, XIGUA_AI_VOICE_BITS,
                                       XIGUA_AI_VOICE_CHANNELS);
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "microphone format failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t header[XIGUA_AI_WAV_HEADER_BYTES] = { 0 };
    /* Leave the erased header untouched until the final length is known.
     * Programming a zero placeholder would prevent the final 0-to-1 bits. */
    uint8_t pcm[XIGUA_AI_AUDIO_CHUNK_BYTES];
    size_t captured = 0;
    const size_t min_pcm = XIGUA_AI_VOICE_MIN_SECONDS * XIGUA_AI_VOICE_HZ *
                           (XIGUA_AI_VOICE_BITS / 8) * XIGUA_AI_VOICE_CHANNELS;
    while (captured < XIGUA_AI_VOICE_MAX_PCM_BYTES) {
        if (s_voice_stop && captured >= min_pcm) break;
        size_t chunk = XIGUA_AI_VOICE_MAX_PCM_BYTES - captured;
        if (chunk > sizeof(pcm)) chunk = sizeof(pcm);
        err = bsp_audio_read(pcm, chunk);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "microphone read failed at %u/%u: %s",
                     (unsigned)captured, (unsigned)XIGUA_AI_VOICE_MAX_PCM_BYTES,
                     esp_err_to_name(err));
            return err;
        }
        err = esp_partition_write(partition, XIGUA_AI_WAV_HEADER_BYTES + captured, pcm, chunk);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "voice flash write failed at %u: %s", (unsigned)captured,
                     esp_err_to_name(err));
            return err;
        }
        captured += chunk;
    }
    wav_header(header, captured);
    err = esp_partition_write(partition, 0, header, sizeof(header));
    (void)bsp_audio_sleep();
    if (err != ESP_OK) return err;
    *wav_bytes_out = XIGUA_AI_WAV_HEADER_BYTES + captured;
    ESP_LOGI(TAG, "voice record complete seconds=%u bytes=%u",
             (unsigned)(captured / (XIGUA_AI_VOICE_HZ * (XIGUA_AI_VOICE_BITS / 8))),
             (unsigned)*wav_bytes_out);
    return ESP_OK;
}

static esp_err_t asr_stream(size_t wav_bytes, char *transcript, size_t transcript_size)
{
    const esp_partition_t *partition = voice_partition();
    if (!partition || wav_bytes < XIGUA_AI_WAV_HEADER_BYTES ||
        wav_bytes > XIGUA_AI_VOICE_MAX_WAV_BYTES) return ESP_ERR_INVALID_SIZE;
    size_t encoded = 4 * ((wav_bytes + 2) / 3);
    size_t total = strlen(s_asr_prefix) + encoded + strlen(s_asr_suffix);
    uint8_t *input = malloc(XIGUA_AI_VOICE_FLASH_CHUNK_BYTES + 2);
    char *encoded_buf = malloc(XIGUA_AI_VOICE_B64_CHUNK_BYTES);
    char *body_data = calloc(1, XIGUA_AI_ASR_BODY_MAX);
    if (!input || !encoded_buf || !body_data) {
        free(input); free(encoded_buf); free(body_data);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "ASR stream request bytes=%u free=%u largest=%u", (unsigned)total,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    char url[256];
    int written = snprintf(url, sizeof(url), "%s/chat/completions", XIGUA_AI_BASE_URL);
    esp_err_t err = (written < 0 || (size_t)written >= sizeof(url)) ? ESP_ERR_INVALID_SIZE : ESP_OK;
    esp_http_client_handle_t client = NULL;
    if (err == ESP_OK) {
        esp_http_client_config_t config = {
            .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 30000,
            .buffer_size = 2048, .crt_bundle_attach = esp_crt_bundle_attach,
        };
        client = esp_http_client_init(&config);
        if (!client) err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        char auth[320];
        int auth_len = snprintf(auth, sizeof(auth), "Bearer %s", XIGUA_AI_API_KEY);
        if (auth_len > 0 && (size_t)auth_len < sizeof(auth)) {
            esp_http_client_set_header(client, "Authorization", auth);
        }
        esp_http_client_set_header(client, "api-key", XIGUA_AI_API_KEY);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        err = esp_http_client_open(client, (int)total);
        if (err != ESP_OK) log_http_connect_diagnostics(client, "open", err);
    }
    if (err == ESP_OK) err = write_http_all(client, s_asr_prefix, strlen(s_asr_prefix));
    size_t offset = 0, carry = 0;
    while (err == ESP_OK && offset < wav_bytes) {
        size_t room = XIGUA_AI_VOICE_FLASH_CHUNK_BYTES - carry;
        size_t count = wav_bytes - offset;
        if (count > room) count = room;
        err = esp_partition_read(partition, offset, input + carry, count);
        if (err != ESP_OK) break;
        offset += count;
        size_t combined = carry + count;
        size_t complete = combined - (combined % 3U);
        if (complete > 0) {
            size_t output = 0;
            int rc = mbedtls_base64_encode((unsigned char *)encoded_buf,
                                           XIGUA_AI_VOICE_B64_CHUNK_BYTES,
                                           &output, input, complete);
            if (rc != 0) { err = ESP_ERR_INVALID_SIZE; break; }
            err = write_http_all(client, encoded_buf, output);
            carry = combined - complete;
            if (carry) memmove(input, input + complete, carry);
        } else {
            carry = combined;
        }
    }
    if (err == ESP_OK && carry > 0) {
        size_t output = 0;
        int rc = mbedtls_base64_encode((unsigned char *)encoded_buf,
                                       XIGUA_AI_VOICE_B64_CHUNK_BYTES,
                                       &output, input, carry);
        if (rc != 0) err = ESP_ERR_INVALID_SIZE;
        else err = write_http_all(client, encoded_buf, output);
    }
    if (err == ESP_OK) err = write_http_all(client, s_asr_suffix, strlen(s_asr_suffix));
    if (err == ESP_OK) {
        if (esp_http_client_fetch_headers(client) < 0) {
            err = ESP_FAIL;
            log_http_connect_diagnostics(client, "fetch_headers", err);
        }
        size_t body_length = 0;
        while (err == ESP_OK && body_length + 1 < XIGUA_AI_ASR_BODY_MAX) {
            char chunk[1024];
            int read = esp_http_client_read(client, chunk, sizeof(chunk));
            if (read <= 0) break;
            if (body_length + (size_t)read + 1 >= XIGUA_AI_ASR_BODY_MAX) {
                err = ESP_ERR_NO_MEM;
                break;
            }
            memcpy(body_data + body_length, chunk, (size_t)read);
            body_length += (size_t)read;
            body_data[body_length] = '\0';
        }
    }
    int status = client ? esp_http_client_get_status_code(client) : 0;
    ESP_LOGI(TAG, "ASR response status=%d err=%s body=%u", status,
             esp_err_to_name(err), (unsigned)strlen(body_data));
    if (err == ESP_OK && (status < 200 || status >= 300)) {
        ESP_LOGW(TAG, "ASR error body=%.*s", 240, body_data);
        err = status == 401 || status == 403 ? ESP_ERR_INVALID_CRC : ESP_FAIL;
    }
    if (err == ESP_OK) {
        cJSON *root = cJSON_Parse(body_data);
        cJSON *choices = root ? cJSON_GetObjectItem(root, "choices") : NULL;
        cJSON *choice = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
        cJSON *message = choice ? cJSON_GetObjectItem(choice, "message") : NULL;
        cJSON *content = message ? cJSON_GetObjectItem(message, "content") : NULL;
        if (cJSON_IsArray(content)) content = cJSON_GetArrayItem(content, 0);
        cJSON *text = content && cJSON_IsObject(content) ? cJSON_GetObjectItem(content, "text") : content;
        if (!cJSON_IsString(text) || !text->valuestring) err = ESP_ERR_INVALID_RESPONSE;
        else snprintf(transcript, transcript_size, "%s", text->valuestring);
        cJSON_Delete(root);
    }
    if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); }
    free(input); free(encoded_buf); free(body_data);
    return err;
}

static esp_err_t voice_once(char *response, size_t response_size)
{
    size_t wav_bytes = 0;
    s_voice_phase = XIGUA_AI_VOICE_RECORDING;
    esp_err_t err = record_voice(&wav_bytes);
    if (err != ESP_OK) {
        (void)bsp_audio_sleep();
        s_voice_phase = XIGUA_AI_VOICE_IDLE;
        return err;
    }

    char transcript[XIGUA_AI_RESPONSE_MAX] = { 0 };
    s_voice_phase = XIGUA_AI_VOICE_TRANSCRIBING;
    err = asr_stream(wav_bytes, transcript, sizeof(transcript));
    if (err != ESP_OK || !transcript[0]) {
        ESP_LOGE(TAG, "ASR failed: %s transcript_bytes=%u",
                 esp_err_to_name(err), (unsigned)strlen(transcript));
        s_voice_phase = XIGUA_AI_VOICE_IDLE;
        return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
    }

    ESP_LOGI(TAG, "ASR transcript length=%u", (unsigned)strlen(transcript));
    s_voice_phase = XIGUA_AI_VOICE_THINKING;
    err = request_once(transcript, response, response_size);
    if (err != ESP_OK) ESP_LOGE(TAG, "chat request failed: %s", esp_err_to_name(err));
    s_voice_phase = XIGUA_AI_VOICE_IDLE;
    s_voice_stop = false;
    return err;
}

static void ai_task(void *arg)
{
    (void)arg;
    xigua_ai_request_t request;
    bool was_connected = false;
    int64_t next_check_us = 0;
    while (true) {
        if (xQueueReceive(s_requests, &request, pdMS_TO_TICKS(5000)) != pdTRUE) {
            bool connected = xigua_wifi_state() == XIGUA_WIFI_CONNECTED;
            if (!connected) {
                s_health = XIGUA_AI_HEALTH_OFFLINE;
                was_connected = false;
                continue;
            }
            if (!was_connected) next_check_us = 0;
            was_connected = true;
            int64_t now_us = esp_timer_get_time();
            if (now_us < next_check_us) continue;
            s_health = XIGUA_AI_HEALTH_CHECKING;
            s_health = run_health_check();
            next_check_us = esp_timer_get_time() +
                (s_health == XIGUA_AI_HEALTH_READY ? XIGUA_AI_HEALTH_SUCCESS_INTERVAL_US :
                                                     XIGUA_AI_HEALTH_RETRY_INTERVAL_US);
            continue;
        }
        xigua_ai_result_t result = { .error = ESP_FAIL };
        if (!xigua_ai_configured()) result.error = ESP_ERR_INVALID_STATE;
        else if (request.kind == XIGUA_AI_REQUEST_VOICE) {
            result.error = voice_once(result.text, sizeof(result.text));
        } else {
            result.error = request_once(request.prompt, result.text, sizeof(result.text));
        }
        xQueueOverwrite(s_results, &result);
    }
}

esp_err_t xigua_ai_start(void)
{
    if (s_task) return ESP_ERR_INVALID_STATE;
    s_requests = xQueueCreate(1, sizeof(xigua_ai_request_t));
    s_results = xQueueCreate(1, sizeof(xigua_ai_result_t));
    if (!s_requests || !s_results) { xigua_ai_stop(); return ESP_ERR_NO_MEM; }
    s_voice_phase = XIGUA_AI_VOICE_IDLE;
    s_health = XIGUA_AI_HEALTH_OFFLINE;
    if (xTaskCreate(ai_task, "xigua_ai", XIGUA_AI_TASK_STACK, NULL, 4, &s_task) != pdPASS) {
        xigua_ai_stop();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void xigua_ai_stop(void)
{
    if (s_task) { vTaskDelete(s_task); s_task = NULL; }
    (void)bsp_audio_sleep();
    s_voice_stop = true;
    s_voice_phase = XIGUA_AI_VOICE_IDLE;
    if (s_requests) { vQueueDelete(s_requests); s_requests = NULL; }
    if (s_results) { vQueueDelete(s_results); s_results = NULL; }
}

bool xigua_ai_configured(void)
{
    return XIGUA_AI_BASE_URL[0] != '\0' && XIGUA_AI_API_KEY[0] != '\0' &&
           strcmp(XIGUA_AI_API_KEY, "replace-with-mimo-api-key") != 0;
}

const char *xigua_ai_model(xigua_ai_model_t model)
{
    return model < XIGUA_AI_MODEL_COUNT ? s_models[model] : "";
}

esp_err_t xigua_ai_request_text(const char *prompt)
{
    if (!s_requests || !prompt || !prompt[0]) return ESP_ERR_INVALID_ARG;
    xigua_ai_request_t request = { .kind = XIGUA_AI_REQUEST_TEXT };
    size_t len = strnlen(prompt, sizeof(request.prompt));
    if (len >= sizeof(request.prompt)) return ESP_ERR_INVALID_SIZE;
    memcpy(request.prompt, prompt, len + 1);
    return xQueueOverwrite(s_requests, &request) == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t xigua_ai_request_voice(void)
{
    if (!s_requests) return ESP_ERR_INVALID_STATE;
    if (s_voice_phase != XIGUA_AI_VOICE_IDLE) return ESP_ERR_INVALID_STATE;
    s_voice_stop = false;
    xigua_ai_request_t request = { .kind = XIGUA_AI_REQUEST_VOICE };
    if (xQueueOverwrite(s_requests, &request) != pdPASS) return ESP_FAIL;
    s_voice_phase = XIGUA_AI_VOICE_RECORDING;
    return ESP_OK;
}

void xigua_ai_stop_voice(void)
{
    s_voice_stop = true;
}

xigua_ai_voice_phase_t xigua_ai_voice_phase(void)
{
    return s_voice_phase;
}

xigua_ai_health_t xigua_ai_health(void)
{
    return s_health;
}

bool xigua_ai_take_text(char *text, size_t text_size, esp_err_t *error)
{
    if (!s_results || !text || text_size == 0) return false;
    xigua_ai_result_t result;
    if (xQueueReceive(s_results, &result, 0) != pdTRUE) return false;
    if (error) *error = result.error;
    if (result.error == ESP_OK) snprintf(text, text_size, "%s", result.text);
    else text[0] = '\0';
    return true;
}
