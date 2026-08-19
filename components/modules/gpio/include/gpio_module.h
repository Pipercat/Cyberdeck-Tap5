/**
 * gpio_module.h - GPIO-Control-Logik (Nutzeranforderung 6). Reine Logikschicht,
 * die ausschliesslich ueber hal_gpio.c/hal_pwm.c/hal_adc.c auf Hardware
 * zugreift - kein direkter Treiberzugriff aus der UI (gpio_module_ui.c).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GPIO_MODULE_MODE_UNCONFIGURED = 0,
    GPIO_MODULE_MODE_INPUT,
    GPIO_MODULE_MODE_INPUT_PULLUP,
    GPIO_MODULE_MODE_INPUT_PULLDOWN,
    GPIO_MODULE_MODE_OUTPUT,
    GPIO_MODULE_MODE_PWM,
    GPIO_MODULE_MODE_ADC,
} gpio_module_mode_t;

// Konfiguriert gpio_num fuer den angegebenen Modus (reserviert ueber
// hal_gpio_request, lehnt PIN_ROLE_INTERNAL/UNVERIFIED und nicht
// unterstuetzte Modi ab). Ein vorheriger Modus auf demselben Pin wird
// automatisch freigegeben.
esp_err_t gpio_module_configure(int gpio_num, gpio_module_mode_t mode);

// Gibt den Pin komplett frei (Modus UNCONFIGURED).
esp_err_t gpio_module_release(int gpio_num);

gpio_module_mode_t gpio_module_get_mode(int gpio_num);

// Nur gueltig im Modus OUTPUT.
esp_err_t gpio_module_set_output(int gpio_num, bool level);

// Nur gueltig im Modus INPUT/INPUT_PULLUP/INPUT_PULLDOWN.
esp_err_t gpio_module_read_input(int gpio_num, bool *out_level);

// Nur gueltig im Modus PWM. freq_hz > 0 nur beim ersten Aufruf noetig (Start),
// danach reicht ein Duty-Update (freq_hz wird ignoriert, wenn PWM schon laeuft).
esp_err_t gpio_module_set_pwm(int gpio_num, uint32_t freq_hz, uint8_t duty_percent);

// Nur gueltig im Modus ADC. Siehe hal_adc.h: out_mv ist eine Naeherung, kein
// kalibrierter Messwert.
esp_err_t gpio_module_read_adc(int gpio_num, int *out_raw, int *out_mv);

#ifdef __cplusplus
}
#endif
