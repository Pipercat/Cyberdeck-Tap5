/**
 * statusbar.h - Persistente obere Statusleiste (Nutzeranforderung 3):
 * Wi-Fi | Server | USB | Target | SD | CPU | RAM | Akku | Uhrzeit
 *
 * Phase 1: Statische Platzhalterwerte, siehe statusbar.c. Ab Phase 2/4
 * werden die statusbar_set_*()-Funktionen von den jeweiligen Modulen
 * (network_tools, system, flash, files) periodisch mit echten Werten
 * gefuettert - die Statusleiste selbst bleibt reine Anzeige ohne eigene
 * Hardwarezugriffe (Trennung UI/HAL, siehe Architektur Abschnitt 4).
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Erstellt die Statusleiste als Kind von parent (i.d.R. lv_layer_top()).
// Rueckgabe: das Statusleisten-Objekt.
lv_obj_t *statusbar_create(lv_obj_t *parent);

void statusbar_set_wifi(int rssi_dbm, bool connected);
void statusbar_set_server(bool connected);
void statusbar_set_usb_target(const char *target_name); // NULL = kein Target
void statusbar_set_sd_present(bool present);
void statusbar_set_cpu_percent(uint8_t percent);
void statusbar_set_ram_percent(uint8_t percent);

// Zeigt die reale INA226-Busspannung (siehe system_module.c) - bewusst KEIN
// geschaetzter Prozentwert, da die Entladekurve der NP-F550-Zelle nicht
// verifiziert ist. valid=false zeigt "--" (Sensor nicht erreichbar).
void statusbar_set_battery_voltage(float volts, bool valid);

void statusbar_set_clock(const char *hh_mm);

#ifdef __cplusplus
}
#endif
