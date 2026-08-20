#include "storage_module.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_spiffs.h"
#include "esp_log.h"

#define STORAGE_MOUNT_POINT "/storage"
#define STORAGE_PARTITION_LABEL "storage"

static const char *TAG = "storage_module";
static bool s_mounted = false;

esp_err_t storage_module_init(void)
{
    if (s_mounted) {
        return ESP_OK;
    }
    const esp_vfs_spiffs_conf_t conf = {
        .base_path = STORAGE_MOUNT_POINT,
        .partition_label = STORAGE_PARTITION_LABEL,
        .max_files = 6,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS-Mount fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }
    s_mounted = true;
    size_t total = 0, used = 0;
    esp_spiffs_info(STORAGE_PARTITION_LABEL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS gemountet auf %s (%u/%u Bytes belegt)",
             STORAGE_MOUNT_POINT, (unsigned)used, (unsigned)total);
    return ESP_OK;
}

bool storage_module_name_is_safe(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    size_t len = strlen(name);
    if (len >= STORAGE_MODULE_MAX_NAME_LEN) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (c == '/' || c == '\\' || c == '"' || (unsigned char)c < 0x20) {
            return false;  // Pfadtrennzeichen/Anfuehrungszeichen (JSON-Ausgabe)/Steuerzeichen ausschliessen
        }
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return false;
    }
    return true;
}

// Puffer bewusst groesser als STORAGE_MOUNT_POINT + STORAGE_MODULE_MAX_NAME_LEN:
// name kommt bei storage_module_list() aus struct dirent.d_name (bis zu 255
// Byte), GCCs -Wformat-truncation kennt die tatsaechliche Laufzeit-Begrenzung
// durch storage_module_name_is_safe() nicht und warnt sonst als Fehler.
static void full_path(char *out, size_t out_len, const char *name)
{
    snprintf(out, out_len, "%s/%s", STORAGE_MOUNT_POINT, name);
}

size_t storage_module_list(storage_module_entry_t *out, size_t max)
{
    if (!s_mounted) {
        return 0;
    }
    DIR *dir = opendir(STORAGE_MOUNT_POINT);
    if (dir == NULL) {
        return 0;
    }
    size_t count = 0;
    struct dirent *ent;
    while (count < max && (ent = readdir(dir)) != NULL) {
        char path[300];
        full_path(path, sizeof(path), ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        strncpy(out[count].name, ent->d_name, STORAGE_MODULE_MAX_NAME_LEN - 1);
        out[count].name[STORAGE_MODULE_MAX_NAME_LEN - 1] = '\0';
        out[count].size_bytes = (size_t)st.st_size;
        count++;
    }
    closedir(dir);
    return count;
}

esp_err_t storage_module_read(const char *name, uint8_t *buf, size_t buf_len, size_t *out_len)
{
    if (!s_mounted || !storage_module_name_is_safe(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[300];
    full_path(path, sizeof(path), name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t n = fread(buf, 1, buf_len, f);
    fclose(f);
    if (out_len != NULL) {
        *out_len = n;
    }
    return ESP_OK;
}

esp_err_t storage_module_delete(const char *name)
{
    if (!s_mounted || !storage_module_name_is_safe(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[300];
    full_path(path, sizeof(path), name);
    return (remove(path) == 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t storage_module_get_usage(size_t *out_used_bytes, size_t *out_total_bytes)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_spiffs_info(STORAGE_PARTITION_LABEL, out_total_bytes, out_used_bytes);
}
