/**
 * log_sink.h - Ringpuffer-Log fuer die On-Device-Anzeige (spaeter: System-Modul
 * "Logs"-Ansicht). Haengt sich per esp_log_set_vprintf() zusaetzlich in die
 * normale ESP_LOG-Ausgabe ein, ohne das gewohnte UART/USB-Log zu ersetzen.
 *
 * SD-Logging ist in Phase 1 vorbereitet (log_sink_enable_sd_mirror), aber
 * mangels verifiziertem SD-Mount in dieser Phase noch nicht aktiv genutzt.
 */
#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialisiert den Ringpuffer und klinkt sich in esp_log ein.
esp_err_t log_sink_init(size_t capacity_bytes);

// Kopiert bis zu max_len Bytes der juengsten Logzeilen in out (0-terminiert).
// Rueckgabe: tatsaechlich kopierte Bytes (ohne Nullterminator).
size_t log_sink_read_recent(char *out, size_t max_len);

// Leert den Ringpuffer.
void log_sink_clear(void);

#ifdef __cplusplus
}
#endif
