#pragma once
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
int xTaskCreate(void (*task)(void *), const char *name, uint32_t stack_bytes,
                void *context, unsigned priority, TaskHandle_t *created);
void xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(int clear, uint32_t wait);
uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
