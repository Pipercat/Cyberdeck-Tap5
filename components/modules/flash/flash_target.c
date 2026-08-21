#include "flash_target.h"
#include <string.h>
#include <inttypes.h>
#include "usb_serial.h"
#include "esp_loader.h"
#include "esp_loader_io.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "FLASH_TARGET";

static int64_t s_timer_start_us = 0;
static uint32_t s_timer_duration_ms = 0;
static bool s_connected = false;

const char *flash_target_chip_name(flash_target_chip_t chip)
{
    switch (chip) {
    case FLASH_TARGET_CHIP_ESP32:   return "ESP32";
    case FLASH_TARGET_CHIP_ESP32S2: return "ESP32-S2";
    case FLASH_TARGET_CHIP_ESP32S3: return "ESP32-S3";
    case FLASH_TARGET_CHIP_ESP32C2: return "ESP32-C2";
    case FLASH_TARGET_CHIP_ESP32C3: return "ESP32-C3";
    case FLASH_TARGET_CHIP_ESP32C5: return "ESP32-C5";
    case FLASH_TARGET_CHIP_ESP32C6: return "ESP32-C6";
    case FLASH_TARGET_CHIP_ESP32H2: return "ESP32-H2";
    case FLASH_TARGET_CHIP_ESP32P4: return "ESP32-P4";
    default:                        return "Unknown";
    }
}

static flash_target_chip_t map_chip(target_chip_t chip)
{
    switch (chip) {
    case ESP32_CHIP:   return FLASH_TARGET_CHIP_ESP32;
    case ESP32S2_CHIP: return FLASH_TARGET_CHIP_ESP32S2;
    case ESP32S3_CHIP: return FLASH_TARGET_CHIP_ESP32S3;
    case ESP32C2_CHIP: return FLASH_TARGET_CHIP_ESP32C2;
    case ESP32C3_CHIP: return FLASH_TARGET_CHIP_ESP32C3;
    case ESP32C6_CHIP: return FLASH_TARGET_CHIP_ESP32C6;
    case ESP32H2_CHIP: return FLASH_TARGET_CHIP_ESP32H2;
#ifdef ESP32P4_CHIP
    case ESP32P4_CHIP: return FLASH_TARGET_CHIP_ESP32P4;
#endif
#ifdef ESP32C5_CHIP
    case ESP32C5_CHIP: return FLASH_TARGET_CHIP_ESP32C5;
#endif
    default:           return FLASH_TARGET_CHIP_UNKNOWN;
    }
}

// --- loader_port_*: von esp-serial-flasher aufgerufene Transport-Callbacks
// (siehe esp-serial-flasher "porting guide") - Implementierung ueber
// usb_serial.h statt eines UART-Treibers. ---

void loader_port_start_timer(uint32_t time_ms)
{
    s_timer_start_us = esp_timer_get_time();
    s_timer_duration_ms = time_ms;
}

uint32_t loader_port_remaining_time(void)
{
    int64_t elapsed_ms = (esp_timer_get_time() - s_timer_start_us) / 1000;
    if (elapsed_ms >= s_timer_duration_ms) {
        return 0;
    }
    return s_timer_duration_ms - (uint32_t)elapsed_ms;
}

void loader_port_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// Klassische esptool-Auto-Reset-Sequenz (siehe Header-Kommentar): DTR haelt
// GPIO0 fest (aktiv-low ueber Inverter -> DTR=1 zieht IO0 LOW), RTS haelt EN
// (aktiv-low -> RTS=1 zieht EN/Reset LOW).
void loader_port_enter_bootloader(void)
{
    usb_serial_set_control_lines(false, true);   // IO0=HIGH, EN=LOW (Reset gehalten)
    loader_port_delay_ms(100);
    usb_serial_set_control_lines(true, false);   // IO0=LOW (Download-Strap), EN=HIGH (Reset freigegeben)
    loader_port_delay_ms(50);
    usb_serial_set_control_lines(false, false);  // IO0 wieder freigeben
}

void loader_port_reset_target(void)
{
    usb_serial_set_control_lines(false, true);   // EN=LOW
    loader_port_delay_ms(100);
    usb_serial_set_control_lines(false, false);  // EN=HIGH, normaler Boot (IO0 unberuehrt)
}

esp_loader_error_t loader_port_read(uint8_t *data, uint16_t size, uint32_t timeout)
{
    uint32_t waited_ms = 0;
    size_t received = 0;
    while (received < size) {
        size_t n = usb_serial_read(data + received, size - received);
        received += n;
        if (received >= size) {
            break;
        }
        if (n == 0) {
            if (waited_ms >= timeout) {
                return ESP_LOADER_ERROR_TIMEOUT;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
            waited_ms += 2;
        }
    }
    return ESP_LOADER_SUCCESS;
}

esp_loader_error_t loader_port_write(const uint8_t *data, uint16_t size, uint32_t timeout)
{
    (void)timeout;  // usb_serial_write() blockiert intern mit eigenem Timeout (siehe usb_serial.c)
    return (usb_serial_write(data, size) == ESP_OK) ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_FAIL;
}

esp_loader_error_t loader_port_change_transmission_rate(uint32_t baudrate)
{
    return (usb_serial_set_baud(baudrate) == ESP_OK) ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_FAIL;
}

void loader_port_debug_print(const char *str)
{
    ESP_LOGD(TAG, "%s", str);
}

// --- Oeffentliche flash_target_*-API, genutzt vom Flash Manager. ---

esp_err_t flash_target_connect(uint32_t baud_rate, flash_target_chip_t *out_chip)
{
    if (!usb_serial_is_open()) {
        esp_err_t err = usb_serial_open(115200);  // Bootloader-ROM startet immer bei 115200
        if (err != ESP_OK) {
            return err;
        }
    }

    esp_loader_connect_args_t connect_args = ESP_LOADER_CONNECT_DEFAULT();
    esp_loader_error_t err = esp_loader_connect(&connect_args);
    if (err != ESP_LOADER_SUCCESS) {
        ESP_LOGW(TAG, "esp_loader_connect fehlgeschlagen (err=%d) - Bootloader-Sync nicht erreicht", err);
        return ESP_ERR_TIMEOUT;
    }

    target_chip_t chip = esp_loader_get_target();
    if (out_chip != NULL) {
        *out_chip = map_chip(chip);
    }

    if (baud_rate != 115200) {
        if (esp_loader_change_baudrate(baud_rate) != ESP_LOADER_SUCCESS) {
            ESP_LOGW(TAG, "Baudratenwechsel auf %" PRIu32 " fehlgeschlagen, bleibe bei 115200", baud_rate);
        }
    }

    s_connected = true;
    ESP_LOGI(TAG, "Bootloader-Sync erfolgreich, Chip=%s", flash_target_chip_name(map_chip(chip)));
    return ESP_OK;
}

esp_err_t flash_target_begin_region(uint32_t address, uint32_t total_size)
{
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_loader_error_t err = esp_loader_flash_start(address, total_size, 4096);
    return (err == ESP_LOADER_SUCCESS) ? ESP_OK : ESP_FAIL;
}

esp_err_t flash_target_write_chunk(const uint8_t *data, size_t len)
{
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_loader_error_t err = esp_loader_flash_write((void *)data, (uint32_t)len);
    return (err == ESP_LOADER_SUCCESS) ? ESP_OK : ESP_FAIL;
}

esp_err_t flash_target_end_region(void)
{
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_loader_error_t err = esp_loader_flash_verify();
    if (err != ESP_LOADER_SUCCESS) {
        ESP_LOGW(TAG, "MD5-Verify fehlgeschlagen/nicht unterstuetzt (err=%d)", err);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t flash_target_finish(bool reboot)
{
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_loader_error_t err = esp_loader_flash_finish(reboot);
    s_connected = false;
    return (err == ESP_LOADER_SUCCESS) ? ESP_OK : ESP_FAIL;
}

esp_err_t flash_target_reset_normal(void)
{
    if (!usb_serial_is_open()) {
        esp_err_t err = usb_serial_open(115200);
        if (err != ESP_OK) {
            return err;
        }
    }
    loader_port_reset_target();
    return ESP_OK;
}

void flash_target_disconnect(void)
{
    s_connected = false;
    usb_serial_close();
}
