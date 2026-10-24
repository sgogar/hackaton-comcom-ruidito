#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t audio_init(void);

int16_t audio_mic_sample(void);

// Bloquea hasta capturar n muestras contiguas del mic (SAMPLE_RATE_HZ = 16 kHz).
esp_err_t audio_mic_read_block(int16_t *dst, size_t n, TickType_t timeout);

esp_err_t audio_play_tone(int freq_hz, int duration_ms);
