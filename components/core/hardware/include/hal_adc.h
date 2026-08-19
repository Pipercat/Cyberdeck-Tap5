/**
 * hal_adc.h - Gemeinsamer ADC-Zugriff (oneshot) fuer GPIO- und ADC/Scope-Modul.
 * Nutzt die verifizierte Kanalzuordnung aus pin_table.c (adc_unit/adc_channel).
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Liest gpio_num einmalig aus und liefert sowohl Rohwert (0-4095, 12 Bit)
// als auch eine NAEHERUNGSWEISE Spannung in mV (lineare Skalierung, keine
// eFuse-Kalibrierung - ESP-IDF v5.4.1 hat fuer ESP32-P4 kein Kalibrierschema,
// siehe hal_adc.c). UI-Code MUSS das als "ca."/unkalibriert kennzeichnen.
// out_raw/out_mv duerfen NULL sein, wenn nicht benoetigt. Gibt
// ESP_ERR_NOT_SUPPORTED zurueck, wenn der Pin laut pin_table.c keinen
// ADC-Kanal hat.
esp_err_t hal_adc_read(int gpio_num, int *out_raw, int *out_mv);

#ifdef __cplusplus
}
#endif
