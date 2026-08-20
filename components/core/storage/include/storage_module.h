/**
 * storage_module.h - SPIFFS-Dateiablage auf der internen Flash-Partition
 * "storage" (siehe partitions.csv, ~2.75 MB).
 *
 * Bewusst SPIFFS auf interner Flash statt SD-Karte: der SD/SDMMC-Pfad hat
 * einen ungeklaerten Konflikt mit esp_hosteds SDIO-Wi-Fi-Transport (beide
 * nutzen SDMMC-nahe Peripherie auf dem ESP32-P4) und liegt deshalb bewusst
 * auf einem separaten Branch, siehe docs/hardware_reference.md. SPIFFS nutzt
 * den internen Flash-Controller (spi_flash/esp_partition) und beruehrt diese
 * Peripherie nicht - kein Konfliktrisiko mit dem C6-Wi-Fi-Link.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_MODULE_MAX_NAME_LEN 64

typedef struct {
    char name[STORAGE_MODULE_MAX_NAME_LEN];
    size_t size_bytes;
} storage_module_entry_t;

// Mountet die SPIFFS-Partition unter /storage (formatiert automatisch, falls
// noch nie initialisiert). Einmalig nach board_init() aufrufen, danach idempotent.
esp_err_t storage_module_init(void);

// true fuer Namen, die als Dateiname sicher sind (kein Pfadtrennzeichen,
// nicht leer, passt in STORAGE_MODULE_MAX_NAME_LEN). Von UI und HTTP-API
// gleichermassen genutzt, um Path-Traversal (z.B. "../") auszuschliessen.
bool storage_module_name_is_safe(const char *name);

// Kopiert bis zu max Eintraege nach out. Rueckgabe: tatsaechliche Anzahl.
size_t storage_module_list(storage_module_entry_t *out, size_t max);

// Liest bis zu buf_len Bytes von name nach buf (fuer Vorschau/kleine
// Downloads - kein Streaming). out_len erhaelt die tatsaechlich gelesene
// Byteanzahl (kann kleiner als die Dateigroesse sein, wenn buf_len zu klein ist).
esp_err_t storage_module_read(const char *name, uint8_t *buf, size_t buf_len, size_t *out_len);

esp_err_t storage_module_delete(const char *name);

// Belegter/gesamter Speicherplatz der Partition in Bytes.
esp_err_t storage_module_get_usage(size_t *out_used_bytes, size_t *out_total_bytes);

#ifdef __cplusplus
}
#endif
