#include <inttypes.h>
#include "esp_log.h"
#include "board_init.h"
#include "settings.h"
#include "log_sink.h"
#include "nav.h"
#include "dashboard.h"
#include "gpio_module_ui.h"
#include "pwm_module_ui.h"
#include "adc_module_ui.h"
#include "i2c_module_ui.h"
#include "serial_module_ui.h"
#include "system_module.h"
#include "system_module_ui.h"
#include "audio_module_ui.h"
#include "settings_module_ui.h"
#include "network_module_ui.h"
#include "wifi_module.h"
#include "sensors_module_ui.h"
#include "statusbar.h"
#include "esp_lvgl_port.h"

static const char *TAG = "main";

#define LOG_SINK_CAPACITY_BYTES (32 * 1024)

// Speist die persistente Statusleiste mit echten Werten (RAM/Akku/Wi-Fi),
// soweit bereits verfuegbar. Server/USB-Target/SD bleiben "--", bis die
// jeweiligen Module (Phase 5) existieren - bewusst keine erfundenen
// Platzhalterwerte. Wi-Fi bleibt "--", bis der Nutzer den Network-Screen
// mindestens einmal geoeffnet hat (wifi_module_init() laeuft dort bewusst
// lazy, nicht beim Boot - SDIO-Bring-up zum C6 ist eine potenziell riskante
// Hardware-Operation, siehe docs/hardware_reference.md).
static void statusbar_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    system_module_stats_t stats;
    if (system_module_get_stats(&stats) != ESP_OK) {
        return;
    }
    uint8_t ram_pct = stats.heap_total_bytes
        ? (uint8_t)((100 * (stats.heap_total_bytes - stats.heap_free_bytes)) / stats.heap_total_bytes) : 0;
    statusbar_set_ram_percent(ram_pct);
    statusbar_set_battery_voltage(stats.battery_voltage_v, stats.battery_voltage_v >= 0.0f);
    statusbar_set_wifi(wifi_module_get_rssi(), wifi_module_get_state() == WIFI_MODULE_STATE_CONNECTED);
}

void app_main(void)
{
    ESP_ERROR_CHECK(log_sink_init(LOG_SINK_CAPACITY_BYTES));

    ESP_LOGI(TAG, "CyberDeck Tab5 - Phase 1 Foundation Build");
    ESP_ERROR_CHECK(settings_init());
    ESP_ERROR_CHECK(board_init());
    system_module_init();

    const settings_t *cfg = settings_get();
    ESP_LOGI(TAG, "Settings geladen: schema=%" PRIu32 " backlight=%u%%",
             cfg->schema_version, cfg->backlight_percent);
    ESP_ERROR_CHECK(board_set_backlight(cfg->backlight_percent));

    // LVGL-Objekte duerfen nur unter Lock erzeugt werden (esp_lvgl_port
    // haelt einen eigenen Task fuer den Render-/Input-Loop).
    lvgl_port_lock(0);
    nav_init();
    dashboard_register();
    gpio_module_ui_register();
    pwm_module_ui_register();
    adc_module_ui_register();
    i2c_module_ui_register();
    serial_module_ui_register();
    system_module_ui_register();
    audio_module_ui_register();
    settings_module_ui_register();
    network_module_ui_register();
    sensors_module_ui_register();
    nav_show(NAV_SCREEN_DASHBOARD);
    lv_timer_create(statusbar_refresh_timer_cb, 3000, NULL);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Dashboard angezeigt - Phase 1 Foundation bereit");
}
