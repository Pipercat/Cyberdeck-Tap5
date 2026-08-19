/**
 * i2c_module.h - I2C-Scanner-Logik (Nutzeranforderung 10) fuer den externen
 * Bus an Port A (GPIO 53 SDA / GPIO 54 SCL, siehe pin_table.c).
 *
 * Sicherheit: Geraeteidentitaet wird NIE als sicher dargestellt, nur als
 * Vermutung anhand der Adresse (siehe i2c_module_guess_device_name) -
 * exakte Nutzeranforderung 10.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    I2C_MODULE_SPEED_100K = 100000,
    I2C_MODULE_SPEED_400K = 400000,
} i2c_module_speed_t;

// Reserviert GPIO 53/54 und initialisiert den externen I2C-Bus. Erneuter
// Aufruf mit anderer Geschwindigkeit baut den Bus neu auf.
esp_err_t i2c_module_init(i2c_module_speed_t speed);

void i2c_module_deinit(void);

// Sondiert Adressen 0x08-0x77 (7-Bit-Standardbereich). found_addrs muss
// Platz fuer mindestens 120 Eintraege bieten.
esp_err_t i2c_module_scan(uint8_t *found_addrs, size_t max_count, size_t *out_count);

esp_err_t i2c_module_read_reg(uint8_t addr, uint8_t reg, uint8_t *out_val);
esp_err_t i2c_module_write_reg(uint8_t addr, uint8_t reg, uint8_t val);

// Liefert einen Vermutungstext ("moegl. OLED", ...) oder NULL, wenn die
// Adresse keinem bekannten Muster entspricht. NIE als gesicherte Aussage
// im UI darstellen - immer mit "vermutlich"/"moegl." kennzeichnen.
const char *i2c_module_guess_device_name(uint8_t addr);

#ifdef __cplusplus
}
#endif
