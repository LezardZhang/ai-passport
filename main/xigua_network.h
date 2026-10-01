#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/* Short HTTP/ASR transactions share the no-PSRAM TLS budget. Audio streams
 * remain independent so a cloud pause/stop command can reach the player. */
esp_err_t xigua_network_init(void);
bool xigua_network_take(uint32_t timeout_ms);
void xigua_network_give(void);
