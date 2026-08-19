#include "audio_module.h"
#include "board_init.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include <math.h>
#include <string.h>

static const char *TAG = "audio_module";

#define AUDIO_I2S_PORT   I2S_NUM_0
#define AUDIO_SAMPLE_RATE 48000

#define I2S_MCLK GPIO_NUM_30
#define I2S_SCLK GPIO_NUM_27
#define I2S_LCLK GPIO_NUM_29
#define I2S_DOUT GPIO_NUM_26  // -> ES8388 (Speaker)
#define I2S_DSIN GPIO_NUM_28  // <- ES7210 (Mikrofon)

static i2s_chan_handle_t s_tx_chan = NULL;
static i2s_chan_handle_t s_rx_chan = NULL;
static const audio_codec_data_if_t *s_i2s_data_if = NULL;
static esp_codec_dev_handle_t s_speaker_dev = NULL;
static esp_codec_dev_handle_t s_mic_dev = NULL;

static TaskHandle_t s_playback_task = NULL;
static volatile bool s_playback_stop_requested = false;

static TaskHandle_t s_mic_task = NULL;
static volatile bool s_mic_running = false;
static float s_mic_rms = 0.0f;
static float s_mic_peak = 0.0f;

typedef struct {
    float freq_hz;
    float end_freq_hz;  // == freq_hz fuer reinen Ton (kein Sweep)
    uint8_t volume_percent;
    uint32_t duration_ms;  // 0 = unbegrenzt (reiner Ton, bis gestoppt)
} playback_params_t;

esp_err_t audio_module_init(void)
{
    if (s_tx_chan != NULL || s_rx_chan != NULL) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan), TAG, "i2s_new_channel");

    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCLK, .bclk = I2S_SCLK, .ws = I2S_LCLK, .dout = I2S_DOUT, .din = I2S_DSIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &std_cfg), TAG, "tx init_std_mode");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_chan), TAG, "tx enable");

    const i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = {
            .sample_rate_hz = AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
            .ws_width = I2S_TDM_AUTO_WS_WIDTH,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = false,
            .big_endian = false,
            .bit_order_lsb = false,
            .skip_mask = false,
            .total_slot = I2S_TDM_AUTO_SLOT_NUM,
        },
        .gpio_cfg = {
            .mclk = I2S_MCLK, .bclk = I2S_SCLK, .ws = I2S_LCLK, .dout = I2S_DOUT, .din = I2S_DSIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(s_rx_chan, &tdm_cfg), TAG, "rx init_tdm_mode");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_chan), TAG, "rx enable");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = AUDIO_I2S_PORT,
        .tx_handle = s_tx_chan,
        .rx_handle = s_rx_chan,
    };
    s_i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (s_i2s_data_if == NULL) {
        return ESP_FAIL;
    }

    i2c_master_bus_handle_t i2c_bus = board_get_system_i2c_bus();

    // --- Lautsprecher (ES8388) ---
    audio_codec_i2c_cfg_t spk_i2c_cfg = {
        .port = 0, .addr = ES8388_CODEC_DEFAULT_ADDR, .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *spk_ctrl_if = audio_codec_new_i2c_ctrl(&spk_i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    es8388_codec_cfg_t es8388_cfg = {
        .ctrl_if = spk_ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .master_mode = false,
        .pa_pin = -1,  // SPK_EN laeuft ueber PI4IOE1 P1, siehe io_expander.c
    };
    const audio_codec_if_t *es8388_dev = es8388_codec_new(&es8388_cfg);
    if (es8388_dev == NULL) {
        ESP_LOGE(TAG, "ES8388-Codec-Init fehlgeschlagen");
        return ESP_FAIL;
    }
    esp_codec_dev_cfg_t spk_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = es8388_dev, .data_if = s_i2s_data_if,
    };
    s_speaker_dev = esp_codec_dev_new(&spk_dev_cfg);

    // --- Mikrofon (ES7210) ---
    audio_codec_i2c_cfg_t mic_i2c_cfg = {
        .port = 0, .addr = ES7210_CODEC_DEFAULT_ADDR, .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *mic_ctrl_if = audio_codec_new_i2c_ctrl(&mic_i2c_cfg);
    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = mic_ctrl_if,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    const audio_codec_if_t *es7210_dev = es7210_codec_new(&es7210_cfg);
    if (es7210_dev == NULL) {
        ESP_LOGE(TAG, "ES7210-Codec-Init fehlgeschlagen");
        return ESP_FAIL;
    }
    esp_codec_dev_cfg_t mic_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = es7210_dev, .data_if = s_i2s_data_if,
    };
    s_mic_dev = esp_codec_dev_new(&mic_dev_cfg);

    ESP_LOGI(TAG, "Audio initialisiert: Speaker=%s Mic=%s",
             s_speaker_dev ? "OK" : "FEHLER", s_mic_dev ? "OK" : "FEHLER");
    return (s_speaker_dev != NULL && s_mic_dev != NULL) ? ESP_OK : ESP_FAIL;
}

static void playback_task(void *arg)
{
    playback_params_t params = *(playback_params_t *)arg;
    vPortFree(arg);

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16, .channel = 1, .channel_mask = 0, .sample_rate = AUDIO_SAMPLE_RATE,
    };
    if (esp_codec_dev_open(s_speaker_dev, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Speaker open fehlgeschlagen");
        s_playback_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    esp_codec_dev_set_out_vol(s_speaker_dev, params.volume_percent);

    #define CHUNK_SAMPLES 256
    int16_t chunk[CHUNK_SAMPLES];
    double phase = 0.0;
    uint32_t elapsed_ms = 0;
    bool sweep = (params.freq_hz != params.end_freq_hz);

    while (!s_playback_stop_requested) {
        if (params.duration_ms > 0 && elapsed_ms >= params.duration_ms) break;

        float progress = (params.duration_ms > 0) ? (float)elapsed_ms / (float)params.duration_ms : 0.0f;
        float cur_freq = sweep ? params.freq_hz + (params.end_freq_hz - params.freq_hz) * progress
                                : params.freq_hz;

        for (int i = 0; i < CHUNK_SAMPLES; i++) {
            chunk[i] = (int16_t)(sinf((float)phase) * 24000.0f);  // ~73% Vollausschlag, vermeidet Clipping
            phase += 2.0 * M_PI * cur_freq / AUDIO_SAMPLE_RATE;
            if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
        }
        esp_codec_dev_write(s_speaker_dev, chunk, sizeof(chunk));
        elapsed_ms += (uint32_t)((1000.0f * CHUNK_SAMPLES) / AUDIO_SAMPLE_RATE);
    }

    esp_codec_dev_close(s_speaker_dev);
    s_playback_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t start_playback_task(playback_params_t params)
{
    audio_module_stop_playback();
    s_playback_stop_requested = false;

    playback_params_t *heap_params = pvPortMalloc(sizeof(playback_params_t));
    if (heap_params == NULL) return ESP_ERR_NO_MEM;
    *heap_params = params;

    BaseType_t ok = xTaskCreate(playback_task, "audio_playback", 4096, heap_params, 5, &s_playback_task);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t audio_module_start_tone(float freq_hz, uint8_t volume_percent)
{
    return start_playback_task((playback_params_t){
        .freq_hz = freq_hz, .end_freq_hz = freq_hz, .volume_percent = volume_percent, .duration_ms = 0,
    });
}

esp_err_t audio_module_start_sweep(float start_hz, float end_hz, uint8_t volume_percent, uint32_t duration_ms)
{
    return start_playback_task((playback_params_t){
        .freq_hz = start_hz, .end_freq_hz = end_hz, .volume_percent = volume_percent, .duration_ms = duration_ms,
    });
}

void audio_module_stop_playback(void)
{
    if (s_playback_task == NULL) return;
    s_playback_stop_requested = true;
    // Task raeumt sich selbst auf (vTaskDelete(NULL)); kurz warten, damit
    // ein nachfolgender Start nicht mit dem alten Task um den Codec konkurriert.
    for (int i = 0; i < 50 && s_playback_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void mic_task(void *arg)
{
    (void)arg;
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16, .channel = 2, .channel_mask = 0, .sample_rate = AUDIO_SAMPLE_RATE,
    };
    if (esp_codec_dev_open(s_mic_dev, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Mic open fehlgeschlagen");
        s_mic_running = false;
        s_mic_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    #define MIC_CHUNK_SAMPLES 512
    int16_t chunk[MIC_CHUNK_SAMPLES];
    while (s_mic_running) {
        int ret = esp_codec_dev_read(s_mic_dev, chunk, sizeof(chunk));
        if (ret != ESP_CODEC_DEV_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        double sum_sq = 0.0;
        int16_t peak = 0;
        for (int i = 0; i < MIC_CHUNK_SAMPLES; i++) {
            int16_t s = chunk[i];
            sum_sq += (double)s * (double)s;
            int16_t a = s < 0 ? -s : s;
            if (a > peak) peak = a;
        }
        float rms = sqrtf((float)(sum_sq / MIC_CHUNK_SAMPLES)) / 32768.0f;
        float peak_f = peak / 32768.0f;
        s_mic_rms = rms;
        if (peak_f > s_mic_peak) s_mic_peak = peak_f;
    }

    esp_codec_dev_close(s_mic_dev);
    s_mic_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t audio_module_start_mic(void)
{
    if (s_mic_running) return ESP_OK;
    s_mic_running = true;
    s_mic_rms = 0.0f;
    s_mic_peak = 0.0f;
    BaseType_t ok = xTaskCreate(mic_task, "audio_mic", 4096, NULL, 5, &s_mic_task);
    if (ok != pdPASS) {
        s_mic_running = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void audio_module_stop_mic(void)
{
    if (!s_mic_running) return;
    s_mic_running = false;
    for (int i = 0; i < 50 && s_mic_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void audio_module_get_mic_level(float *out_rms, float *out_peak)
{
    if (out_rms) *out_rms = s_mic_rms;
    if (out_peak) *out_peak = s_mic_peak;
    s_mic_peak = 0.0f;  // Spitzenwert seit letztem Abruf, dann zuruecksetzen
}
