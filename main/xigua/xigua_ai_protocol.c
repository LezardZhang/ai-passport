#include "xigua_ai_protocol.h"
#include "cJSON.h"
#include "xigua_json.h"
#include <stdint.h>
#include <string.h>

static size_t bounded_length(const char *s, size_t limit)
{
    size_t n = 0;
    if (!s) return limit + 1;
    while (n <= limit && s[n]) ++n;
    return n;
}

static bool blank(const char *s)
{
    for (; *s; ++s)
        if (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') return false;
    return true;
}

static bool unique_keys(const cJSON *value)
{
    for (const cJSON *a = value->child; a; a = a->next) {
        if (cJSON_IsObject(value))
            for (const cJSON *b = a->next; b; b = b->next)
                if (!strcmp(a->string, b->string)) return false;
        if (!unique_keys(a)) return false;
    }
    return true;
}

xigua_ai_status_t xigua_ai_encode(const char *model, const char *prompt,
                                  char *out, size_t capacity)
{
    if (!out || !capacity) return XG_AI_NO_MEMORY;
    out[0] = 0;
    size_t pn = bounded_length(prompt, XG_AI_PROMPT_MAX);
    size_t mn = bounded_length(model, 63);
    if (!pn || pn > XG_AI_PROMPT_MAX || !xigua_utf8_valid(prompt, pn) || blank(prompt))
        return XG_AI_INVALID_PROMPT;
    if (!mn || mn > 63 || !xigua_utf8_valid(model, mn) || blank(model)) return XG_AI_INVALID_CONFIG;
    cJSON *root = cJSON_CreateObject();
    if (!root) return XG_AI_NO_MEMORY;
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    cJSON *message = cJSON_CreateObject();
    cJSON *thinking = cJSON_AddObjectToObject(root, "thinking");
    bool ok = messages && message && thinking;
    if (ok) {
        ok = cJSON_AddItemToArray(messages, message);
        if (!ok) cJSON_Delete(message);
    } else cJSON_Delete(message);
    if (ok) ok = cJSON_AddStringToObject(message, "role", "user") &&
                 cJSON_AddStringToObject(message, "content", prompt) &&
                 cJSON_AddStringToObject(root, "model", model) &&
                 cJSON_AddNumberToObject(root, "max_completion_tokens", 256) &&
                 cJSON_AddBoolToObject(root, "stream", false) &&
                 cJSON_AddStringToObject(thinking, "type", "disabled");
    if (capacity > XG_AI_REQUEST_MAX) capacity = XG_AI_REQUEST_MAX;
    if (ok) ok = cJSON_PrintPreallocated(root, out, (int)capacity, false);
    cJSON_Delete(root);
    if (!ok) out[0] = 0;
    return ok ? XG_AI_OK : XG_AI_NO_MEMORY;
}

xigua_ai_status_t xigua_ai_decode(const char *body, size_t length, xigua_ai_result_t *out)
{
    if (!out) return XG_AI_INVALID_RESPONSE;
    out->answer[0] = 0;
    out->truncated = false;
    out->status = XG_AI_INVALID_RESPONSE;
    if (length > XG_AI_RESPONSE_MAX) return out->status = XG_AI_RESPONSE_TOO_LARGE;
    if (!xigua_json_valid(body, length)) return out->status;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(body, length, &end, false);
    if (!root) return out->status;
    if (!cJSON_IsObject(root) || !unique_keys(root)) goto done;
    if (cJSON_GetObjectItemCaseSensitive(root, "error")) goto done;
    const cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) != 1) goto done;
    const cJSON *choice = choices->child;
    if (!cJSON_IsObject(choice)) goto done;
    const cJSON *finish = cJSON_GetObjectItemCaseSensitive(choice, "finish_reason");
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(choice, "message");
    if (!cJSON_IsString(finish) || !cJSON_IsObject(message)) goto done;
    const cJSON *tools = cJSON_GetObjectItemCaseSensitive(message, "tool_calls");
    const cJSON *function = cJSON_GetObjectItemCaseSensitive(message, "function_call");
    if ((tools && !cJSON_IsNull(tools) && !(cJSON_IsArray(tools) && !tools->child)) ||
        (function && !cJSON_IsNull(function)) || !strcmp(finish->valuestring, "tool_calls") ||
        !strcmp(finish->valuestring, "function_call")) {
        out->status = XG_AI_TOOLS_REJECTED;
        goto done;
    }
    if (!strcmp(finish->valuestring, "length")) { out->status = XG_AI_INCOMPLETE; goto done; }
    if (strcmp(finish->valuestring, "stop")) goto done;
    const cJSON *role = cJSON_GetObjectItemCaseSensitive(message, "role");
    const cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");
    if (!cJSON_IsString(role) || strcmp(role->valuestring, "assistant") ||
        !cJSON_IsString(content) || blank(content->valuestring)) goto done;
    size_t n = strlen(content->valuestring);
    if (!xigua_utf8_valid(content->valuestring, n)) goto done;
    size_t copied = 0;
    while (copied < n) {
        size_t width = xigua_utf8_width((const unsigned char *)content->valuestring + copied, n - copied);
        if (copied + width > XG_AI_ANSWER_MAX) break;
        copied += width;
    }
    memcpy(out->answer, content->valuestring, copied);
    out->answer[copied] = 0;
    out->truncated = copied < n;
    out->status = XG_AI_OK;
done:
    cJSON_Delete(root);
    return out->status;
}
