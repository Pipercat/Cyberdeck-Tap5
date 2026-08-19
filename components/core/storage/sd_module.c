#include "sd_module.h"
#include <string.h>
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "esp_log.h"

static const char *TAG = "sd_module";

#define GPIO_SDMMC_CLK 43
#define GPIO_SDMMC_CMD 44
#define GPIO_SDMMC_D0  39
#define GPIO_SDMMC_D1  40
#define GPIO_SDMMC_D2  41
#define GPIO_SDMMC_D3  42
#define SD_LDO_CHAN_ID 4  // LDO_VO4 versorgt die SDMMC-IO-Spannung auf dem ESP32-P4

static sdmmc_card_t *s_card = NULL;
static sd_pwr_ctrl_handle_t s_pwr_ctrl_handle = NULL;

esp_err_t sd_module_mount(sd_module_status_t *out_status)
{
    if (out_status != NULL) {
        memset(out_status, 0, sizeof(*out_status));
    }

    if (s_card != NULL) {
        if (out_status != NULL) {
            out_status->mounted = true;
            strncpy(out_status->card_name, s_card->cid.name, sizeof(out_status->card_name) - 1);
            out_status->capacity_bytes = (uint64_t)s_card->csd.capacity * s_card->csd.sector_size;
        }
        return ESP_OK;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    if (s_pwr_ctrl_handle == NULL) {
        sd_pwr_ctrl_ldo_config_t ldo_cfg = { .ldo_chan_id = SD_LDO_CHAN_ID };
        esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &s_pwr_ctrl_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "sd_pwr_ctrl_new_on_chip_ldo fehlgeschlagen: %s", esp_err_to_name(err));
            return err;
        }
    }
    host.pwr_ctrl_handle = s_pwr_ctrl_handle;

    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.width = 4;
    slot_cfg.clk = GPIO_SDMMC_CLK;
    slot_cfg.cmd = GPIO_SDMMC_CMD;
    slot_cfg.d0 = GPIO_SDMMC_D0;
    slot_cfg.d1 = GPIO_SDMMC_D1;
    slot_cfg.d2 = GPIO_SDMMC_D2;
    slot_cfg.d3 = GPIO_SDMMC_D3;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,  // vorhandene Kartendaten nicht antasten
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MODULE_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD-Mount fehlgeschlagen: %s (keine Karte eingelegt oder inkompatibel)",
                 esp_err_to_name(err));
        s_card = NULL;
        return err;
    }

    ESP_LOGI(TAG, "SD-Karte gemountet: %s", s_card->cid.name);
    if (out_status != NULL) {
        out_status->mounted = true;
        strncpy(out_status->card_name, s_card->cid.name, sizeof(out_status->card_name) - 1);
        out_status->capacity_bytes = (uint64_t)s_card->csd.capacity * s_card->csd.sector_size;
    }
    return ESP_OK;
}

void sd_module_unmount(void)
{
    if (s_card == NULL) {
        return;
    }
    esp_vfs_fat_sdcard_unmount(SD_MODULE_MOUNT_POINT, s_card);
    s_card = NULL;
}

bool sd_module_is_mounted(void)
{
    return s_card != NULL;
}
