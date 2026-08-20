#include "files_module.h"
#include <ctype.h>

size_t files_module_list(storage_module_entry_t *out, size_t max)
{
    return storage_module_list(out, max);
}

esp_err_t files_module_delete(const char *name)
{
    return storage_module_delete(name);
}

esp_err_t files_module_get_usage(size_t *out_used_bytes, size_t *out_total_bytes)
{
    return storage_module_get_usage(out_used_bytes, out_total_bytes);
}

esp_err_t files_module_read_preview(const char *name, char *buf, size_t buf_len, bool *out_is_text)
{
    if (buf_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t n = 0;
    esp_err_t err = storage_module_read(name, (uint8_t *)buf, buf_len - 1, &n);
    if (err != ESP_OK) {
        return err;
    }
    buf[n] = '\0';

    bool is_text = true;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (!(isprint(c) || c == '\n' || c == '\r' || c == '\t')) {
            is_text = false;
            break;
        }
    }
    if (out_is_text != NULL) {
        *out_is_text = is_text;
    }
    return ESP_OK;
}
