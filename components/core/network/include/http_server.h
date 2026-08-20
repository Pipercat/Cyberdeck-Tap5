/**
 * http_server.h - Minimale REST-API ueber esp_http_server: Systemstatus,
 * Log-Auszug und Datei-Verwaltung auf dem SPIFFS-Storage (storage_module).
 *
 * Bewusst ohne Authentifizierung: dieses Projekt ist ein lokales Engineering-
 * Werkzeug, kein Produkt mit Internetexposition - Serial/GPIO/I2C sind auf
 * dem Geraet selbst ebenfalls ohne Zugriffsschutz nutzbar. Nicht fuer den
 * Betrieb in nicht vertrauenswuerdigen Netzen gedacht.
 *
 * Endpunkte:
 *   GET    /api/status        - JSON: Heap/PSRAM/Flash/Temperatur/Akku/Storage
 *   GET    /api/logs          - juengste Log-Zeilen (Klartext)
 *   GET    /api/files         - JSON-Liste der Dateien auf /storage
 *   GET    /api/files/<name>  - Datei herunterladen
 *   POST   /api/files/<name>  - Datei hochladen (Request-Body = Dateiinhalt,
 *                                kein multipart/form-data - z.B.
 *                                "curl --data-binary @lokal.txt \
 *                                 http://<ip>/api/files/lokal.txt")
 *   DELETE /api/files/<name>  - Datei loeschen
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Startet den HTTP-Server auf Port 80 (bindet auf allen Interfaces - sobald
// Wi-Fi eine IP hat, ist die API darueber erreichbar). Idempotent.
esp_err_t http_server_start(void);

bool http_server_is_running(void);

#ifdef __cplusplus
}
#endif
