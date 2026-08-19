/**
 * wifi_module.h - Wi-Fi-STA-Logik ueber den ESP32-C6-Co-Prozessor.
 *
 * Das Funkmodul sitzt NICHT auf dem ESP32-P4, sondern auf dem separaten
 * ESP32-C6-MINI-1U, angebunden per SDIO2 (siehe pin_table.c GPIO 8-13/15).
 * esp_wifi_remote (Kconfig: CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED) stellt
 * dafuer einen transparenten esp_wifi.h-Ersatz bereit - dieses Modul nutzt
 * daher ganz normale ESP-IDF-Wi-Fi-APIs, keine esp_hosted-spezifischen
 * Aufrufe. Verifiziert gegen die offizielle M5Tab5-UserDemo-Firmware, siehe
 * docs/hardware_reference.md.
 *
 * Passwoerter werden NICHT persistiert (siehe settings.h-Kommentar zu
 * settings_t.wifi_ssid): NVS-Verschluesselung ist bewusst noch nicht
 * aktiviert, ein Klartext-Passwort in der Settings-Blob waere unsicher.
 * Nur die SSID wird nach erfolgreicher Verbindung gespeichert (Komfort:
 * Feld vorausgefuellt), das Passwort muss pro Boot neu eingegeben werden.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MODULE_SSID_MAX_LEN 32
#define WIFI_MODULE_MAX_SCAN_RESULTS 20

typedef enum {
    WIFI_MODULE_STATE_DISCONNECTED = 0,
    WIFI_MODULE_STATE_CONNECTING,
    WIFI_MODULE_STATE_CONNECTED,
    WIFI_MODULE_STATE_FAILED,
} wifi_module_state_t;

typedef struct {
    char    ssid[WIFI_MODULE_SSID_MAX_LEN + 1];
    int8_t  rssi_dbm;
    bool    secured;
} wifi_module_scan_result_t;

// Initialisiert netif/Event-Loop/esp_wifi im STA-Modus. Erwartet, dass
// nvs_flash_init() bereits gelaufen ist (siehe settings_init() in main.c).
// Muss genau einmal aufgerufen werden, bevor andere wifi_module_*-Funktionen
// genutzt werden.
esp_err_t wifi_module_init(void);

// Stoesst einen asynchronen Scan an (non-blocking). Ergebnis ueber
// wifi_module_get_scan_results() abholen, sobald wifi_module_is_scanning()
// wieder false liefert.
esp_err_t wifi_module_start_scan(void);
bool wifi_module_is_scanning(void);

// Kopiert bis zu max Ergebnisse des letzten abgeschlossenen Scans nach out,
// absteigend nach Signalstaerke sortiert (wie von esp_wifi_scan_get_ap_records
// geliefert). Rueckgabe: tatsaechliche Anzahl.
size_t wifi_module_get_scan_results(wifi_module_scan_result_t *out, size_t max);

// Verbindet mit dem angegebenen Netz (asynchron). password darf NULL/leer
// sein fuer offene Netze. Bei Erfolg wird die SSID (nicht das Passwort) in
// den Settings persistiert.
esp_err_t wifi_module_connect(const char *ssid, const char *password);
void wifi_module_disconnect(void);

wifi_module_state_t wifi_module_get_state(void);
const char *wifi_module_get_connected_ssid(void);
int8_t wifi_module_get_rssi(void);

// Formatiert die aktuelle IPv4-Adresse nach buf (z.B. "192.168.1.42").
// Liefert false, wenn nicht verbunden/keine IP zugewiesen.
bool wifi_module_get_ip_str(char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
