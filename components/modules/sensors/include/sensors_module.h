/**
 * sensors_module.h - Interne Sensoren (Nutzeranforderung: "Sensor Dashboard").
 *
 * Aktuell: RTC (RX8130CE, I2C 0x32) - echtes Register-Readout, verifiziert
 * gegen die offizielle M5Tab5-UserDemo-Firmware (hal/utils/rx8130/rx8130.cpp),
 * kein erfundenes Format.
 *
 * IMU (BMI270) ist NICHT enthalten: der Chip braucht zwingend einen ueber I2C
 * hochgeladenen Firmware-Blob (Bosch BMI2 SensorAPI, ~30k Zeilen Vendor-Code)
 * fuer jegliche Datenausgabe - ohne dieses Init liefert er keine sinnvollen
 * Werte. Ein minimaler Register-Read (wie bei RX8130/INA226) funktioniert
 * hier nicht. Absichtlich zurueckgestellt statt eine unvollstaendige/geratene
 * Teilimplementierung zu zeigen - siehe docs/hardware_reference.md.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool ok;
    int year;    // z.B. 2026
    int month;   // 1-12
    int day;     // 1-31
    int hour;    // 0-23
    int minute;  // 0-59
    int second;  // 0-59
} sensors_module_rtc_t;

// Liest die aktuelle Uhrzeit vom RX8130CE. out->ok = false, wenn der Chip
// nicht erreichbar ist (dann bleiben die uebrigen Felder auf 0, keine
// Platzhalterwerte).
esp_err_t sensors_module_read_rtc(sensors_module_rtc_t *out);

#ifdef __cplusplus
}
#endif
