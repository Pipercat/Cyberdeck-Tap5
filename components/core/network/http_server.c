#include "http_server.h"
#include <string.h>
#include <stdio.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_idf_version.h"
#include "storage_module.h"
#include "log_sink.h"

#define HTTP_SERVER_API_FILES_PREFIX "/api/files/"
#define HTTP_SERVER_MAX_UPLOAD_BYTES (512 * 1024)  // Storage-Partition ist ~2.75 MB gesamt
#define HTTP_SERVER_IO_CHUNK 1024

static const char *TAG = "http_server";
static httpd_handle_t s_server = NULL;

// Extrahiert und validiert den Dateinamen aus der URI (nach dem Praefix).
// Liefert NULL bei ungueltigem/unsicherem Namen.
static const char *extract_and_validate_name(httpd_req_t *req)
{
    size_t prefix_len = strlen(HTTP_SERVER_API_FILES_PREFIX);
    if (strncmp(req->uri, HTTP_SERVER_API_FILES_PREFIX, prefix_len) != 0) {
        return NULL;
    }
    const char *name = req->uri + prefix_len;
    return storage_module_name_is_safe(name) ? name : NULL;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    size_t heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t heap_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t storage_used = 0, storage_total = 0;
    storage_module_get_usage(&storage_used, &storage_total);
    int64_t uptime_s = esp_timer_get_time() / 1000000;

    char buf[384];
    int n = snprintf(buf, sizeof(buf),
        "{"
        "\"uptime_s\":%lld,"
        "\"idf_version\":\"%s\","
        "\"heap_free_bytes\":%u,\"heap_total_bytes\":%u,"
        "\"psram_free_bytes\":%u,\"psram_total_bytes\":%u,"
        "\"storage_used_bytes\":%u,\"storage_total_bytes\":%u"
        "}",
        (long long)uptime_s, esp_get_idf_version(),
        (unsigned)heap_free, (unsigned)heap_total,
        (unsigned)psram_free, (unsigned)psram_total,
        (unsigned)storage_used, (unsigned)storage_total);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t logs_get_handler(httpd_req_t *req)
{
    static char buf[4096];
    size_t n = log_sink_read_recent(buf, sizeof(buf));
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t files_list_get_handler(httpd_req_t *req)
{
    storage_module_entry_t entries[64];
    size_t n = storage_module_list(entries, 64);

    // Ringpuffer statisch, da SPIFFS max_files begrenzt und n damit klein ist.
    static char buf[64 * 96 + 16];
    size_t off = 0;
    buf[off++] = '[';
    for (size_t i = 0; i < n; i++) {
        off += snprintf(buf + off, sizeof(buf) - off, "%s{\"name\":\"%s\",\"size\":%u}",
                         i > 0 ? "," : "", entries[i].name, (unsigned)entries[i].size_bytes);
    }
    buf[off++] = ']';
    buf[off] = '\0';

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, off);
}

static esp_err_t file_download_handler(httpd_req_t *req, const char *name)
{
    static uint8_t chunk[HTTP_SERVER_IO_CHUNK];
    // storage_module_read() liest bis buf_len - fuer Streaming groesserer
    // Dateien wird direkt mit fopen gelesen statt ueber die (bounded)
    // Vorschau-API.
    char path[300];
    snprintf(path, sizeof(path), "/storage/%s", name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Datei nicht gefunden");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/octet-stream");
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, (const char *)chunk, n) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t file_upload_handler(httpd_req_t *req, const char *name)
{
    if (req->content_len == 0 || req->content_len > HTTP_SERVER_MAX_UPLOAD_BYTES) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ungueltige oder zu grosse Datei (max. 512 KB)");
        return ESP_FAIL;
    }
    char path[300];
    snprintf(path, sizeof(path), "/storage/%s", name);
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Konnte Datei nicht anlegen");
        return ESP_FAIL;
    }

    static uint8_t chunk[HTTP_SERVER_IO_CHUNK];
    int remaining = req->content_len;
    while (remaining > 0) {
        int to_read = remaining < (int)sizeof(chunk) ? remaining : (int)sizeof(chunk);
        int received = httpd_req_recv(req, (char *)chunk, to_read);
        if (received <= 0) {
            fclose(f);
            remove(path);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload abgebrochen");
            return ESP_FAIL;
        }
        fwrite(chunk, 1, received, f);
        remaining -= received;
    }
    fclose(f);
    ESP_LOGI(TAG, "Datei hochgeladen: %s (%d Bytes)", name, (int)req->content_len);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t file_delete_handler(httpd_req_t *req, const char *name)
{
    if (storage_module_delete(name) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Datei nicht gefunden");
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t files_item_handler(httpd_req_t *req)
{
    const char *name = extract_and_validate_name(req);
    if (name == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ungueltiger Dateiname");
        return ESP_FAIL;
    }
    switch (req->method) {
        case HTTP_GET:    return file_download_handler(req, name);
        case HTTP_POST:   return file_upload_handler(req, name);
        case HTTP_DELETE: return file_delete_handler(req, name);
        default:
            httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, NULL);
            return ESP_FAIL;
    }
}

esp_err_t http_server_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 8;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start fehlgeschlagen: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

    const httpd_uri_t status_uri = { .uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler };
    const httpd_uri_t logs_uri = { .uri = "/api/logs", .method = HTTP_GET, .handler = logs_get_handler };
    const httpd_uri_t files_list_uri = { .uri = "/api/files", .method = HTTP_GET, .handler = files_list_get_handler };
    const httpd_uri_t files_get_uri = { .uri = "/api/files/*", .method = HTTP_GET, .handler = files_item_handler };
    const httpd_uri_t files_post_uri = { .uri = "/api/files/*", .method = HTTP_POST, .handler = files_item_handler };
    const httpd_uri_t files_delete_uri = { .uri = "/api/files/*", .method = HTTP_DELETE, .handler = files_item_handler };

    httpd_register_uri_handler(s_server, &status_uri);
    httpd_register_uri_handler(s_server, &logs_uri);
    httpd_register_uri_handler(s_server, &files_list_uri);
    httpd_register_uri_handler(s_server, &files_get_uri);
    httpd_register_uri_handler(s_server, &files_post_uri);
    httpd_register_uri_handler(s_server, &files_delete_uri);

    ESP_LOGI(TAG, "HTTP-Server gestartet auf Port %d", config.server_port);
    return ESP_OK;
}

bool http_server_is_running(void)
{
    return s_server != NULL;
}
