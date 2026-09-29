#ifndef XIGUA_MANAGEMENT_H
#define XIGUA_MANAGEMENT_H
#include "xigua_config.h"
#define XG_COMMAND_MAX 2048
#define XG_CONFIG_WIRE_MAX 4096
typedef enum { XG_M_STATUS, XG_M_WIFI_SET, XG_M_WIFI_CLEAR, XG_M_AI_SET,
    XG_M_AI_CLEAR, XG_M_TIME_SET, XG_M_AI_ASK, XG_M_AI_TEST,
    XG_M_RECORDS_BEGIN, XG_M_RECORDS_ITEM, XG_M_RECORDS_FINISH } xigua_management_op_t;
typedef struct {
    int id;
    xigua_management_op_t op;
    unsigned slot;
    xigua_wifi_profile_t wifi;
    xigua_ai_config_t ai;
    char timezone[XG_TZ_MAX];
    char text[513];
    uint32_t boot_id, revision;
    uint16_t count, index;
} xigua_management_request_t;
/* Reject unknown/duplicate fields, embedded NUL, trailing data, malformed UTF-8. */
bool xigua_management_parse(const char *json, size_t length, xigua_management_request_t *request);
bool xigua_management_apply(const xigua_management_request_t *request, xigua_config_t *candidate);
/* Update matching SSID or first empty slot, making it highest priority.
 * Returns slot 0..7, -1 for invalid input, -2 when all identities are occupied.
 * Failure leaves the complete candidate unchanged. */
int xigua_management_wifi_upsert(xigua_config_t *candidate, const char *ssid, const char *password);
size_t xigua_config_encode(const xigua_config_t *config, char *json, size_t capacity);
bool xigua_config_decode(xigua_config_t *config, const char *json, size_t length);
#endif
