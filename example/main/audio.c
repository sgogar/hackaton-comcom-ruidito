#include <math.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "audio.h"
#include "board.h"

/*  
    Todo esto esta robado de la docu, para entenderlo bien van a querer leer
    https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2s.html
    Se que es medio gd decirles RTFM pero hay posta demasiadas cosas pasando 
    y no soy bueno explicando.
    La docu tiene muy buenos ejemplos.
*/

static const char *TAG = "audio";

#define SAMPLE_RATE_HZ      16000

#define MIC_CHUNK_FRAMES    256

static i2s_chan_handle_t s_tx;
static i2s_chan_handle_t s_rx;

static volatile int16_t sample;

// Captura de un bloque contiguo de muestras: mic_task lo llena cuando esta armado.
static SemaphoreHandle_t s_cap_semaforo;
static int16_t *volatile s_cap_buf;
static volatile size_t s_cap_len;
static volatile size_t s_cap_pos;


static esp_err_t mic_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_rx), TAG, "new rx channel failed");

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .clk = BOARD_MIC_CLK_IO,
            .din = BOARD_MIC_DOUT_IO,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_pdm_rx_mode(s_rx, &pdm_cfg), TAG, "rx init failed");
    return i2s_channel_enable(s_rx);
}

static void mic_task(void *arg)
{
    int16_t buf[MIC_CHUNK_FRAMES * 2];   // viene intercalado los canales l,r,l,r

    while (1) {
        size_t bytes = 0;
        if (i2s_channel_read(s_rx, buf, sizeof(buf), &bytes, 1000) != ESP_OK) {
            continue;
        }
        int frames = bytes / (2 * sizeof(int16_t));

        // samples puras del canal 0 (mira el 1 si el mic está en el otro slot)
        for (int i = 0; i < frames; i++) {
            sample = buf[i * 2 + 0];
            if (s_cap_buf) {
                s_cap_buf[s_cap_pos++] = sample;
                if (s_cap_pos >= s_cap_len) {
                    s_cap_buf = NULL;
                    xSemaphoreGive(s_cap_semaforo);
                }
            }
        }
    }
}

esp_err_t audio_init(void)
{
    ESP_RETURN_ON_ERROR(mic_init(), TAG, "mic init failed");
    s_cap_semaforo = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_cap_semaforo, ESP_ERR_NO_MEM, TAG, "no mem for capture semaphore");
    ESP_RETURN_ON_FALSE(xTaskCreate(mic_task, "mic", 4096, NULL, 5, NULL) == pdPASS, ESP_ERR_NO_MEM, TAG,
                        "mic task create failed");
    ESP_LOGI(TAG, "speaker (I2S1) and microphone (PDM, I2S0) ready");
    return ESP_OK;
}

int16_t audio_mic_sample(void)
{
    return sample;
}

esp_err_t audio_mic_read_block(int16_t *dst, size_t n, TickType_t timeout)
{
    // Bloqueamos la ejecucion mientras leemos el mic
    s_cap_pos = 0;
    s_cap_len = n;
    s_cap_buf = dst;
    if (xSemaphoreTake(s_cap_semaforo, timeout) != pdTRUE) {
        s_cap_buf = NULL;
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
