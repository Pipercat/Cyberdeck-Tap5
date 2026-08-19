#include "serial_module.h"
#include "hal_gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "serial_module";
#define OWNER_TAG "serial_module"
#define SERIAL_UART_PORT UART_NUM_1
#define SERIAL_RX_BUF_SIZE 2048

static bool s_running = false;
static int s_tx_gpio = -1;
static int s_rx_gpio = -1;

esp_err_t serial_module_start(int tx_gpio, int rx_gpio, int baud_rate)
{
    serial_module_stop();

    esp_err_t err = hal_gpio_request(tx_gpio, HAL_GPIO_MODE_OUTPUT, OWNER_TAG);
    if (err != ESP_OK) return err;
    err = hal_gpio_request(rx_gpio, HAL_GPIO_MODE_INPUT, OWNER_TAG);
    if (err != ESP_OK) {
        hal_gpio_release(tx_gpio);
        return err;
    }

    const uart_config_t uart_cfg = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    err = uart_driver_install(SERIAL_UART_PORT, SERIAL_RX_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK) goto fail;
    err = uart_param_config(SERIAL_UART_PORT, &uart_cfg);
    if (err != ESP_OK) goto fail_uninstall;
    err = uart_set_pin(SERIAL_UART_PORT, tx_gpio, rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) goto fail_uninstall;

    s_tx_gpio = tx_gpio;
    s_rx_gpio = rx_gpio;
    s_running = true;
    ESP_LOGI(TAG, "UART gestartet: TX=%d RX=%d Baud=%d", tx_gpio, rx_gpio, baud_rate);
    return ESP_OK;

fail_uninstall:
    uart_driver_delete(SERIAL_UART_PORT);
fail:
    hal_gpio_release(tx_gpio);
    hal_gpio_release(rx_gpio);
    return err;
}

esp_err_t serial_module_stop(void)
{
    if (!s_running) return ESP_OK;
    uart_driver_delete(SERIAL_UART_PORT);
    hal_gpio_release(s_tx_gpio);
    hal_gpio_release(s_rx_gpio);
    s_tx_gpio = -1;
    s_rx_gpio = -1;
    s_running = false;
    return ESP_OK;
}

bool serial_module_is_running(void)
{
    return s_running;
}

esp_err_t serial_module_write(const uint8_t *data, size_t len)
{
    if (!s_running) return ESP_ERR_INVALID_STATE;
    int written = uart_write_bytes(SERIAL_UART_PORT, (const char *)data, len);
    return written == (int)len ? ESP_OK : ESP_FAIL;
}

size_t serial_module_read(uint8_t *buf, size_t max_len)
{
    if (!s_running) return 0;
    int len = uart_read_bytes(SERIAL_UART_PORT, buf, max_len, 0);
    return len > 0 ? (size_t)len : 0;
}
