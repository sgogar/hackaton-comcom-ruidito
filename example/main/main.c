#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "audio.h"
#include "buttons.h"
#include "display.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fft.h"


static const char *TAG = "demo";

#define UI_PERIOD_MS 50
#define MIC_SAMPLE_RATE_HZ 16000  // el mismo SAMPLE_RATE_HZ de audio.c
#define SPECTRUM_SHOW_MS 5000
#define CANT_BANDAS_COMP 32             // bandas lineales de 0 a 8 kHz (250 Hz cada una)
#define SIMILARIDAD 0.25f         // una banda "difiere" si se aleja mas de 25% de la calibracion
#define TIEMPO_ENTRE_COMPARACIONES 5
#define FFT_N 32768      


static void draw_key(int x, int y, int size, bool pressed)
{
    if (pressed) {
        display_fill_rect(x, y, size, size, true);
    } else {
        display_fill_rect(x, y, size, size, false);
        display_draw_rect(x, y, size, size);
    }
}


// plot de la FFT (bins 1..n_bins-1, sin DC) en 128 columnas
// Tomamos el maximo de cada bin transformamos a db y graficamos
static void draw_spectrum(const float *spectro, int n_bins)
{
    const int plot_x = 0, plot_y = 10, plot_w = 128, plot_h = 54;
    const float range_db = 60.0f;

    float peak = 1e-9f;
    for (int k = 1; k < n_bins; k++) {
        if (spectro[k] > peak) {
            peak = spectro[k];
        }
    }

    display_clear();
    display_text(0, 0, "FFT - 1 para seguir", false);


    const int usable = n_bins - 1;
    for (int x = 0; x < plot_w; x++) {
        int k0 = 1 + x * usable / plot_w;
        int k1 = 1 + (x + 1) * usable / plot_w;
        float v = 0.0f;
        for (int k = k0; k < k1; k++) {
            if (spectro[k] > v) {
                v = spectro[k];
            }
        }
        float db = 20.0f * log10f(v / peak + 1e-9f); 
        int h = (int)((db + range_db) / range_db * plot_h);
        if (h < 0) {
            h = 0;
        }
        if (h > plot_h) {
            h = plot_h;
        }
        if (h > 0) {
            display_fill_rect(plot_x + x, plot_y + plot_h - h, 1, h, true);
        }
    }
    display_flush();
}

static float *capture_spectrum(void)
{
    // 2 x 128 KB para la parte real e imaginaria
    float *re = malloc(FFT_N * sizeof(float));
    float *im = malloc(FFT_N * sizeof(float));
    if (!re || !im) {
        ESP_LOGE(TAG, "sin memoria para la FFT chauu");
        free(re);
        free(im);
        return NULL;
    }

    int16_t *capture = (int16_t *)im;
    if (audio_mic_read_block(capture, FFT_N, pdMS_TO_TICKS(5000)) != ESP_OK) {
        ESP_LOGE(TAG, "captura del mic fallo");
        free(re);
        free(im);
        return NULL;
    }
    for (int i = 0; i < FFT_N; i++) {
        re[i] = capture[i];
    }
    memset(im, 0, FFT_N * sizeof(float));

    fft_radix2(re, im, FFT_N);

    for (int k = 0; k <= FFT_N / 2; k++) {
        re[k] = sqrtf(re[k] * re[k] + im[k] * im[k]);
    }
    free(im);

    // Achicamos el bloque igual despues se deberia de borrar?
    float *shrunk = realloc(re, (FFT_N / 2 + 1) * sizeof(float));
    return shrunk ? shrunk : re;
}

// promedio de la intensidad de cada banda (250hz)
static void promedio_bandas(const float *spectro, int n_bins, float *bands)
{
    const int usable = n_bins - 1;
    for (int b = 0; b < CANT_BANDAS_COMP; b++) {
        int k0 = 1 + b * usable / CANT_BANDAS_COMP;
        int k1 = 1 + (b + 1) * usable / CANT_BANDAS_COMP;
        float sum = 0.0f;
        for (int k = k0; k < k1; k++) {
            sum += spectro[k];
        }
        bands[b] = sum / (k1 - k0);
    }
}

// cantidad de bandas que difieren en mas de SIMILARIDAD respecto de la calibracion.
// Loguea cada banda: rango en Hz, intensidad calibrada, intensidad actual y % de diferencia.
static int count_different_bands(const float *calib, const float *cur)
{
    const int usable = FFT_N / 2;  // bins 1..N/2, igual que promedio_bandas()
    int different = 0;
    for (int b = 0; b < CANT_BANDAS_COMP; b++) {
        float ref = calib[b] > 1e-9f ? calib[b] : 1e-9f;
        float diff_pct = (cur[b] - calib[b]) / ref * 100.0f;
        bool differs = fabsf(diff_pct) > SIMILARIDAD * 100.0f;
        if (differs) {
            different++;
        }

        int f0 = (1 + b * usable / CANT_BANDAS_COMP) * MIC_SAMPLE_RATE_HZ / FFT_N;
        int f1 = (1 + (b + 1) * usable / CANT_BANDAS_COMP) * MIC_SAMPLE_RATE_HZ / FFT_N;
        ESP_LOGI(TAG, "banda %2d %4d-%4d Hz: calib=%9.1f actual=%9.1f dif=%+7.1f%% %s", b, f0, f1, calib[b], cur[b],
                 diff_pct, differs ? "DIFIERE" : "ok");
    }
    return different;
}

void app_main(void)
{
    bool display_ok = display_init() == ESP_OK;
    if (!display_ok) {
        ESP_LOGE(TAG, "display init failed - chau");
        return;
    }

    ESP_ERROR_CHECK(buttons_init());

    display_clear();
    display_text(10, 8, "Esperando", false);
    display_text(10, 18, "Calibracion", false);
    display_text(10, 36, "Usar boton 1", false);
    display_flush();

    bool audio_ok = audio_init() == ESP_OK;

    if (!audio_ok) {
        ESP_LOGE(TAG, "audio failed - chau");
        return;
    }

    QueueHandle_t events = buttons_event_queue();

    button_event_t ev;

    while (1){
        // Esperamos Btn1
        if (xQueueReceive(events, &ev, portMAX_DELAY)) {
            ESP_LOGI(TAG, "button %s %s", buttons_name(ev.id), ev.pressed ? "pressed" : "released");
            if (ev.pressed && ev.id == BTN_KEY1) {
                // Empieza calibracion
                break;
            }
        }
    }

    // Calibración: una captura y una FFT. Guardamos el grafico y el promedio por banda.
    display_clear();
    display_text(10, 8, "Calibrando...", false);
    display_flush();

    float *calib_spectrum = capture_spectrum();
    if (!calib_spectrum) {
        ESP_LOGE(TAG, "calibracion fallo - chau");
        return;
    }
    ESP_LOGI(TAG, "Calibración terminada: FFT de %d muestras, %d bins", FFT_N, FFT_N / 2 + 1);

    display_clear();
    display_text(10, 8, "Calibracion lista", false);
    display_flush();
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Mostramos el grafico de la FFT
    draw_spectrum(calib_spectrum, FFT_N / 2 + 1);

    // Nos quedamos solo con el vector de bandas y liberamos la FFT
    float calib_bands[CANT_BANDAS_COMP];
    promedio_bandas(calib_spectrum, FFT_N / 2 + 1, calib_bands);
    free(calib_spectrum);
    calib_spectrum = NULL;

    // Esperamos Btn1
    while (1){
        if (xQueueReceive(events, &ev, portMAX_DELAY)) {
            ESP_LOGI(TAG, "button %s %s", buttons_name(ev.id), ev.pressed ? "pressed" : "released");
            if (ev.pressed && ev.id == BTN_KEY1) {
                break;
            }
        }
    }

    // ciclo de comparaciones, volvemos a tomar la tff y hacemos la comparacion por bandas
    while (1) {
        char line[24];

        for (int s = TIEMPO_ENTRE_COMPARACIONES; s > 0; s--) {
            display_clear();
            display_text(10, 8, "Nuevo ciclo en", false);
            snprintf(line, sizeof(line), "%d s", s);
            display_text(10, 24, line, false);
            display_flush();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        display_clear();
        display_text(10, 8, "Midiendo...", false);
        display_flush();

        float *spectrum = capture_spectrum();
        if (!spectrum) {
            display_clear();
            display_text(10, 8, "Error de captura", false);
            display_flush();
            continue;
        }

        draw_spectrum(spectrum, FFT_N / 2 + 1);
        vTaskDelay(pdMS_TO_TICKS(SPECTRUM_SHOW_MS));

        float bands[CANT_BANDAS_COMP];
        promedio_bandas(spectrum, FFT_N / 2 + 1, bands);
        free(spectrum);

        int different = count_different_bands(calib_bands, bands);
        ESP_LOGI(TAG, "ciclo: %d/%d bandas difieren mas de %.0f%%", different, CANT_BANDAS_COMP, SIMILARIDAD * 100);

        display_clear();
        display_text(10, 8, different == 0 ? "SIMILAR" : "DISTINTO", false);
        snprintf(line, sizeof(line), "%d/%d bandas >%d%%", different, CANT_BANDAS_COMP, (int)(SIMILARIDAD * 100));
        display_text(10, 24, line, false);
        display_flush();
        vTaskDelay(pdMS_TO_TICKS(SPECTRUM_SHOW_MS));
    }
}
