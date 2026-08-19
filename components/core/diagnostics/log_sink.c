#include "log_sink.h"
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static char *s_buffer = NULL;
static size_t s_capacity = 0;
static size_t s_write_pos = 0;
static bool s_full = false;
static SemaphoreHandle_t s_mutex = NULL;
static vprintf_like_t s_original_vprintf = NULL;

static int sink_vprintf(const char *fmt, va_list args)
{
    char line[256];
    va_list args_copy;
    va_copy(args_copy, args);
    int written = vsnprintf(line, sizeof(line), fmt, args_copy);
    va_end(args_copy);

    if (written > 0 && s_buffer != NULL && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        size_t len = (size_t)written < sizeof(line) ? (size_t)written : sizeof(line) - 1;
        for (size_t i = 0; i < len; i++) {
            s_buffer[s_write_pos] = line[i];
            s_write_pos = (s_write_pos + 1) % s_capacity;
            if (s_write_pos == 0) {
                s_full = true;
            }
        }
        xSemaphoreGive(s_mutex);
    }

    // Normale Ausgabe (UART/USB-Konsole) bleibt unveraendert erhalten.
    return s_original_vprintf ? s_original_vprintf(fmt, args) : vprintf(fmt, args);
}

esp_err_t log_sink_init(size_t capacity_bytes)
{
    if (capacity_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    s_buffer = (char *)heap_caps_malloc(capacity_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_buffer == NULL) {
        s_buffer = (char *)malloc(capacity_bytes);
    }
    if (s_buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(s_buffer, 0, capacity_bytes);
    s_capacity = capacity_bytes;
    s_write_pos = 0;
    s_full = false;

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        free(s_buffer);
        s_buffer = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_original_vprintf = esp_log_set_vprintf(sink_vprintf);
    return ESP_OK;
}

size_t log_sink_read_recent(char *out, size_t max_len)
{
    if (out == NULL || max_len == 0 || s_buffer == NULL) {
        return 0;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }

    size_t available = s_full ? s_capacity : s_write_pos;
    size_t to_copy = available < max_len - 1 ? available : max_len - 1;
    size_t start = s_full ? s_write_pos : 0;

    for (size_t i = 0; i < to_copy; i++) {
        out[i] = s_buffer[(start + (available - to_copy) + i) % s_capacity];
    }
    out[to_copy] = '\0';

    xSemaphoreGive(s_mutex);
    return to_copy;
}

void log_sink_clear(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(s_buffer, 0, s_capacity);
        s_write_pos = 0;
        s_full = false;
        xSemaphoreGive(s_mutex);
    }
}
