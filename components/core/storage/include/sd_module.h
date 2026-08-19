/**
 * sd_module.h - microSD-Karte (4-Bit-SDIO-Modus).
 *
 * Pins (GPIO39-44) und Mount-Sequenz 1:1 aus der offiziellen M5Tab5-UserDemo-
 * Firmware uebernommen (components/m5stack_tab5/m5stack_tab5.c), nicht
 * geraten - inkl. der nicht offensichtlichen Anforderung, dass die SD-IO-
 * Versorgungsspannung ueber den internen ESP32-P4-LDO-Kanal 4 (LDO_VO4)
 * geschaltet werden muss (sd_pwr_ctrl_new_on_chip_ldo), sonst initialisiert
 * der Kartenslot nicht. Siehe docs/hardware_reference.md.
 *
 * Kein Card-Detect-Pin verdrahtet (GPIO_SDMMC_DET = NC in der Referenz) -
 * "Karte vorhanden" laesst sich nur ueber einen tatsaechlichen Mount-Versuch
 * feststellen, nicht ueber ein separates Signal.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SD_MODULE_MOUNT_POINT "/sdcard"

typedef struct {
    bool mounted;
    char card_name[32];
    uint64_t capacity_bytes;
} sd_module_status_t;

// Versucht, die SD-Karte zu mounten (idempotent - wenn bereits gemountet,
// liefert es sofort den aktuellen Status). Kein Formatierungsversuch bei
// Fehlschlag (format_if_mount_failed = false) - vorhandene Kartendaten
// werden nicht angetastet.
esp_err_t sd_module_mount(sd_module_status_t *out_status);

void sd_module_unmount(void);
bool sd_module_is_mounted(void);

#ifdef __cplusplus
}
#endif
