#include "hal_adc.h"
#include <stdbool.h>
#include "pin_table.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

/**
 * Keine ADC-Kalibrierung verfuegbar: ESP-IDF v5.4.1 hat fuer ESP32-P4 weder
 * Curve- noch Line-Fitting-Schema implementiert (siehe
 * components/esp_adc/esp32p4/include/adc_cali_schemes.h: "Now no scheme
 * supported"). Die mV-Umrechnung ist daher eine simple lineare Naeherung
 * (Rohwert / 4095 * 3300mV fuer ADC_ATTEN_DB_12, siehe Espressif-Doku zur
 * Daempfung), KEINE kalibrierte Messung. Wird im ADC-Modul als solche
 * gekennzeichnet ("ca."-Werte), nicht als praezise Spannung dargestellt.
 */
static const char *TAG = "hal_adc";

#define ADC_FULL_SCALE_MV 3300
#define ADC_MAX_RAW       4095

static adc_oneshot_unit_handle_t s_unit_handle[2] = { NULL, NULL };   // Index 0 = ADC_UNIT_1, 1 = ADC_UNIT_2

static esp_err_t ensure_unit(int unit_idx)
{
    if (s_unit_handle[unit_idx] != NULL) {
        return ESP_OK;
    }
    const adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = (unit_idx == 0) ? ADC_UNIT_1 : ADC_UNIT_2,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &s_unit_handle[unit_idx]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit fehlgeschlagen: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t hal_adc_read(int gpio_num, int *out_raw, int *out_mv)
{
    const pin_descriptor_t *pin = pin_table_find(gpio_num);
    if (pin == NULL || !pin->supports_adc || pin->adc_unit < 1 || pin->adc_unit > 2) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    int unit_idx = pin->adc_unit - 1;

    esp_err_t err = ensure_unit(unit_idx);
    if (err != ESP_OK) {
        return err;
    }

    adc_channel_t channel = (adc_channel_t)pin->adc_channel;
    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,   // voller Eingangsspannungsbereich (~0-3.3V)
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_unit_handle[unit_idx], channel, &chan_cfg);
    if (err != ESP_OK) {
        return err;
    }

    int raw = 0;
    err = adc_oneshot_read(s_unit_handle[unit_idx], channel, &raw);
    if (err != ESP_OK) {
        return err;
    }
    if (out_raw) {
        *out_raw = raw;
    }
    if (out_mv) {
        *out_mv = (raw * ADC_FULL_SCALE_MV) / ADC_MAX_RAW;  // unkalibrierte Naeherung, siehe Dateikopf
    }
    return ESP_OK;
}
