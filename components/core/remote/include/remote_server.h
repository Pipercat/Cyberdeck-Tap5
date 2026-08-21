/**
 * remote_server.h - HTTP-REST + WebSocket-Server fuer den Remote-Zugriff
 * (Architektur Abschnitt 10/11). Nutzt ausschliesslich esp_http_server
 * (Teil von ESP-IDF, laeuft ueber lwip/Sockets - KEIN SPIFFS/VFS-Mount, kein
 * Dateisystem noetig, siehe docs/remote_flashing.md zum dokumentierten
 * VFS-Crash-Verdacht).
 *
 * Der Server wird NICHT automatisch beim Boot gestartet (Nutzeranforderung
 * 16: "Remote Access soll standardmaessig deaktivierbar sein" + Abschnitt 37
 * "niemals unauthenticated /flash im normalen WLAN"). remote_server_init()
 * registriert lediglich einen IP_EVENT-Handler, der den Server automatisch
 * (re-)startet, sobald eine IP vorhanden ist UND settings()->remote_access_enabled
 * gesetzt ist - WLAN-Verbindungsaufbau selbst bleibt weiterhin die
 * bestehende, bewusst manuelle wifi_module_init()-Grenze (siehe
 * network_module_ui.c), nicht Teil dieser Aenderung.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REMOTE_SERVER_PORT 80

esp_err_t remote_server_init(void);

// Direkter Start/Stop, z.B. vom Settings-Toggle (sofortige Wirkung,
// unabhaengig vom automatischen IP_EVENT-Trigger).
esp_err_t remote_server_start(void);
esp_err_t remote_server_stop(void);

bool remote_server_is_running(void);
size_t remote_server_get_client_count(void);
int64_t remote_server_get_last_activity_us(void);  // 0 = noch keine Aktivitaet

#ifdef __cplusplus
}
#endif
