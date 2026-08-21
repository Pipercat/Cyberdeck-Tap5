/**
 * usb_device_manager.h - Klassifiziert Geraete, die ueber usb_host_manager.h
 * gemeldet werden, und stellt ihren Zustand fuer UI und Remote-API bereit,
 * OHNE dass UI/Remote den USB-Host direkt anfassen muessen (Architektur
 * Abschnitt 5: "Der Device Manager darf nicht direkt von der UI abhaengen").
 *
 * WICHTIG zur Chip-Erkennung: Espressif-Chips mit eingebautem
 * USB-Serial/JTAG (ESP32-S3/C3/C6/H2/P4) melden ALLE denselben generischen
 * USB-Deskriptor (VID 303A, PID 1001, Produktstring "USB JTAG/serial debug
 * unit") - der USB-Deskriptor allein erlaubt KEINE Unterscheidung zwischen
 * diesen Chip-Familien. usb_target_chip_t/likely_chip ist deshalb bewusst
 * nur ein UI-Hinweis ("Espressif-Chip, vermutlich flashbar"), NICHT die
 * autoritative Chip-Erkennung - die liefert erst esp_loader_get_target_chip()
 * (siehe flash_target.h) nach dem Bootloader-Sync anhand des echten
 * Chip-Magic-Werts. Auf reine USB-Erkennung wird hier bewusst keine falsche
 * "ESP32-S3 erkannt"-Anzeige gebaut (Nutzervorgabe: keine Fake Device
 * Detection, siehe Abschnitt 42).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    USB_BRIDGE_KIND_NONE = 0,      // kein serieller Pfad erkannt
    USB_BRIDGE_KIND_NATIVE_CDC,    // Espressif USB-Serial/JTAG oder natives USB-CDC (voll unterstuetzt)
    USB_BRIDGE_KIND_CP210X,        // Silicon Labs CP210x (Interface erkannt, Treiber siehe docs/usb_host.md)
    USB_BRIDGE_KIND_CH34X,         // WCH CH340/CH341/CH9102 (Interface erkannt, Treiber siehe docs/usb_host.md)
    USB_BRIDGE_KIND_FTDI,          // FTDI FT23x (Interface erkannt, Treiber siehe docs/usb_host.md)
    USB_BRIDGE_KIND_UNKNOWN,       // CDC-Interface vorhanden, aber nicht klassifiziert
} usb_bridge_kind_t;

typedef struct {
    bool     connected;
    uint16_t vid;
    uint16_t pid;
    char     manufacturer[64];
    char     product[64];
    char     serial[64];

    usb_bridge_kind_t bridge_kind;
    const char *bridge_label;      // menschenlesbar, z.B. "Espressif USB-Serial/JTAG"

    // Faehigkeiten - siehe usb_device_manager.c fuer die genaue Herleitung
    // je Bridge-Kind. Bewusst getrennte Flags statt eines einzelnen "ready"-
    // Bools, damit UI/Remote-API praezise Fehlermeldungen zeigen koennen
    // (Abschnitt 24: "Unsupported USB device" vs. "No target connected").
    bool     serial_supported;     // usb_serial.h kann RX/TX oeffnen
    bool     flash_supported;      // flash_target.h kann darueber flashen
} usb_device_manager_target_t;

esp_err_t usb_device_manager_init(void);

// Kopiert den aktuellen Zustand. connected=false, wenn kein Geraet
// angeschlossen ist (restliche Felder dann undefiniert).
void usb_device_manager_get_target(usb_device_manager_target_t *out);

#ifdef __cplusplus
}
#endif
