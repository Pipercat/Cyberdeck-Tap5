#include "usb_serial.h"
#include <string.h>
#include <inttypes.h>
#include "usb_host_manager.h"
#include "usb_device_manager.h"
#include "usb/cdc_acm_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "USB_SERIAL";

// Fester Ringpuffer statt unbegrenztem Wachstum (Nutzervorgabe Abschnitt 27
// "Speicherverwaltung" / Abschnitt 13 "keine unkontrollierten malloc-
// Schleifen"). 8 KB reicht fuer Bootloader-Antworten (esp_loader-Frames sind
// klein) und typische Log-Ausgabe-Bursts zwischen zwei Poll-Zyklen.
#define RX_RING_SIZE 8192

static bool s_cdc_driver_installed = false;
static cdc_acm_dev_hdl_t s_dev_hdl = NULL;
static bool s_open = false;

static uint8_t s_rx_ring[RX_RING_SIZE];
static size_t s_rx_head = 0;   // naechster Schreib-Index
static size_t s_rx_count = 0;  // Anzahl gueltiger Bytes
static SemaphoreHandle_t s_rx_mutex = NULL;

static usb_serial_rx_cb_t s_rx_cb = NULL;
static void *s_rx_cb_ctx = NULL;

static void rx_ring_push(const uint8_t *data, size_t len)
{
    xSemaphoreTake(s_rx_mutex, portMAX_DELAY);
    for (size_t i = 0; i < len; i++) {
        if (s_rx_count >= RX_RING_SIZE) {
            // Puffer voll: aeltestes Byte verwerfen statt zu blockieren oder
            // unbegrenzt zu wachsen - ein Log einmalig pro Overflow-Episode
            // waere sinnvoll, aber bewusst kein Log-Spam pro Byte (Abschnitt 28).
            s_rx_head = (s_rx_head + 1) % RX_RING_SIZE;
            s_rx_count--;
        }
        size_t write_idx = (s_rx_head + s_rx_count) % RX_RING_SIZE;
        s_rx_ring[write_idx] = data[i];
        s_rx_count++;
    }
    xSemaphoreGive(s_rx_mutex);
}

static size_t rx_ring_pop(uint8_t *out, size_t max_len)
{
    xSemaphoreTake(s_rx_mutex, portMAX_DELAY);
    size_t n = (s_rx_count < max_len) ? s_rx_count : max_len;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_rx_ring[(s_rx_head + i) % RX_RING_SIZE];
    }
    s_rx_head = (s_rx_head + n) % RX_RING_SIZE;
    s_rx_count -= n;
    xSemaphoreGive(s_rx_mutex);
    return n;
}

// Laeuft im cdc_acm_host-eigenen Event-Task-Kontext - siehe usb/cdc_acm_host.h.
// Rueckgabe true = Daten wurden konsumiert (Standard-Kontrakt der Komponente).
static bool cdc_data_rx_cb(const uint8_t *data, size_t data_len, void *arg)
{
    (void)arg;
    rx_ring_push(data, data_len);
    if (s_rx_cb != NULL) {
        s_rx_cb(data, data_len, s_rx_cb_ctx);
    }
    return true;
}

static void cdc_event_cb(const cdc_acm_host_dev_event_data_t *event, void *arg)
{
    (void)arg;
    if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED) {
        ESP_LOGW(TAG, "CDC-Geraet waehrend aktiver Sitzung getrennt");
        s_open = false;
        s_dev_hdl = NULL;
    }
}

esp_err_t usb_serial_open(uint32_t baud_rate)
{
    if (s_open) {
        return ESP_ERR_INVALID_STATE;
    }

    usb_device_manager_target_t target;
    usb_device_manager_get_target(&target);
    if (!target.connected || !target.serial_supported) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (s_rx_mutex == NULL) {
        s_rx_mutex = xSemaphoreCreateMutex();
        if (s_rx_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!s_cdc_driver_installed) {
        const cdc_acm_host_driver_config_t driver_config = {
            .driver_task_stack_size = 4096,
            .driver_task_priority = 5,
            .xCoreID = tskNO_AFFINITY,
            .new_dev_cb = NULL,
        };
        ESP_RETURN_ON_ERROR(cdc_acm_host_install(&driver_config), TAG, "cdc_acm_host_install fehlgeschlagen");
        s_cdc_driver_installed = true;
    }

    const cdc_acm_host_device_config_t dev_config = {
        .connection_timeout_ms = 1000,
        .out_buffer_size = 512,
        .in_buffer_size = 512,
        .event_cb = cdc_event_cb,
        .data_cb = cdc_data_rx_cb,
        .user_arg = NULL,
    };
    esp_err_t err = cdc_acm_host_open(target.vid, target.pid, 0, &dev_config, &s_dev_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cdc_acm_host_open fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }

    const cdc_acm_line_coding_t line_coding = {
        .dwDTERate = baud_rate,
        .bCharFormat = 0,   // 1 Stopbit
        .bParityType = 0,   // keine Paritaet
        .bDataBits = 8,
    };
    err = cdc_acm_host_line_coding_set(s_dev_hdl, &line_coding);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "line_coding_set fehlgeschlagen (Geraet unterstuetzt evtl. keine CDC-Line-Coding-Requests): %s",
                 esp_err_to_name(err));
        // Kein harter Fehler: manche USB-Serial/JTOG-Implementierungen
        // ignorieren Line-Coding-Requests, da die Baudrate fuer den reinen
        // JTAG/Serial-Debug-Kanal ohnehin virtuell ist.
    }

    s_rx_head = 0;
    s_rx_count = 0;
    s_open = true;
    ESP_LOGI(TAG, "USB-Serial geoeffnet, Baud=%" PRIu32, baud_rate);
    return ESP_OK;
}

esp_err_t usb_serial_close(void)
{
    if (!s_open) {
        return ESP_OK;
    }
    esp_err_t err = cdc_acm_host_close(s_dev_hdl);
    s_dev_hdl = NULL;
    s_open = false;
    return err;
}

bool usb_serial_is_open(void)
{
    return s_open;
}

esp_err_t usb_serial_write(const uint8_t *data, size_t len)
{
    if (!s_open) {
        return ESP_ERR_INVALID_STATE;
    }
    return cdc_acm_host_data_tx_blocking(s_dev_hdl, data, len, 1000);
}

size_t usb_serial_read(uint8_t *buf, size_t max_len)
{
    if (!s_open || s_rx_mutex == NULL) {
        return 0;
    }
    return rx_ring_pop(buf, max_len);
}

esp_err_t usb_serial_set_baud(uint32_t baud_rate)
{
    if (!s_open) {
        return ESP_ERR_INVALID_STATE;
    }
    const cdc_acm_line_coding_t line_coding = {
        .dwDTERate = baud_rate,
        .bCharFormat = 0,
        .bParityType = 0,
        .bDataBits = 8,
    };
    return cdc_acm_host_line_coding_set(s_dev_hdl, &line_coding);
}

esp_err_t usb_serial_set_control_lines(bool dtr, bool rts)
{
    if (!s_open) {
        return ESP_ERR_INVALID_STATE;
    }
    return cdc_acm_host_set_control_line_state(s_dev_hdl, dtr, rts);
}

esp_err_t usb_serial_register_rx_callback(usb_serial_rx_cb_t cb, void *ctx)
{
    s_rx_cb = cb;
    s_rx_cb_ctx = ctx;
    return ESP_OK;
}
