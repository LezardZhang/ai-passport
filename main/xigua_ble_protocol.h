#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define XIGUA_BLE_FRAME_BYTES 512
#define XIGUA_BLE_WINDOW_US (300LL * 1000000)
typedef struct { char data[XIGUA_BLE_FRAME_BYTES]; size_t length; } xigua_ble_frame_t;
typedef enum { XIGUA_BLE_PARTIAL, XIGUA_BLE_COMPLETE, XIGUA_BLE_INVALID } xigua_ble_frame_result_t;

/* One newline-terminated UTF-8 JSON command, split across acknowledged writes.
 * Reject overflow, embedded NUL, and multiple commands in one write. */
static inline xigua_ble_frame_result_t xigua_ble_frame_feed(xigua_ble_frame_t *frame,
                                                           const uint8_t *data, size_t size)
{
    if (!frame || !data || !size) return XIGUA_BLE_INVALID;
    for (size_t i=0; i<size; ++i) {
        if (!data[i] || (data[i]!='\n' && frame->length >= sizeof(frame->data)-1) ||
            (data[i]=='\n' && (i+1!=size || frame->length==0))) {
            memset(frame,0,sizeof(*frame)); return XIGUA_BLE_INVALID;
        }
        if (data[i]=='\n') { frame->data[frame->length]=0; return XIGUA_BLE_COMPLETE; }
        frame->data[frame->length++]=(char)data[i];
    }
    return XIGUA_BLE_PARTIAL;
}

static inline bool xigua_ble_wifi_valid(const char *ssid, const char *password)
{
    if (!ssid || !password) return false;
    size_t ssid_size=strlen(ssid), password_size=strlen(password);
    if (!ssid_size || ssid_size>31 || (password_size && (password_size<8 || password_size>63))) return false;
    for (size_t i=0;i<ssid_size;++i) if ((uint8_t)ssid[i]<32 || (uint8_t)ssid[i]==127) return false;
    for (size_t i=0;i<password_size;++i) if ((uint8_t)password[i]<32 || (uint8_t)password[i]==127) return false;
    return true;
}

static inline bool xigua_ble_window_valid(int64_t now, int64_t deadline)
{ return deadline>0 && now<deadline; }
