#include "storage.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_spiffs.h"
#include "esp_log.h"

static const char *TAG = "storage";
static const char *SPIFFS_LABEL = "storage";
static bool s_mounted = false;

esp_err_t storage_init(void)
{
    if (s_mounted) {
        return ESP_OK;
    }
    esp_vfs_spiffs_conf_t conf = {
        .base_path = STORAGE_MOUNT_POINT,
        .partition_label = SPIFFS_LABEL,
        .max_files = 8,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_spiffs_register fehlgeschlagen (err=%d)", err);
        return err;
    }
    s_mounted = true;
    ESP_LOGI(TAG, "SPIFFS auf '%s' gemountet", STORAGE_MOUNT_POINT);
    return ESP_OK;
}

bool storage_get_usage(size_t *used_bytes, size_t *total_bytes)
{
    if (!s_mounted) {
        return false;
    }
    return esp_spiffs_info(SPIFFS_LABEL, total_bytes, used_bytes) == ESP_OK;
}

size_t storage_list_files(storage_file_t *out, size_t max)
{
    if (!s_mounted || out == NULL || max == 0) {
        return 0;
    }
    DIR *dir = opendir(STORAGE_MOUNT_POINT);
    if (dir == NULL) {
        return 0;
    }
    size_t n = 0;
    struct dirent *entry;
    while (n < max && (entry = readdir(dir)) != NULL) {
        char path[sizeof(STORAGE_MOUNT_POINT) + sizeof(entry->d_name) + 1];
        snprintf(path, sizeof(path), "%s/%s", STORAGE_MOUNT_POINT, entry->d_name);
        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }
        strncpy(out[n].name, entry->d_name, STORAGE_MAX_NAME_LEN - 1);
        out[n].name[STORAGE_MAX_NAME_LEN - 1] = '\0';
        out[n].size_bytes = (size_t)st.st_size;
        n++;
    }
    closedir(dir);
    return n;
}

bool storage_build_path(const char *name, char *out_path, size_t out_len)
{
    if (name == NULL || name[0] == '\0' || strlen(name) >= STORAGE_MAX_NAME_LEN) {
        return false;
    }
    if (strchr(name, '/') != NULL || strstr(name, "..") != NULL) {
        return false;
    }
    int written = snprintf(out_path, out_len, "%s/%s", STORAGE_MOUNT_POINT, name);
    return written > 0 && (size_t)written < out_len;
}

esp_err_t storage_delete_file(const char *name)
{
    char path[STORAGE_MAX_NAME_LEN + sizeof(STORAGE_MOUNT_POINT) + 1];
    if (!storage_build_path(name, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }
    return (remove(path) == 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}
