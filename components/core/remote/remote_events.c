#include "remote_events.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "cJSON.h"
#include "usb_host_manager.h"
#include "usb_device_manager.h"
#include "usb_serial.h"
#include "flash_manager.h"
#include "esp_log.h"

static const char *TAG = "REMOTE_EVENTS";

static httpd_handle_t s_server = NULL;
static int s_ws_fds[REMOTE_EVENTS_MAX_WS_CLIENTS];
static int s_serial_fds[REMOTE_EVENTS_MAX_SERIAL_CLIENTS];
static bool s_initialized = false;

typedef struct {
    uint8_t *data;
    size_t   len;
    bool     is_binary;
    bool     is_serial;
} broadcast_work_t;

static void broadcast_work_fn(void *arg)
{
    broadcast_work_t *w = (broadcast_work_t *)arg;
    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = w->is_binary ? HTTPD_WS_TYPE_BINARY : HTTPD_WS_TYPE_TEXT,
        .payload = w->data,
        .len = w->len,
    };
    int *fds = w->is_serial ? s_serial_fds : s_ws_fds;
    size_t max = w->is_serial ? REMOTE_EVENTS_MAX_SERIAL_CLIENTS : REMOTE_EVENTS_MAX_WS_CLIENTS;
    for (size_t i = 0; i < max; i++) {
        if (fds[i] >= 0) {
            esp_err_t err = httpd_ws_send_frame_async(s_server, fds[i], &frame);
            if (err != ESP_OK) {
                fds[i] = -1;  // Client vermutlich weg - naechstes /ws bzw. /serial raeumt sauber auf
            }
        }
    }
    free(w->data);
    free(w);
}

void remote_events_broadcast_json_owned(char *json_str)
{
    if (s_server == NULL || json_str == NULL) {
        free(json_str);
        return;
    }
    broadcast_work_t *w = malloc(sizeof(broadcast_work_t));
    if (w == NULL) {
        free(json_str);
        return;
    }
    w->data = (uint8_t *)json_str;
    w->len = strlen(json_str);
    w->is_binary = false;
    w->is_serial = false;
    if (httpd_queue_work(s_server, broadcast_work_fn, w) != ESP_OK) {
        free(json_str);
        free(w);
    }
}

static void broadcast_serial_bytes(const uint8_t *data, size_t len)
{
    if (s_server == NULL || len == 0) {
        return;
    }
    uint8_t *copy = malloc(len);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, data, len);
    broadcast_work_t *w = malloc(sizeof(broadcast_work_t));
    if (w == NULL) {
        free(copy);
        return;
    }
    w->data = copy;
    w->len = len;
    w->is_binary = true;
    w->is_serial = true;
    if (httpd_queue_work(s_server, broadcast_work_fn, w) != ESP_OK) {
        free(copy);
        free(w);
    }
}

static void broadcast_event(cJSON *root)
{
    cJSON_AddNumberToObject(root, "protocol", 1);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    remote_events_broadcast_json_owned(out);
}

static void on_usb_connect(const usb_host_device_info_t *info, void *ctx)
{
    (void)ctx;
    usb_device_manager_target_t target;
    usb_device_manager_get_target(&target);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "device.connected");
    cJSON *dev = cJSON_AddObjectToObject(root, "device");
    char vid_str[8], pid_str[8];
    snprintf(vid_str, sizeof(vid_str), "%04x", info->vid);
    snprintf(pid_str, sizeof(pid_str), "%04x", info->pid);
    cJSON_AddStringToObject(dev, "vid", vid_str);
    cJSON_AddStringToObject(dev, "pid", pid_str);
    cJSON_AddStringToObject(dev, "name", info->product);
    cJSON_AddStringToObject(dev, "bridge", target.bridge_label ? target.bridge_label : "?");
    cJSON_AddBoolToObject(dev, "serial_supported", target.serial_supported);
    cJSON_AddBoolToObject(dev, "flash_supported", target.flash_supported);
    broadcast_event(root);
}

static void on_usb_disconnect(uint8_t dev_addr, void *ctx)
{
    (void)ctx;
    (void)dev_addr;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "device.disconnected");
    broadcast_event(root);
}

static void on_flash_status(const flash_status_t *status, void *ctx)
{
    (void)ctx;
    cJSON *root = cJSON_CreateObject();
    if (status->state == FLASH_STATE_SUCCESS || status->state == FLASH_STATE_ERROR ||
        status->state == FLASH_STATE_CANCELLED) {
        cJSON_AddStringToObject(root, "type", "flash.completed");
        cJSON_AddBoolToObject(root, "success", status->state == FLASH_STATE_SUCCESS);
        if (status->state != FLASH_STATE_SUCCESS) {
            cJSON_AddStringToObject(root, "error", flash_error_str(status->last_error));
        }
    } else {
        cJSON_AddStringToObject(root, "type", "flash.progress");
        cJSON_AddStringToObject(root, "state", flash_state_str(status->state));
        cJSON_AddStringToObject(root, "file", status->current_file);
        char addr_str[16];
        snprintf(addr_str, sizeof(addr_str), "0x%08" PRIX32, status->current_address);
        cJSON_AddStringToObject(root, "address", addr_str);
        cJSON_AddNumberToObject(root, "progress", status->progress_percent);
        cJSON_AddNumberToObject(root, "written", (double)status->bytes_written_total);
        cJSON_AddNumberToObject(root, "total", (double)status->bytes_total_all);
    }
    broadcast_event(root);
}

static void on_serial_rx(const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;
    broadcast_serial_bytes(data, len);
}

esp_err_t remote_events_init(httpd_handle_t server)
{
    if (s_initialized) {
        return ESP_OK;
    }
    s_server = server;
    for (int i = 0; i < REMOTE_EVENTS_MAX_WS_CLIENTS; i++) s_ws_fds[i] = -1;
    for (int i = 0; i < REMOTE_EVENTS_MAX_SERIAL_CLIENTS; i++) s_serial_fds[i] = -1;

    esp_err_t err = flash_manager_init();  // stellt sicher, dass usb_device_manager bereits registriert ist
    if (err != ESP_OK) {
        return err;
    }
    flash_manager_register_status_callback(on_flash_status, NULL);
    usb_host_manager_register_callbacks(on_usb_connect, on_usb_disconnect, NULL);
    usb_serial_register_rx_callback(on_serial_rx, NULL);

    s_initialized = true;
    ESP_LOGI(TAG, "Event-Bus initialisiert");
    return ESP_OK;
}

void remote_events_register_ws_client(int fd)
{
    for (int i = 0; i < REMOTE_EVENTS_MAX_WS_CLIENTS; i++) {
        if (s_ws_fds[i] < 0) {
            s_ws_fds[i] = fd;
            return;
        }
    }
    ESP_LOGW(TAG, "Kein freier /ws-Client-Slot mehr (max %d)", REMOTE_EVENTS_MAX_WS_CLIENTS);
}

void remote_events_unregister_ws_client(int fd)
{
    for (int i = 0; i < REMOTE_EVENTS_MAX_WS_CLIENTS; i++) {
        if (s_ws_fds[i] == fd) {
            s_ws_fds[i] = -1;
        }
    }
}

void remote_events_register_serial_client(int fd)
{
    for (int i = 0; i < REMOTE_EVENTS_MAX_SERIAL_CLIENTS; i++) {
        if (s_serial_fds[i] < 0) {
            s_serial_fds[i] = fd;
            return;
        }
    }
}

void remote_events_unregister_serial_client(int fd)
{
    for (int i = 0; i < REMOTE_EVENTS_MAX_SERIAL_CLIENTS; i++) {
        if (s_serial_fds[i] == fd) {
            s_serial_fds[i] = -1;
        }
    }
}
