#pragma once

#include "esp_err.h"

// Wi-Fi depends on NVS; initialize it without erasing user data on failure.
esp_err_t demo_radio_nvs_prepare(void);

// Wi-Fi 默认 STA netif 依赖这两个全局服务。它们按应用生命周期保留。
esp_err_t demo_radio_network_prepare(void);
