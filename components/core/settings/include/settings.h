/**
 * settings.h - Persistente Konfiguration ueber NVS.
 *
 * Schema-Versionierung: SETTINGS_SCHEMA_VERSION wird bei jedem Boot mit dem
 * gespeicherten Wert verglichen. Bei Aenderung greift settings_migrate()
 * (aktuell no-op, da Schema v1 die erste Version ist) - Platzhalter fuer
 * kuenftige Phasen, wenn neue Settings-Felder dazukommen.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_SCHEMA_VERSION 1

typedef struct {
    uint32_t schema_version;
    uint8_t  backlight_percent;
    bool     dark_mode;            // Phase 1: immer true (Dark-Mode-only), Feld fuer spaeter vorbereitet
    char     last_firmware_path[128];
    char     wifi_ssid[33];
    // Zugangsdaten (Wi-Fi-Passwort, Server-Token) werden NICHT hier gespeichert,
    // sondern separat ueber die NVS-Verschluesselung (CONFIG_NVS_ENCRYPTION) in
    // einem eigenen, engeren Namespace - siehe settings_set_secret() (Phase 4).
} settings_t;

// Initialisiert NVS (falls noetig) und laedt die Settings. Muss vor jedem
// anderen settings_*-Aufruf einmalig laufen.
esp_err_t settings_init(void);

// Liefert einen Zeiger auf die aktuell geladenen Settings (read-only Zugriff).
const settings_t *settings_get(void);

// Schreibt die uebergebenen Settings ins RAM-Cache und persistiert nach NVS.
esp_err_t settings_save(const settings_t *settings);

// Setzt alle Settings auf Werkswerte zurueck (RAM + NVS).
esp_err_t settings_reset_defaults(void);

#ifdef __cplusplus
}
#endif
