#include "xigua_network.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_transaction;
esp_err_t xigua_network_init(void)
{
    if (!s_transaction) s_transaction=xSemaphoreCreateMutex();
    return s_transaction?ESP_OK:ESP_ERR_NO_MEM;
}
bool xigua_network_take(uint32_t timeout_ms)
{
    return s_transaction && xSemaphoreTake(s_transaction,pdMS_TO_TICKS(timeout_ms))==pdTRUE;
}
void xigua_network_give(void)
{
    if (s_transaction) xSemaphoreGive(s_transaction);
}
