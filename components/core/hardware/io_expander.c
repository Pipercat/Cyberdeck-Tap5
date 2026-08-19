#include "io_expander.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "io_expander";

#define I2C_DEV_ADDR_PI4IOE1 0x43
#define I2C_DEV_ADDR_PI4IOE2 0x44
#define I2C_TIMEOUT_MS       100

#define PI4IO_REG_CHIP_RESET 0x01
#define PI4IO_REG_IO_DIR     0x03
#define PI4IO_REG_OUT_SET    0x05
#define PI4IO_REG_OUT_H_IM   0x07
#define PI4IO_REG_IN_DEF_STA 0x09
#define PI4IO_REG_PULL_EN    0x0B
#define PI4IO_REG_PULL_SEL   0x0D
#define PI4IO_REG_INT_MASK   0x11

static esp_err_t write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(dev, buf, 2, I2C_TIMEOUT_MS);
}

esp_err_t io_expander_init(i2c_master_bus_handle_t bus_handle)
{
    i2c_device_config_t cfg1 = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2C_DEV_ADDR_PI4IOE1,
        .scl_speed_hz = 400000,
    };
    i2c_master_dev_handle_t dev1;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_handle, &cfg1, &dev1), TAG, "PI4IOE1 add_device");

    // PI4IOE1 (0x43): P1=SPK_EN, P2=EXT5V_EN, P4=LCD_RST, P5=TP_RST, P6=CAM_RST
    ESP_RETURN_ON_ERROR(write_reg(dev1, PI4IO_REG_CHIP_RESET, 0xFF), TAG, "PI4IOE1 chip reset");
    vTaskDelay(pdMS_TO_TICKS(10)); // Settling-Zeit nach Chip-Reset (nicht in Referenz dokumentiert, aber ueblich)
    ESP_RETURN_ON_ERROR(write_reg(dev1, PI4IO_REG_IO_DIR, 0b01111111), TAG, "PI4IOE1 io dir");     // P0-P6 output
    ESP_RETURN_ON_ERROR(write_reg(dev1, PI4IO_REG_OUT_H_IM, 0b00000000), TAG, "PI4IOE1 high-z");
    ESP_RETURN_ON_ERROR(write_reg(dev1, PI4IO_REG_PULL_SEL, 0b01111111), TAG, "PI4IOE1 pull sel");
    ESP_RETURN_ON_ERROR(write_reg(dev1, PI4IO_REG_PULL_EN, 0b01111111), TAG, "PI4IOE1 pull en");
    // P1,P2,P4,P5,P6 High -> SPK_EN, EXT5V_EN, LCD_RST, TP_RST, CAM_RST entlassen
    ESP_RETURN_ON_ERROR(write_reg(dev1, PI4IO_REG_OUT_SET, 0b01110110), TAG, "PI4IOE1 out set");

    i2c_device_config_t cfg2 = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2C_DEV_ADDR_PI4IOE2,
        .scl_speed_hz = 400000,
    };
    i2c_master_dev_handle_t dev2;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_handle, &cfg2, &dev2), TAG, "PI4IOE2 add_device");

    // PI4IOE2 (0x44): P0=WLAN_PWR_EN, P3=USB5V_EN, P7=CHG_EN
    ESP_RETURN_ON_ERROR(write_reg(dev2, PI4IO_REG_CHIP_RESET, 0xFF), TAG, "PI4IOE2 chip reset");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(write_reg(dev2, PI4IO_REG_IO_DIR, 0b10111001), TAG, "PI4IOE2 io dir");
    ESP_RETURN_ON_ERROR(write_reg(dev2, PI4IO_REG_OUT_H_IM, 0b00000110), TAG, "PI4IOE2 high-z");
    ESP_RETURN_ON_ERROR(write_reg(dev2, PI4IO_REG_PULL_SEL, 0b10111001), TAG, "PI4IOE2 pull sel");
    ESP_RETURN_ON_ERROR(write_reg(dev2, PI4IO_REG_PULL_EN, 0b11111001), TAG, "PI4IOE2 pull en");
    ESP_RETURN_ON_ERROR(write_reg(dev2, PI4IO_REG_IN_DEF_STA, 0b01000000), TAG, "PI4IOE2 in def");
    ESP_RETURN_ON_ERROR(write_reg(dev2, PI4IO_REG_INT_MASK, 0b10111111), TAG, "PI4IOE2 int mask");
    // P0,P3 High -> WLAN_PWR_EN, USB5V_EN entlassen (CHG_EN/P7 bleibt aus,
    // exakt wie in der M5Stack-Werksfirmware)
    ESP_RETURN_ON_ERROR(write_reg(dev2, PI4IO_REG_OUT_SET, 0b00001001), TAG, "PI4IOE2 out set");

    ESP_LOGI(TAG, "PI4IOE5V6408 (0x43/0x44) initialisiert - LCD/TP/CAM-Reset entlassen");
    return ESP_OK;
}
