/**
 * usb_host_manager.h - Duenne Schicht ueber ESP-IDFs eingebauter usb_host-
 * Bibliothek (Espressif-Treiber, keine Eigenimplementierung des USB-
 * Protokolls - siehe docs/usb_host.md). Zustaendig fuer:
 *   - usb_host_install()/Client-Registrierung/Daemon-Tasks,
 *   - Connect/Disconnect-Erkennung + Geraete-/Config-Deskriptor-Auslesen,
 *   - Fan-out an registrierte Abonnenten (Device Manager, siehe
 *     usb_device_manager.h), OHNE selbst irgendeine Chip-/Treiberlogik zu
 *     kennen (Trennung Transport-Layer <-> Klassifikation, Architektur
 *     Abschnitt 5/26).
 *
 * Der ESP32-P4 des Tab5 hat einen einzelnen USB-A-Host-Port (siehe
 * docs/pinout.md) - dieses Modul geht von genau einem angeschlossenen
 * Geraet gleichzeitig aus (kein Hub-Fanout gezielt unterstuetzt, wird aber
 * durch die usb_host-Bibliothek selbst nicht ausgeschlossen).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "usb/usb_host.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_HOST_MANAGER_STR_MAX 64

typedef struct {
    uint8_t  dev_addr;
    uint16_t vid;
    uint16_t pid;
    uint8_t  device_class;
    uint8_t  device_subclass;
    uint8_t  device_protocol;
    char     manufacturer[USB_HOST_MANAGER_STR_MAX];
    char     product[USB_HOST_MANAGER_STR_MAX];
    char     serial[USB_HOST_MANAGER_STR_MAX];
    bool     has_cdc_interface;   // mind. ein Interface mit bInterfaceClass==USB_CLASS_COMM
    usb_device_handle_t dev_hdl;  // nur waehrend "connected" gueltig, NICHT ausserhalb cachen
} usb_host_device_info_t;

typedef void (*usb_host_connect_cb_t)(const usb_host_device_info_t *info, void *ctx);
typedef void (*usb_host_disconnect_cb_t)(uint8_t dev_addr, void *ctx);

#define USB_HOST_MANAGER_MAX_CALLBACKS 4

// Installiert usb_host, registriert einen Client und startet Daemon-/Client-
// Event-Tasks. Analog zu wifi_module_init() bewusst NICHT beim Boot in
// main.c aufgerufen, sondern lazy beim ersten Betreten von Flash/Remote-
// Funktionalitaet - USB-Host-Bring-up ist wie der Wi-Fi-SDIO-Link eine
// potenziell riskante fruehe Hardware-Operation (siehe
// docs/hardware_reference.md zur allgemeinen esp_hosted-Empfindlichkeit).
// Idempotent - mehrfacher Aufruf ist ein no-op.
esp_err_t usb_host_manager_init(void);

// Registriert Callbacks fuer Connect/Disconnect-Events (Aufruf im Kontext
// des internen Client-Event-Tasks, NICHT im LVGL-Kontext - Aufrufer muss
// selbst thread-sicher an UI/Remote-Layer weiterreichen, siehe
// usb_device_manager.c fuer das dafuer genutzte Queue-Muster).
esp_err_t usb_host_manager_register_callbacks(usb_host_connect_cb_t on_connect,
                                               usb_host_disconnect_cb_t on_disconnect,
                                               void *ctx);

// true + *out gefuellt, wenn aktuell ein Geraet verbunden ist.
bool usb_host_manager_get_current_device(usb_host_device_info_t *out);

#ifdef __cplusplus
}
#endif
