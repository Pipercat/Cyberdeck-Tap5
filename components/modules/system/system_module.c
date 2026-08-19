#include "system_module.h"
#include "board_init.h"
#include "self_test.h"
#include "driver/i2c_master.h"
#include "driver/temperature_sensor.h"
#include "esp_heap_caps.h"
#include "esp_flash.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"

static const char *TAG = "system_module";

#define INA226_ADDR         0x41
#define INA226_REG_BUS_V    0x02
#define INA226_BUS_V_LSB_UV 1250  // 1.25 mV pro LSB (INA226-Standarddatenblatt)

static temperature_sensor_handle_t s_temp_handle = NULL;

static float read_battery_voltage(void);

static self_test_status_t selftest_display(const char **out_detail)
{
    if (board_get_display() == NULL) {
        *out_detail = "board_get_display() == NULL";
        return SELF_TEST_FAIL;
    }
    return SELF_TEST_PASS;
}

static self_test_status_t selftest_system_i2c(const char **out_detail)
{
    if (board_get_system_i2c_bus() == NULL) {
        *out_detail = "System-I2C-Bus nicht initialisiert";
        return SELF_TEST_FAIL;
    }
    return SELF_TEST_PASS;
}

static self_test_status_t selftest_psram(const char **out_detail)
{
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) {
        *out_detail = "Kein PSRAM erkannt";
        return SELF_TEST_FAIL;
    }
    return SELF_TEST_PASS;
}

static self_test_status_t selftest_battery_sensor(const char **out_detail)
{
    if (read_battery_voltage() < 0.0f) {
        *out_detail = "INA226 (0x41) nicht erreichbar";
        return SELF_TEST_FAIL;
    }
    return SELF_TEST_PASS;
}

esp_err_t system_module_init(void)
{
    self_test_register("Display", selftest_display);
    self_test_register("System I2C Bus", selftest_system_i2c);
    self_test_register("PSRAM", selftest_psram);
    self_test_register("Battery Sensor (INA226)", selftest_battery_sensor);

    const temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
    esp_err_t err = temperature_sensor_install(&cfg, &s_temp_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Temperatursensor-Init fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }
    return temperature_sensor_enable(s_temp_handle);
}

static float read_battery_voltage(void)
{
    i2c_master_bus_handle_t bus = board_get_system_i2c_bus();
    if (bus == NULL) return -1.0f;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = INA226_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) != ESP_OK) {
        return -1.0f;
    }

    uint8_t reg = INA226_REG_BUS_V;
    uint8_t raw[2] = { 0 };
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, raw, 2, 200);
    i2c_master_bus_rm_device(dev);
    if (err != ESP_OK) {
        return -1.0f;
    }

    uint16_t value = ((uint16_t)raw[0] << 8) | raw[1];
    return (value * INA226_BUS_V_LSB_UV) / 1000000.0f;
}

esp_err_t system_module_get_stats(system_module_stats_t *out_stats)
{
    if (out_stats == NULL) return ESP_ERR_INVALID_ARG;

    out_stats->heap_free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out_stats->heap_total_bytes = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    out_stats->psram_free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    out_stats->psram_total_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    out_stats->flash_size_bytes = flash_size;

    out_stats->uptime_s = esp_timer_get_time() / 1000000;

    float temp_c = 0.0f;
    if (s_temp_handle != NULL && temperature_sensor_get_celsius(s_temp_handle, &temp_c) == ESP_OK) {
        out_stats->chip_temp_c = temp_c;
    } else {
        out_stats->chip_temp_c = -1000.0f;  // Sentinel: nicht verfuegbar
    }

    out_stats->battery_voltage_v = read_battery_voltage();

    out_stats->idf_version = esp_get_idf_version();
    const esp_app_desc_t *app_desc = esp_app_get_description();
    out_stats->project_version = (app_desc != NULL) ? app_desc->version : "?";

    return ESP_OK;
}

void system_module_restart_device(void)
{
    ESP_LOGW(TAG, "Neustart angefordert");
    esp_restart();
}
