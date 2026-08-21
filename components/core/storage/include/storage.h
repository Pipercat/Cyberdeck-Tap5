/**
 * storage.h - Zugriff auf die "storage"-Partition (SPIFFS, siehe
 * partitions.csv) fuer die Files-UI und den Remote-API-Download.
 *
 * Bewusst kein VFS-Pfad-Passthrough nach aussen: storage_build_path()
 * validiert den Dateinamen (keine Pfad-Traversal ueber "/" oder "..")
 * und liefert einen absoluten Pfad unter STORAGE_MOUNT_POINT, den Aufrufer
 * direkt mit fopen()/remove() verwenden. Keine eigene Dateisystem-
 * Abstraktion darueber - SPIFFS ist flach (keine Unterverzeichnisse).
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_MOUNT_POINT "/storage"
#define STORAGE_MAX_NAME_LEN 32

typedef struct {
    char   name[STORAGE_MAX_NAME_LEN];
    size_t size_bytes;
} storage_file_t;

// Mountet SPIFFS auf der "storage"-Partition. Idempotent - mehrfacher Aufruf
// ist ein No-Op. format_if_mount_failed=true (Erststart auf leerer Partition).
esp_err_t storage_init(void);

// Belegter/gesamter Speicherplatz in Bytes. false, wenn storage_init() noch
// nicht erfolgreich war.
bool storage_get_usage(size_t *used_bytes, size_t *total_bytes);

// Listet Dateien alphabetisch, liefert Anzahl (<= max).
size_t storage_list_files(storage_file_t *out, size_t max);

// Baut einen validierten absoluten Pfad aus einem Dateinamen (kein "/", kein
// "..", max. STORAGE_MAX_NAME_LEN-1 Zeichen). false bei ungueltigem Namen.
bool storage_build_path(const char *name, char *out_path, size_t out_len);

esp_err_t storage_delete_file(const char *name);

#ifdef __cplusplus
}
#endif
