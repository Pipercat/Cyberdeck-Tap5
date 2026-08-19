/**
 * pwm_module.h - PWM/Servo-Test-Logik (Nutzeranforderung 8).
 *
 * Sicherheitsgrenzen (Nutzeranforderung 22): Ein GPIO darf keinen Motor
 * direkt versorgen - dieses Modul erzeugt ausschliesslich ein PWM-Steuer-
 * signal (Servo-Impulse bzw. LED/Logik-PWM), niemals einen Leistungsausgang.
 * Servo-Pulsbreite 500-2500us / 0-180 Grad ist eine gaengige Konvention,
 * KEINE universelle Norm - im UI klar als Richtwert gekennzeichnet, da
 * einzelne Servos abweichende Bereiche erwarten.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PWM_MODULE_SERVO_MIN_US   500
#define PWM_MODULE_SERVO_MAX_US   2500
#define PWM_MODULE_SERVO_PERIOD_US 20000  // 50 Hz, Standard-Servo-Framerate

// Startet PWM auf gpio_num mit gegebener Frequenz/Duty (allgemeiner Modus,
// z.B. LED-Dimmung). Ersetzt einen zuvor auf diesem Pin laufenden Servo-/
// PWM-Betrieb.
esp_err_t pwm_module_start_generic(int gpio_num, uint32_t freq_hz, uint8_t duty_percent);

// Startet Servo-Betrieb (50Hz) auf gpio_num mit gegebener Pulsbreite in us.
esp_err_t pwm_module_start_servo(int gpio_num, uint32_t pulse_us);

// Aendert die Pulsbreite eines bereits per pwm_module_start_servo gestarteten Pins.
esp_err_t pwm_module_set_servo_pulse(int gpio_num, uint32_t pulse_us);

esp_err_t pwm_module_stop(int gpio_num);

// Hilfsfunktion: Pulsbreite (us) <-> Winkel (0-180 Grad), lineare Naeherung
// ueber PWM_MODULE_SERVO_MIN_US/MAX_US.
uint32_t pwm_module_angle_to_pulse_us(uint8_t angle_deg);
uint8_t pwm_module_pulse_us_to_angle(uint32_t pulse_us);

#ifdef __cplusplus
}
#endif
