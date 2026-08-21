/**
 * flash_target.h - Kapselt Espressifs offizielle esp-serial-flasher-
 * Bibliothek (component registry "espressif/esp-serial-flasher") als
 * Transport-/Flash-Protokoll-Schicht ueber einem angeschlossenen ESP-Chip.
 * Keine eigene Implementierung des seriellen Flash-Protokolls (SLIP-Framing,
 * Stub-Upload etc.) - siehe docs/remote_flashing.md.
 *
 * Diese Schicht implementiert die von esp-serial-flasher geforderten
 * loader_port_*()-Callbacks (siehe esp-serial-flasher "porting guide") ueber
 * usb_serial.h als Transport - dieselbe serielle Verbindung, die auch fuer
 * das Remote-Serial-Terminal genutzt wird (Nutzervorgabe Abschnitt 14: kein
 * zweiter unabhaengiger serieller Pfad).
 *
 * Bootloader-Einstieg/Reset nutzen die klassische DTR/RTS-Auto-Reset-
 * Sequenz (DTR->GPIO0, RTS->EN, ueber NPN-Inverter aktiv-low), wie sie
 * esptool.py und praktisch alle ESP32-Dev-Boards mit USB-UART-Bruecke
 * verwenden - eine breit etablierte Konvention, keine geraetespezifisch
 * geratene Pinbelegung. Funktioniert die automatische Sequenz auf einem
 * konkreten Board nicht (z.B. ohne Auto-Program-Schaltung), meldet
 * flash_target_connect() FLASH_TARGET_ERR_SYNC_TIMEOUT - die UI zeigt dann
 * "Manual bootloader entry required" statt eine falsche Erfolgsmeldung
 * (Nutzervorgabe Abschnitt 15).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLASH_TARGET_CHIP_UNKNOWN = 0,
    FLASH_TARGET_CHIP_ESP32,
    FLASH_TARGET_CHIP_ESP32S2,
    FLASH_TARGET_CHIP_ESP32S3,
    FLASH_TARGET_CHIP_ESP32C2,
    FLASH_TARGET_CHIP_ESP32C3,
    FLASH_TARGET_CHIP_ESP32C5,
    FLASH_TARGET_CHIP_ESP32C6,
    FLASH_TARGET_CHIP_ESP32H2,
    FLASH_TARGET_CHIP_ESP32P4,
} flash_target_chip_t;

const char *flash_target_chip_name(flash_target_chip_t chip);

// Fuehrt die Auto-Reset-Sequenz aus und synchronisiert mit dem ROM-
// Bootloader (esp_loader_connect()). *out_chip wird anhand des vom Chip
// selbst gemeldeten Magic-Werts gesetzt - das ist die einzige autoritative
// Chip-Erkennung im System (siehe usb_device_manager.h-Kommentar dazu, warum
// der USB-Deskriptor dafuer NICHT ausreicht).
esp_err_t flash_target_connect(uint32_t baud_rate, flash_target_chip_t *out_chip);

// Startet den Schreibvorgang fuer einen zusammenhaengenden Speicherbereich
// (i.d.R. eine .bin-Datei aus dem Flash-Manifest, siehe flash_manager.h).
esp_err_t flash_target_begin_region(uint32_t address, uint32_t total_size);

// Schreibt einen Block innerhalb des zuletzt mit begin_region() gestarteten
// Bereichs. len darf variieren, muss aber in Summe genau total_size ergeben.
esp_err_t flash_target_write_chunk(const uint8_t *data, size_t len);

// Schliesst den aktuellen Bereich ab (MD5-Verify ueber esp_loader_flash_verify(),
// sofern vom Ziel unterstuetzt).
esp_err_t flash_target_end_region(void);

// Beendet die Bootloader-Sitzung. reboot=true startet die neue Firmware
// sofort (esp_loader_flash_finish()).
esp_err_t flash_target_finish(bool reboot);

// Reine Reset-Sequenz ohne Bootloader-Sync (fuer POST /device/reset, auch
// ausserhalb eines Flash-Vorgangs nutzbar).
esp_err_t flash_target_reset_normal(void);

// Fuehrt nur die DTR/RTS-Bootloader-Einstiegssequenz aus, OHNE danach mit
// esp_loader_connect() zu synchronisieren (fuer POST /device/bootloader -
// Nutzeranforderung 15, "Bootloader-Modus steuern" als eigenstaendige
// Aktion). Meldet ESP_ERR_TIMEOUT nicht - ob der Chip tatsaechlich im
// Bootloader ist, wird hier bewusst NICHT behauptet (keine Sync-Pruefung),
// da genau das den Unterschied zu flash_target_connect() ausmacht.
esp_err_t flash_target_enter_bootloader_only(void);

// Gibt die USB-Serial-Verbindung frei (Cleanup nach Fehler/Abbruch).
void flash_target_disconnect(void);

#ifdef __cplusplus
}
#endif
