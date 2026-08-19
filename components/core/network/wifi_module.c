#include "wifi_module.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_check.h"
#include "settings.h"

static const char *TAG = "wifi_module";

static esp_netif_t *s_netif = NULL;
static volatile wifi_module_state_t s_state = WIFI_MODULE_STATE_DISCONNECTED;
static volatile bool s_scanning = false;
static char s_connected_ssid[WIFI_MODULE_SSID_MAX_LEN + 1] = {0};
static int8_t s_rssi_dbm = 0;
static bool s_initialized = false;

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_SCAN_DONE:
            s_scanning = false;
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            s_state = (s_state == WIFI_MODULE_STATE_CONNECTING) ? WIFI_MODULE_STATE_FAILED
                                                                  : WIFI_MODULE_STATE_DISCONNECTED;
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_state = WIFI_MODULE_STATE_CONNECTED;
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            s_rssi_dbm = ap.rssi;
        }
        settings_t cfg = *settings_get();
        strncpy(cfg.wifi_ssid, s_connected_ssid, sizeof(cfg.wifi_ssid) - 1);
        cfg.wifi_ssid[sizeof(cfg.wifi_ssid) - 1] = '\0';
        settings_save(&cfg);
        ESP_LOGI(TAG, "Verbunden mit '%s'", s_connected_ssid);
    }
}

esp_err_t wifi_module_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init fehlgeschlagen");
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_netif = esp_netif_create_default_wifi_sta();
    if (s_netif == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_cfg), TAG, "esp_wifi_init fehlgeschlagen (C6-SDIO-Link?)");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL),
                         TAG, "WIFI_EVENT-Handler-Registrierung fehlgeschlagen");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL),
                         TAG, "IP_EVENT-Handler-Registrierung fehlgeschlagen");

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode fehlgeschlagen");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start fehlgeschlagen");

    s_initialized = true;
    ESP_LOGI(TAG, "Wi-Fi (STA, ueber ESP32-C6/SDIO) initialisiert");
    return ESP_OK;
}

esp_err_t wifi_module_start_scan(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_scanning = true;
    esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err != ESP_OK) {
        s_scanning = false;
    }
    return err;
}

bool wifi_module_is_scanning(void)
{
    return s_scanning;
}

static int cmp_rssi_desc(const void *a, const void *b)
{
    const wifi_ap_record_t *ra = a, *rb = b;
    return (int)rb->rssi - (int)ra->rssi;
}

size_t wifi_module_get_scan_results(wifi_module_scan_result_t *out, size_t max)
{
    if (out == NULL || max == 0) {
        return 0;
    }
    uint16_t count = WIFI_MODULE_MAX_SCAN_RESULTS;
    static wifi_ap_record_t records[WIFI_MODULE_MAX_SCAN_RESULTS];
    if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
        return 0;
    }
    qsort(records, count, sizeof(records[0]), cmp_rssi_desc);

    size_t n = (count < max) ? count : max;
    for (size_t i = 0; i < n; i++) {
        strncpy(out[i].ssid, (const char *)records[i].ssid, WIFI_MODULE_SSID_MAX_LEN);
        out[i].ssid[WIFI_MODULE_SSID_MAX_LEN] = '\0';
        out[i].rssi_dbm = records[i].rssi;
        out[i].secured = records[i].authmode != WIFI_AUTH_OPEN;
    }
    return n;
}

esp_err_t wifi_module_connect(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    if (password != NULL) {
        strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    }
    wifi_cfg.sta.threshold.authmode = (password != NULL && password[0] != '\0')
        ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    strncpy(s_connected_ssid, ssid, sizeof(s_connected_ssid) - 1);
    s_connected_ssid[sizeof(s_connected_ssid) - 1] = '\0';

    esp_wifi_disconnect();  // vorherige Verbindung sauber trennen, Fehler hier egal
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "esp_wifi_set_config fehlgeschlagen");
    s_state = WIFI_MODULE_STATE_CONNECTING;
    return esp_wifi_connect();
}

void wifi_module_disconnect(void)
{
    esp_wifi_disconnect();
    s_state = WIFI_MODULE_STATE_DISCONNECTED;
    s_connected_ssid[0] = '\0';
}

wifi_module_state_t wifi_module_get_state(void)
{
    return s_state;
}

const char *wifi_module_get_connected_ssid(void)
{
    return s_connected_ssid;
}

int8_t wifi_module_get_rssi(void)
{
    return s_rssi_dbm;
}

bool wifi_module_get_ip_str(char *buf, size_t buf_len)
{
    if (s_state != WIFI_MODULE_STATE_CONNECTED || s_netif == NULL) {
        return false;
    }
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_netif, &ip_info) != ESP_OK) {
        return false;
    }
    snprintf(buf, buf_len, IPSTR, IP2STR(&ip_info.ip));
    return true;
}
