#include "settings.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "settings";
#define NVS_NAMESPACE "cyberdeck_cfg"
#define NVS_KEY_BLOB   "settings_v1"

static settings_t s_settings;
static bool s_loaded = false;

static void apply_defaults(settings_t *s)
{
    memset(s, 0, sizeof(*s));
    s->schema_version = SETTINGS_SCHEMA_VERSION;
    s->backlight_percent = 80;
    s->dark_mode = true;
    s->remote_access_enabled = false;
    s->remote_require_pairing = true;
    strncpy(s->device_name, "CyberDeck-01", sizeof(s->device_name) - 1);
}

static esp_err_t load_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Kein gespeichertes Settings-Blob - verwende Werkswerte");
        apply_defaults(&s_settings);
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_open fehlgeschlagen");

    size_t required_size = sizeof(s_settings);
    err = nvs_get_blob(handle, NVS_KEY_BLOB, &s_settings, &required_size);
    nvs_close(handle);

    if (err != ESP_OK || required_size != sizeof(s_settings) ||
        s_settings.schema_version != SETTINGS_SCHEMA_VERSION) {
        ESP_LOGW(TAG, "Settings-Blob fehlt/inkompatibel (err=%s) - verwende Werkswerte",
                 esp_err_to_name(err));
        apply_defaults(&s_settings);
    }
    return ESP_OK;
}

esp_err_t settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS-Partition wird neu initialisiert (%s)", esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs_flash_erase fehlgeschlagen");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_flash_init fehlgeschlagen");

    ESP_RETURN_ON_ERROR(load_from_nvs(), TAG, "Laden der Settings fehlgeschlagen");
    s_loaded = true;
    return ESP_OK;
}

const settings_t *settings_get(void)
{
    if (!s_loaded) {
        ESP_LOGE(TAG, "settings_get() vor settings_init() aufgerufen");
    }
    return &s_settings;
}

esp_err_t settings_save(const settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_settings = *settings;
    s_settings.schema_version = SETTINGS_SCHEMA_VERSION;

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open fehlgeschlagen");
    esp_err_t err = nvs_set_blob(handle, NVS_KEY_BLOB, &s_settings, sizeof(s_settings));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t settings_reset_defaults(void)
{
    apply_defaults(&s_settings);
    return settings_save(&s_settings);
}
