/**
 * usb_serial.h - Serieller Datenpfad ueber ein natives USB-CDC-Geraet
 * (Espressif USB-Serial/JTOG oder natives USB-CDC, siehe
 * usb_device_manager.h). Nutzt Espressifs offizielle cdc_acm_host-
 * Komponente (usb/cdc_acm_host.h) - keine eigene USB-Protokollimplementierung
 * (Nutzervorgabe Abschnitt 4).
 *
 * Wird sowohl vom Flash Manager (Bootloader-Sync/Datentransfer, siehe
 * components/modules/flash/flash_target.c) als auch vom Remote-Serial-
 * Bridge-Pfad (siehe components/core/remote/) genutzt - EIN Datenpfad statt
 * zweier unabhaengiger Implementierungen (Nutzervorgabe Abschnitt 14).
 *
 * Nur EIN geoeffneter Handle gleichzeitig (das CyberDeck hat einen USB-Host-
 * Port fuer genau ein Zielgeraet) - konkurrierender Zugriff (z.B. Flash UI
 * UND Remote Serial gleichzeitig) wird ueber ESP_ERR_INVALID_STATE
 * verhindert, nicht stillschweigend gemischt.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Empfangene Bytes werden zusaetzlich zum Ringpuffer (usb_serial_read()) an
// einen optionalen Live-Callback gereicht, damit die Remote-WebSocket-Bruecke
// Daten ohne Polling-Verzoegerung weiterreichen kann. Laeuft im Kontext des
// internen cdc_acm_host-Event-Tasks - Empfaenger muss selbst thread-sicher
// puffern/weiterreichen (siehe remote_events.c).
typedef void (*usb_serial_rx_cb_t)(const uint8_t *data, size_t len, void *ctx);

// Oeffnet die CDC-Verbindung zum aktuell angeschlossenen Zielgeraet
// (usb_device_manager muss serial_supported==true melden). Installiert
// cdc_acm_host beim allererst Aufruf lazy (analog zu usb_host_manager_init).
esp_err_t usb_serial_open(uint32_t baud_rate);
esp_err_t usb_serial_close(void);
bool usb_serial_is_open(void);

esp_err_t usb_serial_write(const uint8_t *data, size_t len);

// Nicht blockierend: liefert bis zu max_len seit dem letzten Aufruf
// empfangene Bytes aus dem internen Ringpuffer (siehe usb_serial.c fuer
// dessen feste Groesse - kein unbegrenztes Wachstum, Abschnitt 27).
size_t usb_serial_read(uint8_t *buf, size_t max_len);

esp_err_t usb_serial_set_baud(uint32_t baud_rate);

// DTR/RTS-Leitungssteuerung fuer die klassische EN/IO0-Auto-Reset-Sequenz
// (siehe flash_target.c enter_bootloader()/reset_target()).
esp_err_t usb_serial_set_control_lines(bool dtr, bool rts);

esp_err_t usb_serial_register_rx_callback(usb_serial_rx_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
