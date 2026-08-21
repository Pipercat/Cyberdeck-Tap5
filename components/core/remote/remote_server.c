#include "remote_server.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "cJSON.h"

#include "settings.h"
#include "wifi_module.h"
#include "system_module.h"
#include "log_sink.h"
#include "usb_device_manager.h"
#include "flash_manager.h"
#include "usb_serial.h"
#include "remote_auth.h"
#include "remote_protocol.h"
#include "remote_events.h"

static const char *TAG = "REMOTE_SERVER";

#define MAX_JSON_BODY 4096

static httpd_handle_t s_httpd = NULL;
static bool s_ip_handler_registered = false;
static int64_t s_last_activity_us = 0;

// --- Hilfsfunktionen -------------------------------------------------------

static void touch_activity(void)
{
    s_last_activity_us = esp_timer_get_time();
}

static void add_cors_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization, Content-Type, X-File-Index, X-Offset");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
}

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    add_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (out == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    esp_err_t err = httpd_resp_send(req, out, strlen(out));
    free(out);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, const char *http_status, const char *code, const char *message)
{
    add_cors_headers(req);
    httpd_resp_set_status(req, http_status);
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", code);
    cJSON_AddStringToObject(root, "message", message);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    esp_err_t err = httpd_resp_send(req, out, strlen(out));
    free(out);
    return err;
}

// Prueft "Authorization: Bearer <token>". Bei Erfolg wird die Client-
// Aktivitaet vermerkt (statusbar_set_server()/System-Monitor, siehe main.c).
static bool require_auth(httpd_req_t *req)
{
    char hdr[REMOTE_AUTH_TOKEN_HEXLEN + 16];
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK) {
        return false;
    }
    static const char prefix[] = "Bearer ";
    if (strncmp(hdr, prefix, sizeof(prefix) - 1) != 0) {
        return false;
    }
    bool ok = remote_auth_validate_token(hdr + sizeof(prefix) - 1);
    if (ok) {
        touch_activity();
    }
    return ok;
}

static bool query_param(httpd_req_t *req, const char *key, char *out, size_t out_len)
{
    char query[256];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(query, key, out, out_len) == ESP_OK;
}

// Liest den kompletten Request-Body in buf (max buf_len-1), 0-terminiert.
// Lehnt zu grosse Bodies ab statt unbegrenzt zu puffern (Abschnitt 13/27).
static esp_err_t read_body(httpd_req_t *req, char *buf, size_t buf_len, size_t *out_len)
{
    if (req->content_len >= buf_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, buf + received, req->content_len - received);
        if (n <= 0) {
            return ESP_FAIL;
        }
        received += n;
    }
    buf[received] = '\0';
    if (out_len != NULL) {
        *out_len = received;
    }
    return ESP_OK;
}

// --- /api/v1/system, /api/v1/network ---------------------------------------

static esp_err_t system_get_handler(httpd_req_t *req)
{
    cJSON *root = remote_protocol_new_envelope();
    system_module_stats_t stats;
    if (system_module_get_stats(&stats) == ESP_OK) {
        cJSON_AddNumberToObject(root, "uptime_s", (double)stats.uptime_s);
        cJSON_AddNumberToObject(root, "heap_free", stats.heap_free_bytes);
        cJSON_AddNumberToObject(root, "heap_total", stats.heap_total_bytes);
        cJSON_AddNumberToObject(root, "psram_free", stats.psram_free_bytes);
        cJSON_AddNumberToObject(root, "psram_total", stats.psram_total_bytes);
        cJSON_AddNumberToObject(root, "flash_size", stats.flash_size_bytes);
        cJSON_AddNumberToObject(root, "chip_temp_c", stats.chip_temp_c);
        cJSON_AddNumberToObject(root, "battery_voltage_v", stats.battery_voltage_v);
        cJSON_AddStringToObject(root, "idf_version", stats.idf_version);
    }
    cJSON_AddBoolToObject(root, "remote_access_enabled", settings_get()->remote_access_enabled);
    cJSON_AddBoolToObject(root, "require_pairing", settings_get()->remote_require_pairing);
    return send_json(req, root);
}

static esp_err_t network_get_handler(httpd_req_t *req)
{
    cJSON *root = remote_protocol_new_envelope();
    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    bool connected = wifi_module_get_state() == WIFI_MODULE_STATE_CONNECTED;
    cJSON_AddBoolToObject(wifi, "connected", connected);
    cJSON_AddStringToObject(wifi, "ssid", wifi_module_get_connected_ssid());
    cJSON_AddNumberToObject(wifi, "rssi_dbm", wifi_module_get_rssi());
    char ip[16] = "";
    wifi_module_get_ip_str(ip, sizeof(ip));
    cJSON_AddStringToObject(wifi, "ip", ip);

    cJSON *server = cJSON_AddObjectToObject(root, "remote_server");
    cJSON_AddBoolToObject(server, "running", remote_server_is_running());
    cJSON_AddNumberToObject(server, "port", REMOTE_SERVER_PORT);
    cJSON_AddNumberToObject(server, "clients", (double)remote_server_get_client_count());
    int64_t last = remote_server_get_last_activity_us();
    cJSON_AddNumberToObject(server, "last_activity_s_ago", last > 0 ? (double)((esp_timer_get_time() - last) / 1000000) : -1);
    return send_json(req, root);
}

// --- /api/v1/pair/confirm ---------------------------------------------------

static esp_err_t pair_confirm_post_handler(httpd_req_t *req)
{
    char body[256];
    if (read_body(req, body, sizeof(body), NULL) != ESP_OK) {
        return send_error(req, "400 Bad Request", "invalid_body", "Body zu gross oder unlesbar");
    }
    cJSON *json = cJSON_Parse(body);
    if (json == NULL) {
        return send_error(req, "400 Bad Request", "invalid_json", "Ungueltiges JSON");
    }
    const cJSON *code = cJSON_GetObjectItem(json, "code");
    const cJSON *name = cJSON_GetObjectItem(json, "client_name");
    if (!cJSON_IsString(code)) {
        cJSON_Delete(json);
        return send_error(req, "400 Bad Request", "missing_code", "Feld 'code' fehlt");
    }

    char token[REMOTE_AUTH_TOKEN_HEXLEN + 1];
    remote_auth_client_t client;
    esp_err_t err = remote_auth_confirm(code->valuestring, cJSON_IsString(name) ? name->valuestring : NULL,
                                         token, &client);
    cJSON_Delete(json);

    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, "429 Too Many Requests", "rate_limited", "Zu viele Fehlversuche - bitte spaeter erneut versuchen");
    }
    if (err == ESP_ERR_NO_MEM) {
        return send_error(req, "409 Conflict", "max_clients", "Maximale Anzahl gepairter Clients erreicht");
    }
    if (err != ESP_OK) {
        return send_error(req, "401 Unauthorized", "invalid_code", "Pairing-Code ungueltig oder abgelaufen");
    }

    cJSON *root = remote_protocol_new_envelope();
    cJSON_AddStringToObject(root, "client_token", token);
    cJSON_AddStringToObject(root, "client_id", client.client_id);
    return send_json(req, root);
}

// --- /api/v1/devices ---------------------------------------------------------

static void target_to_json(cJSON *root, const usb_device_manager_target_t *t)
{
    cJSON_AddBoolToObject(root, "connected", t->connected);
    if (!t->connected) {
        return;
    }
    char vid_str[8], pid_str[8];
    snprintf(vid_str, sizeof(vid_str), "%04x", t->vid);
    snprintf(pid_str, sizeof(pid_str), "%04x", t->pid);
    cJSON_AddStringToObject(root, "vid", vid_str);
    cJSON_AddStringToObject(root, "pid", pid_str);
    cJSON_AddStringToObject(root, "manufacturer", t->manufacturer);
    cJSON_AddStringToObject(root, "product", t->product);
    cJSON_AddStringToObject(root, "serial", t->serial);
    cJSON_AddStringToObject(root, "bridge", t->bridge_label ? t->bridge_label : "?");
    cJSON_AddBoolToObject(root, "serial_supported", t->serial_supported);
    cJSON_AddBoolToObject(root, "flash_supported", t->flash_supported);
}

static esp_err_t devices_get_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return send_error(req, "401 Unauthorized", "auth_required", "Bearer-Token fehlt/ungueltig");
    flash_manager_init();  // stellt USB-Erkennung sicher, falls noch nicht aktiv
    usb_device_manager_target_t t;
    usb_device_manager_get_target(&t);
    cJSON *root = remote_protocol_new_envelope();
    cJSON *devices = cJSON_AddArrayToObject(root, "devices");
    if (t.connected) {
        cJSON *dev = cJSON_CreateObject();
        target_to_json(dev, &t);
        cJSON_AddItemToArray(devices, dev);
    }
    return send_json(req, root);
}

static esp_err_t devices_current_get_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return send_error(req, "401 Unauthorized", "auth_required", "Bearer-Token fehlt/ungueltig");
    flash_manager_init();
    usb_device_manager_target_t t;
    usb_device_manager_get_target(&t);
    cJSON *root = remote_protocol_new_envelope();
    target_to_json(root, &t);
    return send_json(req, root);
}

// --- /api/v1/flash/* -----------------------------------------------------

static void flash_status_to_json(cJSON *root, const flash_status_t *s)
{
    cJSON_AddStringToObject(root, "state", flash_state_str(s->state));
    cJSON_AddStringToObject(root, "error", flash_error_str(s->last_error));
    cJSON_AddStringToObject(root, "current_file", s->current_file);
    char addr[16];
    snprintf(addr, sizeof(addr), "0x%08" PRIX32, s->current_address);
    cJSON_AddStringToObject(root, "address", addr);
    cJSON_AddNumberToObject(root, "file_index", (double)s->file_index);
    cJSON_AddNumberToObject(root, "file_count", (double)s->file_count);
    cJSON_AddNumberToObject(root, "bytes_written_total", (double)s->bytes_written_total);
    cJSON_AddNumberToObject(root, "bytes_total_all", (double)s->bytes_total_all);
    cJSON_AddNumberToObject(root, "progress_percent", s->progress_percent);
    cJSON_AddStringToObject(root, "detected_chip", flash_target_chip_name(s->detected_chip));
}

static esp_err_t flash_status_get_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return send_error(req, "401 Unauthorized", "auth_required", "Bearer-Token fehlt/ungueltig");
    flash_manager_init();
    flash_status_t status;
    flash_manager_get_status(&status);
    cJSON *root = remote_protocol_new_envelope();
    flash_status_to_json(root, &status);
    return send_json(req, root);
}

static bool parse_address(const char *str, uint32_t *out)
{
    if (str == NULL) return false;
    char *end;
    unsigned long v = strtoul(str, &end, 0);  // Basis 0: erkennt "0x..." automatisch, sonst dezimal
    if (end == str) return false;
    *out = (uint32_t)v;
    return true;
}

static esp_err_t flash_start_post_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return send_error(req, "401 Unauthorized", "auth_required", "Bearer-Token fehlt/ungueltig");

    char body[MAX_JSON_BODY];
    if (read_body(req, body, sizeof(body), NULL) != ESP_OK) {
        return send_error(req, "413 Payload Too Large", "body_too_large", "Manifest zu gross");
    }
    cJSON *json = cJSON_Parse(body);
    if (json == NULL) {
        return send_error(req, "400 Bad Request", "invalid_json", "Ungueltiges JSON");
    }

    flash_manifest_t manifest = {0};
    const cJSON *chip = cJSON_GetObjectItem(json, "chip");
    if (cJSON_IsString(chip)) {
        strncpy(manifest.chip_hint, chip->valuestring, sizeof(manifest.chip_hint) - 1);
    }
    const cJSON *files = cJSON_GetObjectItem(json, "files");
    if (!cJSON_IsArray(files) || cJSON_GetArraySize(files) == 0 ||
        cJSON_GetArraySize(files) > FLASH_MANIFEST_MAX_FILES) {
        cJSON_Delete(json);
        return send_error(req, "400 Bad Request", "invalid_manifest", "Manifest 'files' fehlt/leer/zu lang");
    }

    size_t i = 0;
    const cJSON *f;
    cJSON_ArrayForEach(f, files) {
        const cJSON *name = cJSON_GetObjectItem(f, "name");
        const cJSON *address = cJSON_GetObjectItem(f, "address");
        const cJSON *size = cJSON_GetObjectItem(f, "size");

        uint32_t addr_val = 0;
        bool addr_ok = false;
        if (cJSON_IsString(address)) {
            addr_ok = parse_address(address->valuestring, &addr_val);
        } else if (cJSON_IsNumber(address)) {
            addr_val = (uint32_t)address->valuedouble;
            addr_ok = true;
        }

        if (!cJSON_IsString(name) || !cJSON_IsNumber(size) || !addr_ok) {
            cJSON_Delete(json);
            return send_error(req, "400 Bad Request", "invalid_manifest", "Datei-Eintrag unvollstaendig/ungueltig");
        }
        strncpy(manifest.files[i].name, name->valuestring, FLASH_MANIFEST_MAX_NAME - 1);
        manifest.files[i].address = addr_val;
        manifest.files[i].size = (uint32_t)size->valuedouble;
        const cJSON *crc = cJSON_GetObjectItem(f, "crc32");
        if (cJSON_IsNumber(crc)) {
            manifest.files[i].has_crc32 = true;
            manifest.files[i].crc32 = (uint32_t)crc->valuedouble;
        }
        i++;
    }
    manifest.file_count = i;
    cJSON_Delete(json);

    esp_err_t err = flash_manager_start(&manifest);
    if (err != ESP_OK) {
        flash_status_t status;
        flash_manager_get_status(&status);
        return send_error(req, "409 Conflict", "flash_start_failed", flash_error_str(status.last_error));
    }
    cJSON *root = remote_protocol_new_envelope();
    cJSON_AddBoolToObject(root, "ok", true);
    return send_json(req, root);
}

static esp_err_t flash_chunk_post_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return send_error(req, "401 Unauthorized", "auth_required", "Bearer-Token fehlt/ungueltig");

    char file_str[8], offset_str[16];
    if (!query_param(req, "file", file_str, sizeof(file_str)) ||
        !query_param(req, "offset", offset_str, sizeof(offset_str))) {
        return send_error(req, "400 Bad Request", "missing_params", "Query-Parameter 'file'/'offset' fehlen");
    }
    size_t file_index = (size_t)strtoul(file_str, NULL, 10);
    uint32_t offset = (uint32_t)strtoul(offset_str, NULL, 10);

    if (req->content_len == 0 || req->content_len > FLASH_CHUNK_MAX_LEN) {
        return send_error(req, "413 Payload Too Large", "chunk_too_large", "Chunk-Groesse ausserhalb des zulaessigen Bereichs");
    }

    static uint8_t chunk_buf[FLASH_CHUNK_MAX_LEN];
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, (char *)chunk_buf + received, req->content_len - received);
        if (n <= 0) {
            return send_error(req, "400 Bad Request", "read_failed", "Chunk-Body konnte nicht gelesen werden");
        }
        received += n;
    }

    char crc_hdr[16];
    if (httpd_req_get_hdr_value_str(req, "X-Chunk-CRC32", crc_hdr, sizeof(crc_hdr)) == ESP_OK) {
        uint32_t expected = (uint32_t)strtoul(crc_hdr, NULL, 16);
        uint32_t actual = remote_protocol_crc32(chunk_buf, received);
        if (expected != actual) {
            return send_error(req, "400 Bad Request", "crc_mismatch", "Chunk-CRC32 stimmt nicht ueberein");
        }
    }

    esp_err_t err = flash_manager_write_chunk(file_index, offset, chunk_buf, received);
    if (err != ESP_OK) {
        flash_status_t status;
        flash_manager_get_status(&status);
        return send_error(req, "409 Conflict", "chunk_rejected", flash_error_str(status.last_error));
    }
    cJSON *root = remote_protocol_new_envelope();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "written", (double)received);
    return send_json(req, root);
}

static esp_err_t flash_finish_post_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return send_error(req, "401 Unauthorized", "auth_required", "Bearer-Token fehlt/ungueltig");
    esp_err_t err = flash_manager_finish();
    if (err != ESP_OK) {
        flash_status_t status;
        flash_manager_get_status(&status);
        return send_error(req, "409 Conflict", "finish_failed", flash_error_str(status.last_error));
    }
    cJSON *root = remote_protocol_new_envelope();
    cJSON_AddBoolToObject(root, "ok", true);
    return send_json(req, root);
}

static esp_err_t flash_cancel_post_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return send_error(req, "401 Unauthorized", "auth_required", "Bearer-Token fehlt/ungueltig");
    flash_manager_cancel();
    cJSON *root = remote_protocol_new_envelope();
    cJSON_AddBoolToObject(root, "ok", true);
    return send_json(req, root);
}

// --- /api/v1/device/* -----------------------------------------------------

static esp_err_t device_reset_post_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return send_error(req, "401 Unauthorized", "auth_required", "Bearer-Token fehlt/ungueltig");
    flash_manager_init();
    esp_err_t err = flash_target_reset_normal();
    cJSON *root = remote_protocol_new_envelope();
    cJSON_AddBoolToObject(root, "ok", err == ESP_OK);
    return send_json(req, root);
}

static esp_err_t device_bootloader_post_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return send_error(req, "401 Unauthorized", "auth_required", "Bearer-Token fehlt/ungueltig");
    flash_manager_init();
    usb_device_manager_target_t t;
    usb_device_manager_get_target(&t);
    if (!t.connected || !t.serial_supported) {
        return send_error(req, "409 Conflict", "manual_entry_required",
                           "Manual bootloader entry required. Hold BOOT and press RESET.");
    }
    esp_err_t err = flash_target_enter_bootloader_only();
    cJSON *root = remote_protocol_new_envelope();
    cJSON_AddBoolToObject(root, "ok", err == ESP_OK);
    if (err != ESP_OK) {
        cJSON_AddStringToObject(root, "message", "Manual bootloader entry required. Hold BOOT and press RESET.");
    }
    return send_json(req, root);
}

// --- /api/v1/logs -----------------------------------------------------------

static esp_err_t logs_get_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return send_error(req, "401 Unauthorized", "auth_required", "Bearer-Token fehlt/ungueltig");
    add_cors_headers(req);
    httpd_resp_set_type(req, "text/plain");
    static char buf[4096];
    size_t n = log_sink_read_recent(buf, sizeof(buf) - 1);
    buf[n] = '\0';
    return httpd_resp_send(req, buf, n);
}

// --- OPTIONS-Catch-All fuer CORS-Preflight ---------------------------------

static esp_err_t options_handler(httpd_req_t *req)
{
    add_cors_headers(req);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

// --- WebSocket: /api/v1/ws --------------------------------------------------

static bool ws_token_valid(httpd_req_t *req)
{
    char query[256], token[REMOTE_AUTH_TOKEN_HEXLEN + 1];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    if (httpd_query_key_value(query, "token", token, sizeof(token)) != ESP_OK) {
        return false;
    }
    return remote_auth_validate_token(token);
}

// HINWEIS zur Handshake-Ablehnung: ob ESP-IDFs esp_http_server bei
// is_websocket=true den 101-Handshake bereits VOR diesem ersten
// HTTP_GET-Aufruf abschliesst oder erst danach, haengt von der genauen
// esp_http_server-Version ab (in dieser Session nicht gegen echte Header
// verifizierbar, siehe docs/remote_protocol.md). In beiden Faellen gilt:
// ein ungueltiges Token fuehrt dazu, dass die Verbindung sofort wieder
// geschlossen wird (ESP_FAIL) - kein Byte Nutzdaten erreicht einen nicht
// authentifizierten Client, auch wenn die HTTP-Statuszeile dafuer evtl.
// nicht exakt "401" zeigt.
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        if (!ws_token_valid(req)) {
            return ESP_FAIL;
        }
        touch_activity();
        remote_events_register_ws_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {0};
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        remote_events_unregister_ws_client(httpd_req_to_sockfd(req));
        return err;
    }
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        remote_events_unregister_ws_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    if (frame.len == 0) {
        return ESP_OK;
    }
    if (frame.len > 512) {
        // Eingehende Frames auf /ws sind reine Steuerkanal-Pings (alle
        // Aktionen laufen ueber REST, siehe docs/remote_protocol.md) - ein
        // derart grosser Frame ist unerwartet. Verbindung sauber schliessen
        // statt einen unbegrenzt grossen Puffer zu allozieren (Abschnitt 13/27).
        remote_events_unregister_ws_client(httpd_req_to_sockfd(req));
        return ESP_FAIL;
    }
    uint8_t discard[512];
    frame.payload = discard;
    return httpd_ws_recv_frame(req, &frame, frame.len);
}

// --- WebSocket: /api/v1/serial ----------------------------------------------

static esp_err_t serial_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        if (!ws_token_valid(req)) {
            return ESP_FAIL;
        }
        flash_manager_init();
        char baud_str[8];
        uint32_t baud = query_param(req, "baud", baud_str, sizeof(baud_str)) ? (uint32_t)strtoul(baud_str, NULL, 10) : 115200;
        if (!usb_serial_is_open()) {
            usb_serial_open(baud);
        }
        touch_activity();
        remote_events_register_serial_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {0};
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        remote_events_unregister_serial_client(httpd_req_to_sockfd(req));
        return err;
    }
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        remote_events_unregister_serial_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    if (frame.len == 0) {
        return ESP_OK;
    }
    if (frame.len > 512) {
        // Eine einzelne TX-Eingabe (Terminal-Zeile/Kommando) sollte nie so
        // gross sein - Verbindung schliessen statt unbegrenzt zu puffern.
        remote_events_unregister_serial_client(httpd_req_to_sockfd(req));
        return ESP_FAIL;
    }
    uint8_t buf[512];
    frame.payload = buf;
    if (httpd_ws_recv_frame(req, &frame, frame.len) == ESP_OK) {
        usb_serial_write(buf, frame.len);
    }
    return ESP_OK;
}

// --- Server-Lifecycle --------------------------------------------------------

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    if (settings_get()->remote_access_enabled) {
        remote_server_start();
    }
}

esp_err_t remote_server_init(void)
{
    remote_auth_init();
    if (!s_ip_handler_registered) {
        esp_err_t err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        s_ip_handler_registered = true;
    }
    return ESP_OK;
}

esp_err_t remote_server_start(void)
{
    if (s_httpd != NULL) {
        return ESP_OK;  // bereits gestartet
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = REMOTE_SERVER_PORT;
    config.max_uri_handlers = 20;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.stack_size = 8192;

    esp_err_t err = httpd_start(&s_httpd, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start fehlgeschlagen: %s", esp_err_to_name(err));
        s_httpd = NULL;
        return err;
    }

    static const httpd_uri_t uris[] = {
        { .uri = "/api/v1/system",            .method = HTTP_GET,  .handler = system_get_handler },
        { .uri = "/api/v1/network",           .method = HTTP_GET,  .handler = network_get_handler },
        { .uri = "/api/v1/pair/confirm",      .method = HTTP_POST, .handler = pair_confirm_post_handler },
        { .uri = "/api/v1/devices",           .method = HTTP_GET,  .handler = devices_get_handler },
        { .uri = "/api/v1/devices/current",   .method = HTTP_GET,  .handler = devices_current_get_handler },
        { .uri = "/api/v1/flash/status",      .method = HTTP_GET,  .handler = flash_status_get_handler },
        { .uri = "/api/v1/flash/start",       .method = HTTP_POST, .handler = flash_start_post_handler },
        { .uri = "/api/v1/flash/chunk",       .method = HTTP_POST, .handler = flash_chunk_post_handler },
        { .uri = "/api/v1/flash/finish",      .method = HTTP_POST, .handler = flash_finish_post_handler },
        { .uri = "/api/v1/flash/cancel",      .method = HTTP_POST, .handler = flash_cancel_post_handler },
        { .uri = "/api/v1/device/reset",      .method = HTTP_POST, .handler = device_reset_post_handler },
        { .uri = "/api/v1/device/bootloader", .method = HTTP_POST, .handler = device_bootloader_post_handler },
        { .uri = "/api/v1/logs",              .method = HTTP_GET,  .handler = logs_get_handler },
        { .uri = "/api/v1/ws",       .method = HTTP_GET, .handler = ws_handler,        .is_websocket = true },
        { .uri = "/api/v1/serial",   .method = HTTP_GET, .handler = serial_ws_handler, .is_websocket = true },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_httpd, &uris[i]);
    }
    static const httpd_uri_t options_uri = { .uri = "/api/v1/*", .method = HTTP_OPTIONS, .handler = options_handler };
    httpd_register_uri_handler(s_httpd, &options_uri);

    remote_events_init(s_httpd);

    ESP_LOGI(TAG, "Remote-Server gestartet auf Port %d", REMOTE_SERVER_PORT);
    return ESP_OK;
}

esp_err_t remote_server_stop(void)
{
    if (s_httpd == NULL) {
        return ESP_OK;
    }
    esp_err_t err = httpd_stop(s_httpd);
    s_httpd = NULL;
    ESP_LOGI(TAG, "Remote-Server gestoppt");
    return err;
}

bool remote_server_is_running(void)
{
    return s_httpd != NULL;
}

size_t remote_server_get_client_count(void)
{
    if (s_httpd == NULL) {
        return 0;
    }
    size_t fds = CONFIG_LWIP_MAX_SOCKETS;
    int client_fds[CONFIG_LWIP_MAX_SOCKETS];
    if (httpd_get_client_list(s_httpd, &fds, client_fds) != ESP_OK) {
        return 0;
    }
    return fds;
}

int64_t remote_server_get_last_activity_us(void)
{
    return s_last_activity_us;
}
