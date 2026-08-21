#include "remote_protocol.h"
#include "esp_rom_crc.h"
#include "esp_app_desc.h"
#include "settings.h"

cJSON *remote_protocol_new_envelope(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "protocol", REMOTE_PROTOCOL_VERSION);
    cJSON_AddStringToObject(root, "device", settings_get()->device_name);
    const esp_app_desc_t *app_desc = esp_app_get_description();
    cJSON_AddStringToObject(root, "firmware", (app_desc != NULL) ? app_desc->version : "?");
    return root;
}

uint32_t remote_protocol_crc32(const uint8_t *data, size_t len)
{
    return esp_rom_crc32_le(0, data, len);
}
