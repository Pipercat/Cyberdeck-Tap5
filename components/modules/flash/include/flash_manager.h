/**
 * flash_manager.h - Koordiniert Flash-Vorgaenge oberhalb von flash_target.h
 * (Architektur Abschnitt 7). Eigener FreeRTOS-Task ("flash_worker") fuehrt
 * alle blockierenden esp_loader-Aufrufe aus - weder LVGL noch der Remote-
 * HTTP-Server-Task duerfen dafuer blockieren (Abschnitt 8/26).
 *
 * Streaming statt Zwischenspeichern (Abschnitt 2/13): flash_manager_write_chunk()
 * kopiert hoechstens FLASH_CHUNK_MAX_LEN Bytes in einen einzigen statischen
 * Puffer und blockiert den Aufrufer, bis der Worker-Task diesen Chunk
 * tatsaechlich auf den Zielchip geschrieben hat (Rueckdruck/Backpressure) -
 * es existiert zu keinem Zeitpunkt eine vollstaendige Firmware-Kopie im RAM.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "flash_target.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLASH_STATE_IDLE = 0,
    FLASH_STATE_TARGET_CONNECTED,
    FLASH_STATE_PREPARING,
    FLASH_STATE_ENTERING_BOOTLOADER,
    FLASH_STATE_SYNCING,
    FLASH_STATE_ERASING,
    FLASH_STATE_FLASHING,
    FLASH_STATE_VERIFYING,
    FLASH_STATE_RESETTING,
    FLASH_STATE_SUCCESS,
    FLASH_STATE_ERROR,
    FLASH_STATE_CANCELLED,
} flash_state_t;

typedef enum {
    FLASH_ERR_NONE = 0,
    FLASH_ERR_NO_TARGET,
    FLASH_ERR_UNSUPPORTED_DEVICE,
    FLASH_ERR_TARGET_DISCONNECTED,
    FLASH_ERR_BOOTLOADER_TIMEOUT,
    FLASH_ERR_SYNC_TIMEOUT,
    FLASH_ERR_ERASE_FAILED,
    FLASH_ERR_WRITE_FAILED,
    FLASH_ERR_VERIFY_FAILED,
    FLASH_ERR_UPLOAD_INTERRUPTED,
    FLASH_ERR_INVALID_MANIFEST,
    FLASH_ERR_WRONG_CHIP,
    FLASH_ERR_BUFFER_OVERFLOW,
    FLASH_ERR_OUT_OF_MEMORY,
    FLASH_ERR_CANCELLED_BY_USER,
    FLASH_ERR_INTERNAL,
} flash_error_t;

const char *flash_error_str(flash_error_t err);
const char *flash_state_str(flash_state_t state);

#define FLASH_MANIFEST_MAX_FILES 8
#define FLASH_MANIFEST_MAX_NAME  64
#define FLASH_CHUNK_MAX_LEN      4096

typedef struct {
    char     name[FLASH_MANIFEST_MAX_NAME];
    uint32_t address;
    uint32_t size;
    bool     has_crc32;
    uint32_t crc32;
} flash_manifest_file_t;

typedef struct {
    char     chip_hint[16];       // Client-Angabe, nur informativ (siehe usb_device_manager.h)
    size_t   file_count;
    flash_manifest_file_t files[FLASH_MANIFEST_MAX_FILES];
} flash_manifest_t;

typedef struct {
    flash_state_t state;
    flash_error_t last_error;
    char     current_file[FLASH_MANIFEST_MAX_NAME];
    uint32_t current_address;
    size_t   file_index;
    size_t   file_count;
    uint32_t bytes_written_current_file;
    uint32_t bytes_total_current_file;
    uint64_t bytes_written_total;
    uint64_t bytes_total_all;
    int      progress_percent;
    flash_target_chip_t detected_chip;
    int64_t  started_at_us;
    int64_t  updated_at_us;
} flash_status_t;

typedef void (*flash_manager_status_cb_t)(const flash_status_t *status, void *ctx);

// Startet den Worker-Task, registriert USB-Disconnect-Ueberwachung. Einmalig
// aufrufen (z.B. beim ersten Betreten des Flash-Screens oder bei erstem
// Remote-API-Zugriff - analog zu usb_device_manager_init()).
esp_err_t flash_manager_init(void);

esp_err_t flash_manager_register_status_callback(flash_manager_status_cb_t cb, void *ctx);

// Validiert das Manifest und stoesst PREPARING/ENTERING_BOOTLOADER/SYNCING
// an. Nur aus IDLE/SUCCESS/ERROR/CANCELLED zulaessig.
esp_err_t flash_manager_start(const flash_manifest_t *manifest);

// offset muss exakt bytes_written_current_file entsprechen (monotone,
// lueckenlose Chunks - Sequenzpruefung, Abschnitt 13). len <= FLASH_CHUNK_MAX_LEN.
esp_err_t flash_manager_write_chunk(size_t file_index, uint32_t offset, const uint8_t *data, size_t len);

// Signalisiert "alle Dateien vollstaendig uebertragen" -> RESETTING -> SUCCESS.
esp_err_t flash_manager_finish(void);

esp_err_t flash_manager_cancel(void);

void flash_manager_get_status(flash_status_t *out);

#ifdef __cplusplus
}
#endif
