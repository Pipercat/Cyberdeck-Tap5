/**
 * adc_module.h - ADC/Mini-Oszilloskop-Logik (Nutzeranforderung 9).
 * Kein Laborersatz - siehe hal_adc.h: mV-Werte sind eine unkalibrierte
 * lineare Naeherung, im UI klar als "ca." zu kennzeichnen.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Reserviert gpio_num fuer ADC-Nutzung (hal_gpio_request). Vorher reservierte
// Pins werden automatisch freigegeben, wenn ein anderer Pin selektiert wird.
esp_err_t adc_module_select(int gpio_num);

// Gibt den aktuell selektierten Pin frei.
void adc_module_release(void);

// Liest den aktuell selektierten Pin. Siehe hal_adc_read fuer Semantik.
esp_err_t adc_module_read(int *out_raw, int *out_mv);

#ifdef __cplusplus
}
#endif
