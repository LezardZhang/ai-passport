#pragma once
#include "bsp_audio.h"
/* wake() is a no-op before init; cold-start playback must initialize first. */
static inline esp_err_t xigua_audio_output_prepare(uint32_t hz, uint8_t bits, uint8_t channels)
{
    esp_err_t err=bsp_audio_init();
    if (err==ESP_OK) err=bsp_audio_wake();
    if (err==ESP_OK) err=bsp_audio_set_format(hz,bits,channels);
    return err;
}
