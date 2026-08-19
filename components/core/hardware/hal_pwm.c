#include "hal_pwm.h"
#include <inttypes.h>
#include <stdbool.h>
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "hal_pwm";

#define HAL_PWM_MAX_CHANNELS 8  // SOC_LEDC_CHANNEL_NUM
#define HAL_PWM_MAX_TIMERS   4  // SOC_LEDC_TIMER_BIT_WIDTH-Timer-Anzahl
#define HAL_PWM_DUTY_RES     LEDC_TIMER_13_BIT
#define HAL_PWM_DUTY_MAX     ((1 << 13) - 1)

typedef struct {
    int gpio_num;       // -1 = frei
    ledc_channel_t channel;
    ledc_timer_t timer;
} pwm_slot_t;

typedef struct {
    bool in_use;
    uint32_t freq_hz;
    int ref_count;
} timer_slot_t;

static pwm_slot_t s_channels[HAL_PWM_MAX_CHANNELS];
static timer_slot_t s_timers[HAL_PWM_MAX_TIMERS];
static bool s_initialized = false;

static void ensure_init(void)
{
    if (s_initialized) return;
    for (int i = 0; i < HAL_PWM_MAX_CHANNELS; i++) {
        s_channels[i].gpio_num = -1;
    }
    s_initialized = true;
}

static pwm_slot_t *find_channel(int gpio_num)
{
    for (int i = 0; i < HAL_PWM_MAX_CHANNELS; i++) {
        if (s_channels[i].gpio_num == gpio_num) return &s_channels[i];
    }
    return NULL;
}

static pwm_slot_t *find_free_channel(void)
{
    return find_channel(-1);
}

static int find_or_alloc_timer(uint32_t freq_hz)
{
    int free_slot = -1;
    for (int i = 0; i < HAL_PWM_MAX_TIMERS; i++) {
        if (s_timers[i].in_use && s_timers[i].freq_hz == freq_hz) {
            return i;
        }
        if (!s_timers[i].in_use && free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot < 0) {
        return -1;
    }
    const ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = HAL_PWM_DUTY_RES,
        .timer_num = (ledc_timer_t)free_slot,
        .freq_hz = freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&timer_cfg) != ESP_OK) {
        return -1;
    }
    s_timers[free_slot].in_use = true;
    s_timers[free_slot].freq_hz = freq_hz;
    s_timers[free_slot].ref_count = 0;
    return free_slot;
}

esp_err_t hal_pwm_start(int gpio_num, uint32_t freq_hz, uint8_t duty_percent)
{
    ensure_init();
    if (find_channel(gpio_num) != NULL) {
        ESP_LOGE(TAG, "GPIO %d hat bereits einen aktiven PWM-Kanal", gpio_num);
        return ESP_ERR_INVALID_STATE;
    }
    pwm_slot_t *slot = find_free_channel();
    if (slot == NULL) {
        ESP_LOGE(TAG, "Keine freien LEDC-Kanaele mehr (max %d)", HAL_PWM_MAX_CHANNELS);
        return ESP_ERR_NO_MEM;
    }
    int timer = find_or_alloc_timer(freq_hz);
    if (timer < 0) {
        ESP_LOGE(TAG, "Keine freien LEDC-Timer fuer %" PRIu32 " Hz mehr (max %d unterschiedliche Frequenzen)",
                 freq_hz, HAL_PWM_MAX_TIMERS);
        return ESP_ERR_NO_MEM;
    }

    ledc_channel_t channel = (ledc_channel_t)(slot - s_channels);
    uint32_t duty = (HAL_PWM_DUTY_MAX * duty_percent) / 100;
    const ledc_channel_config_t ch_cfg = {
        .gpio_num = gpio_num,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = channel,
        .timer_sel = (ledc_timer_t)timer,
        .duty = duty,
        .hpoint = 0,
    };
    esp_err_t err = ledc_channel_config(&ch_cfg);
    if (err != ESP_OK) {
        return err;
    }

    slot->gpio_num = gpio_num;
    slot->channel = channel;
    slot->timer = (ledc_timer_t)timer;
    s_timers[timer].ref_count++;
    return ESP_OK;
}

esp_err_t hal_pwm_set_duty_percent(int gpio_num, uint8_t duty_percent)
{
    ensure_init();
    pwm_slot_t *slot = find_channel(gpio_num);
    if (slot == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    uint32_t duty = (HAL_PWM_DUTY_MAX * duty_percent) / 100;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, slot->channel, duty);
    if (err != ESP_OK) return err;
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, slot->channel);
}

esp_err_t hal_pwm_set_pulse_us(int gpio_num, uint32_t pulse_us, uint32_t period_us)
{
    ensure_init();
    pwm_slot_t *slot = find_channel(gpio_num);
    if (slot == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (pulse_us > period_us) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t duty = (uint32_t)(((uint64_t)HAL_PWM_DUTY_MAX * pulse_us) / period_us);
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, slot->channel, duty);
    if (err != ESP_OK) return err;
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, slot->channel);
}

esp_err_t hal_pwm_stop(int gpio_num)
{
    ensure_init();
    pwm_slot_t *slot = find_channel(gpio_num);
    if (slot == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    ledc_stop(LEDC_LOW_SPEED_MODE, slot->channel, 0);
    int timer = slot->timer;
    s_timers[timer].ref_count--;
    if (s_timers[timer].ref_count <= 0) {
        s_timers[timer].in_use = false;
    }
    slot->gpio_num = -1;
    return ESP_OK;
}
