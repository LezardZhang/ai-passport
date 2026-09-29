#ifndef XIGUA_AI_PROTOCOL_H
#define XIGUA_AI_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>

#define XG_AI_PROMPT_MAX 512
#define XG_AI_ANSWER_MAX 1024
#define XG_AI_RESPONSE_MAX 8192
#define XG_AI_REQUEST_MAX 4096

typedef enum {
    XG_AI_OK = 0,
    XG_AI_INVALID_CONFIG,
    XG_AI_INVALID_PROMPT,
    XG_AI_NO_MEMORY,
    XG_AI_NETWORK_ERROR,
    XG_AI_TIMEOUT,
    XG_AI_HTTP_ERROR,
    XG_AI_RESPONSE_TOO_LARGE,
    XG_AI_INVALID_RESPONSE,
    XG_AI_TOOLS_REJECTED,
    XG_AI_INCOMPLETE,
} xigua_ai_status_t;

typedef struct {
    xigua_ai_status_t status;
    int http_status;
    bool truncated;
    char answer[XG_AI_ANSWER_MAX + 1];
} xigua_ai_result_t;

/* Pure protocol helpers: no networking, credentials, LVGL, or model actions. */
xigua_ai_status_t xigua_ai_encode(const char *model, const char *prompt,
                                  char *out, size_t capacity);
xigua_ai_status_t xigua_ai_decode(const char *body, size_t length,
                                  xigua_ai_result_t *out);
#endif
