/**
 * hal_pwm.h - Gemeinsame LEDC-PWM-Verwaltung fuer GPIO-, PWM/Servo- und
 * andere Module. Kapselt Kanal-/Timer-Zuteilung (ESP32-P4: 8 Kanaele,
 * 4 Timer, siehe soc_caps.h SOC_LEDC_CHANNEL_NUM/SOC_LEDC_TIMER_BIT_WIDTH),
 * damit nicht jedes Modul eigene Buchhaltung braucht.
 *
 * Ruft NICHT hal_gpio_request() auf - das ist Aufgabe des aufrufenden
 * Moduls, bevor es hal_pwm_start() aufruft (Trennung Pin-Erlaubnis vs.
 * Peripherie-Konfiguration).
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Startet PWM auf gpio_num mit freq_hz und duty_percent (0-100). Reserviert
// automatisch einen freien LEDC-Kanal und -Timer (Timer wird bei
// uebereinstimmender Frequenz mit anderen Kanaelen geteilt).
esp_err_t hal_pwm_start(int gpio_num, uint32_t freq_hz, uint8_t duty_percent);

// Aendert den Duty-Cycle eines bereits gestarteten Kanals (0-100 %).
esp_err_t hal_pwm_set_duty_percent(int gpio_num, uint8_t duty_percent);

// Praezise Pulsbreiten-Steuerung in Mikrosekunden (fuer Servos). period_us
// wird als Frequenz an hal_pwm_start uebergeben, pulse_us bestimmt den Duty.
esp_err_t hal_pwm_set_pulse_us(int gpio_num, uint32_t pulse_us, uint32_t period_us);

// Stoppt PWM und gibt Kanal/ggf. Timer wieder frei.
esp_err_t hal_pwm_stop(int gpio_num);

#ifdef __cplusplus
}
#endif
