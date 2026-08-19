/**
 * hal_gpio.h - Einziger erlaubter Zugriffspfad auf GPIOs fuer alle Module.
 *
 * Kernprinzip (siehe Architektur-Plan Abschnitt 5): Jede GPIO-Operation eines
 * Fach-Moduls (GPIO/PWM/ADC/...) MUSS ueber hal_gpio_request() laufen. Diese
 * Funktion lehnt PIN_ROLE_INTERNAL und PIN_ROLE_UNVERIFIED hart ab - dadurch
 * ist der Pin-Schutz an einer einzigen Stelle erzwungen statt in jedem Modul
 * einzeln nachgebaut zu werden.
 */
#pragma once

#include "esp_err.h"
#include "pin_table.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_GPIO_MODE_INPUT,
    HAL_GPIO_MODE_INPUT_PULLUP,
    HAL_GPIO_MODE_INPUT_PULLDOWN,
    HAL_GPIO_MODE_OUTPUT,
    HAL_GPIO_MODE_PWM,
    HAL_GPIO_MODE_ADC,
    HAL_GPIO_MODE_INTERRUPT,
} hal_gpio_mode_t;

/**
 * Prueft und reserviert einen Pin fuer den angeforderten Modus.
 *
 * Rueckgabewerte:
 *   ESP_OK                    - erlaubt, Pin ist ab jetzt fuer diesen Zweck reserviert
 *   ESP_ERR_NOT_ALLOWED        - Pin ist PIN_ROLE_INTERNAL (fest belegt)
 *   ESP_ERR_INVALID_STATE       - Pin ist PIN_ROLE_UNVERIFIED (nicht bestaetigt)
 *   ESP_ERR_NOT_FOUND            - Pin existiert nicht in der Tabelle
 *   ESP_ERR_NOT_SUPPORTED         - Pin unterstuetzt den angeforderten Modus nicht
 *   ESP_ERR_INVALID_ARG            - Pin ist bereits fuer einen anderen Modus reserviert
 */
esp_err_t hal_gpio_request(int gpio_num, hal_gpio_mode_t mode, const char *owner_tag);

/** Gibt einen zuvor reservierten Pin wieder frei. */
esp_err_t hal_gpio_release(int gpio_num);

/** Liefert den aktuellen Besitzer-Tag eines Pins oder NULL, wenn frei. */
const char *hal_gpio_owner(int gpio_num);

#ifdef __cplusplus
}
#endif
