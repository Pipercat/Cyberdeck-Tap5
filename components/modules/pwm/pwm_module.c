#include "pwm_module.h"
#include <stdbool.h>
#include "hal_gpio.h"
#include "hal_pwm.h"
#include "esp_log.h"

static const char *TAG = "pwm_module";
#define OWNER_TAG "pwm_module"
#define MAX_GPIO_NUM 64

static bool s_active[MAX_GPIO_NUM];

static bool valid_num(int gpio_num)
{
    return gpio_num >= 0 && gpio_num < MAX_GPIO_NUM;
}

static esp_err_t start_common(int gpio_num, uint32_t freq_hz, uint8_t duty_percent)
{
    if (!valid_num(gpio_num)) return ESP_ERR_INVALID_ARG;
    if (s_active[gpio_num]) {
        hal_pwm_stop(gpio_num);
        hal_gpio_release(gpio_num);
        s_active[gpio_num] = false;
    }
    esp_err_t err = hal_gpio_request(gpio_num, HAL_GPIO_MODE_PWM, OWNER_TAG);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d nicht freigegeben: %s", gpio_num, esp_err_to_name(err));
        return err;
    }
    err = hal_pwm_start(gpio_num, freq_hz, duty_percent);
    if (err != ESP_OK) {
        hal_gpio_release(gpio_num);
        return err;
    }
    s_active[gpio_num] = true;
    return ESP_OK;
}

esp_err_t pwm_module_start_generic(int gpio_num, uint32_t freq_hz, uint8_t duty_percent)
{
    return start_common(gpio_num, freq_hz, duty_percent);
}

esp_err_t pwm_module_start_servo(int gpio_num, uint32_t pulse_us)
{
    uint32_t freq_hz = 1000000 / PWM_MODULE_SERVO_PERIOD_US;  // 20000us Periode = 50 Hz
    esp_err_t err = start_common(gpio_num, freq_hz, 0);
    if (err != ESP_OK) return err;
    return hal_pwm_set_pulse_us(gpio_num, pulse_us, PWM_MODULE_SERVO_PERIOD_US);
}

esp_err_t pwm_module_set_servo_pulse(int gpio_num, uint32_t pulse_us)
{
    if (!valid_num(gpio_num) || !s_active[gpio_num]) return ESP_ERR_INVALID_STATE;
    return hal_pwm_set_pulse_us(gpio_num, pulse_us, PWM_MODULE_SERVO_PERIOD_US);
}

esp_err_t pwm_module_stop(int gpio_num)
{
    if (!valid_num(gpio_num)) return ESP_ERR_INVALID_ARG;
    if (!s_active[gpio_num]) return ESP_OK;
    hal_pwm_stop(gpio_num);
    hal_gpio_release(gpio_num);
    s_active[gpio_num] = false;
    return ESP_OK;
}

uint32_t pwm_module_angle_to_pulse_us(uint8_t angle_deg)
{
    if (angle_deg > 180) angle_deg = 180;
    uint32_t span = PWM_MODULE_SERVO_MAX_US - PWM_MODULE_SERVO_MIN_US;
    return PWM_MODULE_SERVO_MIN_US + (span * angle_deg) / 180;
}

uint8_t pwm_module_pulse_us_to_angle(uint32_t pulse_us)
{
    if (pulse_us < PWM_MODULE_SERVO_MIN_US) pulse_us = PWM_MODULE_SERVO_MIN_US;
    if (pulse_us > PWM_MODULE_SERVO_MAX_US) pulse_us = PWM_MODULE_SERVO_MAX_US;
    uint32_t span = PWM_MODULE_SERVO_MAX_US - PWM_MODULE_SERVO_MIN_US;
    return (uint8_t)(((pulse_us - PWM_MODULE_SERVO_MIN_US) * 180) / span);
}
