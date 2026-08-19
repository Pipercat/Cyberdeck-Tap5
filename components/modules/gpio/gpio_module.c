#include "gpio_module.h"
#include "hal_gpio.h"
#include "hal_pwm.h"
#include "hal_adc.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "gpio_module";
#define OWNER_TAG "gpio_module"
#define MAX_GPIO_NUM 64

static gpio_module_mode_t s_mode[MAX_GPIO_NUM];

static bool valid_num(int gpio_num)
{
    return gpio_num >= 0 && gpio_num < MAX_GPIO_NUM;
}

esp_err_t gpio_module_release(int gpio_num)
{
    if (!valid_num(gpio_num)) return ESP_ERR_INVALID_ARG;

    switch (s_mode[gpio_num]) {
        case GPIO_MODULE_MODE_PWM:
            hal_pwm_stop(gpio_num);
            break;
        case GPIO_MODULE_MODE_OUTPUT:
        case GPIO_MODULE_MODE_INPUT:
        case GPIO_MODULE_MODE_INPUT_PULLUP:
        case GPIO_MODULE_MODE_INPUT_PULLDOWN:
            gpio_reset_pin((gpio_num_t)gpio_num);
            break;
        default:
            break;
    }
    if (s_mode[gpio_num] != GPIO_MODULE_MODE_UNCONFIGURED) {
        hal_gpio_release(gpio_num);
    }
    s_mode[gpio_num] = GPIO_MODULE_MODE_UNCONFIGURED;
    return ESP_OK;
}

esp_err_t gpio_module_configure(int gpio_num, gpio_module_mode_t mode)
{
    if (!valid_num(gpio_num)) return ESP_ERR_INVALID_ARG;

    gpio_module_release(gpio_num);  // vorherigen Modus sauber aufraeumen

    hal_gpio_mode_t hal_mode;
    switch (mode) {
        case GPIO_MODULE_MODE_INPUT:
        case GPIO_MODULE_MODE_INPUT_PULLUP:
        case GPIO_MODULE_MODE_INPUT_PULLDOWN:
            hal_mode = HAL_GPIO_MODE_INPUT; break;
        case GPIO_MODULE_MODE_OUTPUT:
            hal_mode = HAL_GPIO_MODE_OUTPUT; break;
        case GPIO_MODULE_MODE_PWM:
            hal_mode = HAL_GPIO_MODE_PWM; break;
        case GPIO_MODULE_MODE_ADC:
            hal_mode = HAL_GPIO_MODE_ADC; break;
        default:
            return ESP_OK;  // UNCONFIGURED: nur Release, keine Neukonfiguration noetig
    }

    esp_err_t err = hal_gpio_request(gpio_num, hal_mode, OWNER_TAG);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d: Konfiguration abgelehnt (%s)", gpio_num, esp_err_to_name(err));
        return err;
    }

    switch (mode) {
        case GPIO_MODULE_MODE_INPUT: {
            const gpio_config_t cfg = {
                .pin_bit_mask = 1ULL << gpio_num,
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
            };
            err = gpio_config(&cfg);
            break;
        }
        case GPIO_MODULE_MODE_INPUT_PULLUP: {
            const gpio_config_t cfg = {
                .pin_bit_mask = 1ULL << gpio_num,
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
            };
            err = gpio_config(&cfg);
            break;
        }
        case GPIO_MODULE_MODE_INPUT_PULLDOWN: {
            const gpio_config_t cfg = {
                .pin_bit_mask = 1ULL << gpio_num,
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_ENABLE,
            };
            err = gpio_config(&cfg);
            break;
        }
        case GPIO_MODULE_MODE_OUTPUT: {
            const gpio_config_t cfg = {
                .pin_bit_mask = 1ULL << gpio_num,
                .mode = GPIO_MODE_OUTPUT,
            };
            err = gpio_config(&cfg);
            if (err == ESP_OK) {
                gpio_set_level((gpio_num_t)gpio_num, 0);
            }
            break;
        }
        case GPIO_MODULE_MODE_PWM:
            err = hal_pwm_start(gpio_num, 1000, 50);  // Default 1kHz/50%, per UI anpassbar
            break;
        case GPIO_MODULE_MODE_ADC:
            break;  // hal_adc konfiguriert den Kanal lazy bei jedem Read
        default:
            break;
    }

    if (err != ESP_OK) {
        hal_gpio_release(gpio_num);
        ESP_LOGE(TAG, "GPIO %d: Peripherie-Init fehlgeschlagen (%s)", gpio_num, esp_err_to_name(err));
        return err;
    }

    s_mode[gpio_num] = mode;
    return ESP_OK;
}

gpio_module_mode_t gpio_module_get_mode(int gpio_num)
{
    if (!valid_num(gpio_num)) return GPIO_MODULE_MODE_UNCONFIGURED;
    return s_mode[gpio_num];
}

esp_err_t gpio_module_set_output(int gpio_num, bool level)
{
    if (!valid_num(gpio_num) || s_mode[gpio_num] != GPIO_MODULE_MODE_OUTPUT) {
        return ESP_ERR_INVALID_STATE;
    }
    return gpio_set_level((gpio_num_t)gpio_num, level ? 1 : 0);
}

esp_err_t gpio_module_read_input(int gpio_num, bool *out_level)
{
    if (!valid_num(gpio_num) || out_level == NULL) return ESP_ERR_INVALID_ARG;
    gpio_module_mode_t m = s_mode[gpio_num];
    if (m != GPIO_MODULE_MODE_INPUT && m != GPIO_MODULE_MODE_INPUT_PULLUP &&
        m != GPIO_MODULE_MODE_INPUT_PULLDOWN) {
        return ESP_ERR_INVALID_STATE;
    }
    *out_level = gpio_get_level((gpio_num_t)gpio_num) != 0;
    return ESP_OK;
}

esp_err_t gpio_module_set_pwm(int gpio_num, uint32_t freq_hz, uint8_t duty_percent)
{
    if (!valid_num(gpio_num) || s_mode[gpio_num] != GPIO_MODULE_MODE_PWM) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)freq_hz;  // laeuft bereits mit der beim Konfigurieren gesetzten Frequenz
    return hal_pwm_set_duty_percent(gpio_num, duty_percent);
}

esp_err_t gpio_module_read_adc(int gpio_num, int *out_raw, int *out_mv)
{
    if (!valid_num(gpio_num) || s_mode[gpio_num] != GPIO_MODULE_MODE_ADC) {
        return ESP_ERR_INVALID_STATE;
    }
    return hal_adc_read(gpio_num, out_raw, out_mv);
}
