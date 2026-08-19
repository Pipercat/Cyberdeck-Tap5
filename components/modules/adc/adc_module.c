#include "adc_module.h"
#include "hal_gpio.h"
#include "hal_adc.h"
#include "esp_log.h"

static const char *TAG = "adc_module";
#define OWNER_TAG "adc_module"

static int s_selected_gpio = -1;

esp_err_t adc_module_select(int gpio_num)
{
    if (s_selected_gpio == gpio_num) {
        return ESP_OK;
    }
    adc_module_release();

    esp_err_t err = hal_gpio_request(gpio_num, HAL_GPIO_MODE_ADC, OWNER_TAG);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d nicht freigegeben: %s", gpio_num, esp_err_to_name(err));
        return err;
    }
    s_selected_gpio = gpio_num;
    return ESP_OK;
}

void adc_module_release(void)
{
    if (s_selected_gpio >= 0) {
        hal_gpio_release(s_selected_gpio);
        s_selected_gpio = -1;
    }
}

esp_err_t adc_module_read(int *out_raw, int *out_mv)
{
    if (s_selected_gpio < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return hal_adc_read(s_selected_gpio, out_raw, out_mv);
}
