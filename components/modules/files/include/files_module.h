/**
 * files_module.h - Duenne Logikschicht ueber storage_module (core/storage)
 * fuer den Files-Screen: Listing, kleine Textvorschau, Loeschen.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "storage_module.h"

#ifdef __cplusplus
extern "C" {
#endif

size_t files_module_list(storage_module_entry_t *out, size_t max);
esp_err_t files_module_delete(const char *name);
esp_err_t files_module_get_usage(size_t *out_used_bytes, size_t *out_total_bytes);

// Liest bis zu buf_len-1 Bytes von name und terminiert nullterminiert.
// Liefert in out_is_text, ob der gelesene Ausschnitt aus druckbaren
// ASCII-/Whitespace-Zeichen besteht (fuer eine sinnvolle Textvorschau -
// Binaerdateien werden nicht als Rohbytes im UI angezeigt).
esp_err_t files_module_read_preview(const char *name, char *buf, size_t buf_len, bool *out_is_text);

#ifdef __cplusplus
}
#endif
