/**
 * statusbar.h - Persistente obere Statusleiste, bewusst kompakt (Nutzer-
 * vorgabe 2026-08-20: "nur die wichtigsten Informationen"):
 * CYBERDECK | Wi-Fi | Server | USB+Target | Akku | Uhrzeit
 *
 * CPU/RAM/SD wurden bewusst entfernt (zu dicht fuer eine staendig
 * sichtbare Leiste) - stehen weiterhin im System-Screen zur Verfuegung.
 * Die statusbar_set_*()-Funktionen werden von den jeweiligen Modulen
 * periodisch mit echten Werten gefuettert - die Statusleiste selbst bleibt
 * reine Anzeige ohne eigene Hardwarezugriffe (Trennung UI/HAL).
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

// Zeigt die reale INA226-Busspannung (siehe system_module.c) - bewusst KEIN
// geschaetzter Prozentwert, da die Entladekurve der NP-F550-Zelle nicht
// verifiziert ist. valid=false zeigt "--" (Sensor nicht erreichbar).
void statusbar_set_battery_voltage(float volts, bool valid);

void statusbar_set_clock(const char *hh_mm);

#ifdef __cplusplus
}
#endif
