#include "i2c_module.h"
#include "hal_gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "i2c_module";
#define OWNER_TAG "i2c_module"
#define EXT_I2C_SDA GPIO_NUM_53
#define EXT_I2C_SCL GPIO_NUM_54
#define EXT_I2C_PORT I2C_NUM_1  // Port 0 ist der interne Systembus (board_init.c)

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_module_speed_t s_speed = I2C_MODULE_SPEED_100K;

esp_err_t i2c_module_init(i2c_module_speed_t speed)
{
    if (s_bus != NULL && speed == s_speed) {
        return ESP_OK;  // schon mit gewuenschter Geschwindigkeit initialisiert
    }
    i2c_module_deinit();

    esp_err_t err = hal_gpio_request((int)EXT_I2C_SDA, HAL_GPIO_MODE_INPUT, OWNER_TAG);
    if (err != ESP_OK) return err;
    err = hal_gpio_request((int)EXT_I2C_SCL, HAL_GPIO_MODE_INPUT, OWNER_TAG);
    if (err != ESP_OK) {
        hal_gpio_release((int)EXT_I2C_SDA);
        return err;
    }

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = EXT_I2C_PORT,
        .sda_io_num = EXT_I2C_SDA,
        .scl_io_num = EXT_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus fehlgeschlagen: %s", esp_err_to_name(err));
        hal_gpio_release((int)EXT_I2C_SDA);
        hal_gpio_release((int)EXT_I2C_SCL);
        s_bus = NULL;
        return err;
    }
    s_speed = speed;
    return ESP_OK;
}

void i2c_module_deinit(void)
{
    if (s_bus != NULL) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        hal_gpio_release((int)EXT_I2C_SDA);
        hal_gpio_release((int)EXT_I2C_SCL);
    }
}

esp_err_t i2c_module_scan(uint8_t *found_addrs, size_t max_count, size_t *out_count)
{
    if (s_bus == NULL || found_addrs == NULL || out_count == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t count = 0;
    for (uint8_t addr = 0x08; addr <= 0x77 && count < max_count; addr++) {
        if (i2c_master_probe(s_bus, addr, 30) == ESP_OK) {
            found_addrs[count++] = addr;
        }
    }
    *out_count = count;
    return ESP_OK;
}

static esp_err_t add_device(uint8_t addr, i2c_master_dev_handle_t *out_dev)
{
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = s_speed,
    };
    return i2c_master_bus_add_device(s_bus, &dev_cfg, out_dev);
}

esp_err_t i2c_module_read_reg(uint8_t addr, uint8_t reg, uint8_t *out_val)
{
    if (s_bus == NULL || out_val == NULL) return ESP_ERR_INVALID_STATE;
    i2c_master_dev_handle_t dev;
    esp_err_t err = add_device(addr, &dev);
    if (err != ESP_OK) return err;
    err = i2c_master_transmit_receive(dev, &reg, 1, out_val, 1, 200);
    i2c_master_bus_rm_device(dev);
    return err;
}

esp_err_t i2c_module_write_reg(uint8_t addr, uint8_t reg, uint8_t val)
{
    if (s_bus == NULL) return ESP_ERR_INVALID_STATE;
    i2c_master_dev_handle_t dev;
    esp_err_t err = add_device(addr, &dev);
    if (err != ESP_OK) return err;
    uint8_t buf[2] = { reg, val };
    err = i2c_master_transmit(dev, buf, 2, 200);
    i2c_master_bus_rm_device(dev);
    return err;
}

typedef struct { uint8_t addr; const char *name; } known_device_t;

// Allgemein bekannte, haeufig genutzte I2C-Adressen (Embedded-Standardwissen,
// nicht Tab5-spezifisch). Reine Vermutung anhand der Adresse - mehrere
// Chips koennen dieselbe Adresse teilen, daher immer als "moegl." markiert.
static const known_device_t k_known_devices[] = {
    { 0x3C, "OLED (SSD1306)" },
    { 0x3D, "OLED (SSD1306)" },
    { 0x40, "Sensor/PWM (INA2xx, PCA9685, Si70xx)" },
    { 0x48, "ADC/Temp (ADS1115, TMP102)" },
    { 0x49, "ADC/Temp (ADS1115, TMP102)" },
    { 0x50, "EEPROM (24Cxx)" },
    { 0x57, "EEPROM (24Cxx)" },
    { 0x5A, "IR-Temp (MLX90614)" },
    { 0x68, "IMU/RTC (MPU6050, DS1307/3231)" },
    { 0x69, "IMU (MPU9250 o.ae.)" },
    { 0x76, "Druck/Temp (BMP280/BME280)" },
    { 0x77, "Druck/Temp (BMP280/BME280)" },
};

const char *i2c_module_guess_device_name(uint8_t addr)
{
    if (addr >= 0x20 && addr <= 0x27) return "IO-Expander (PCF8574)";
    for (size_t i = 0; i < sizeof(k_known_devices) / sizeof(k_known_devices[0]); i++) {
        if (k_known_devices[i].addr == addr) {
            return k_known_devices[i].name;
        }
    }
    return NULL;
}
