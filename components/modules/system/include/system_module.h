/**
 * system_module.h - System-Monitor-Logik (Nutzeranforderung 17).
 *
 * Akku: Nur die INA226-Busspannung (Register 0x02, benoetigt keine
 * Kalibrierung) wird gelesen - Strom/Leistung wuerden eine Kalibrierung
 * anhand des Shunt-Widerstands auf der Platine erfordern, dessen Wert nicht
 * aus einer Primaerquelle verifiziert ist (siehe docs/hardware_reference.md).
 * KEIN geschaetzter Akku-Prozentwert - nur die reale Spannung, wie vom
 * Nutzer in Abschnitt 15 explizit gefordert ("reale Messwerte, kein
 * geschaetzter Prozentwert").
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t heap_free_bytes;
    uint32_t heap_total_bytes;
    uint32_t psram_free_bytes;
    uint32_t psram_total_bytes;
    uint32_t flash_size_bytes;
    int64_t  uptime_s;
    float    chip_temp_c;      // Die-Temperatur, NICHT Umgebungstemperatur
    float    battery_voltage_v; // -1.0f wenn INA226 nicht erreichbar
    const char *idf_version;
    const char *project_version;
} system_module_stats_t;

// Initialisiert den Chip-Temperatursensor. Einmalig nach board_init() aufrufen.
esp_err_t system_module_init(void);

// Sammelt alle aktuellen Systemwerte (schnelle, blockierende Aufrufe: Heap-
// Abfragen sind sofort, Temperatur-/INA226-Lesevorgaenge dauern wenige ms).
esp_err_t system_module_get_stats(system_module_stats_t *out_stats);

// Startet das Geraet neu (esp_restart) - vom UI mit Bestaetigungsdialog
// abzusichern, siehe Nutzeranforderung 22 (kritische Aktionen bestaetigen).
void system_module_restart_device(void);

#ifdef __cplusplus
}
#endif
