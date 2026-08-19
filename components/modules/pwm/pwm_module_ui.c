#include "pwm_module_ui.h"
#include "pwm_module.h"
#include "nav.h"
#include "theme.h"
#include "pin_table.h"
#include "back_button.h"
#include "lvgl.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

typedef enum { PWM_UI_MODE_SERVO, PWM_UI_MODE_GENERIC } pwm_ui_mode_t;

static int s_selected_gpio = -1;
static bool s_running = false;
static pwm_ui_mode_t s_mode = PWM_UI_MODE_SERVO;
static bool s_sweep_active = false;
static int32_t s_sweep_dir = 1;
static uint32_t s_sweep_pulse_us = PWM_MODULE_SERVO_MIN_US;
static uint16_t s_generic_freq_hz = 1000;

static lv_obj_t *s_pin_dropdown = NULL;
static lv_obj_t *s_servo_panel = NULL;
static lv_obj_t *s_generic_panel = NULL;
static lv_obj_t *s_servo_slider = NULL;
static lv_obj_t *s_servo_label = NULL;
static lv_obj_t *s_duty_slider = NULL;
static lv_obj_t *s_freq_dropdown = NULL;
static lv_obj_t *s_start_stop_btn = NULL;
static lv_obj_t *s_start_stop_label = NULL;
static lv_obj_t *s_sweep_btn = NULL;
static lv_timer_t *s_sweep_timer = NULL;

static int s_pwm_pins[32];
static int s_pwm_pin_count = 0;

static void collect_pwm_pins(void)
{
    s_pwm_pin_count = 0;
    for (size_t i = 0; i < g_pin_table_count && s_pwm_pin_count < 32; i++) {
        if (g_pin_table[i].role == PIN_ROLE_FREE && g_pin_table[i].supports_pwm) {
            s_pwm_pins[s_pwm_pin_count++] = g_pin_table[i].gpio_num;
        }
    }
}

static void update_servo_label(uint32_t pulse_us)
{
    uint8_t angle = pwm_module_pulse_us_to_angle(pulse_us);
    lv_label_set_text_fmt(s_servo_label, "Pulse: %" PRIu32 " us  /  ca. %u Grad (Richtwert)", pulse_us, angle);
}

static void stop_sweep(void)
{
    if (s_sweep_timer) {
        lv_timer_delete(s_sweep_timer);
        s_sweep_timer = NULL;
    }
    s_sweep_active = false;
    if (s_sweep_btn) {
        lv_obj_set_style_bg_color(s_sweep_btn, THEME_COLOR_SURFACE_HI, 0);
    }
}

static void sweep_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_selected_gpio < 0 || !s_running) return;
    s_sweep_pulse_us += (uint32_t)(s_sweep_dir * 40);
    if (s_sweep_pulse_us >= PWM_MODULE_SERVO_MAX_US) {
        s_sweep_pulse_us = PWM_MODULE_SERVO_MAX_US;
        s_sweep_dir = -1;
    } else if (s_sweep_pulse_us <= PWM_MODULE_SERVO_MIN_US) {
        s_sweep_pulse_us = PWM_MODULE_SERVO_MIN_US;
        s_sweep_dir = 1;
    }
    pwm_module_set_servo_pulse(s_selected_gpio, s_sweep_pulse_us);
    lv_slider_set_value(s_servo_slider, (int32_t)s_sweep_pulse_us, LV_ANIM_OFF);
    update_servo_label(s_sweep_pulse_us);
}

static void stop_pwm(void)
{
    if (s_selected_gpio >= 0) {
        pwm_module_stop(s_selected_gpio);
    }
    stop_sweep();
    s_running = false;
    lv_label_set_text(s_start_stop_label, LV_SYMBOL_PLAY " Start");
    lv_obj_set_style_bg_color(s_start_stop_btn, THEME_COLOR_SUCCESS, 0);
}

static void start_stop_cb(lv_event_t *e)
{
    (void)e;
    if (s_selected_gpio < 0) return;

    if (s_running) {
        stop_pwm();
        return;
    }

    esp_err_t err;
    if (s_mode == PWM_UI_MODE_SERVO) {
        uint32_t pulse = (uint32_t)lv_slider_get_value(s_servo_slider);
        err = pwm_module_start_servo(s_selected_gpio, pulse);
    } else {
        uint8_t duty = (uint8_t)lv_slider_get_value(s_duty_slider);
        err = pwm_module_start_generic(s_selected_gpio, s_generic_freq_hz, duty);
    }
    if (err != ESP_OK) return;

    s_running = true;
    lv_label_set_text(s_start_stop_label, LV_SYMBOL_STOP " Stop");
    lv_obj_set_style_bg_color(s_start_stop_btn, THEME_COLOR_DANGER, 0);
}

static void pin_dropdown_cb(lv_event_t *e)
{
    (void)e;
    if (s_running) stop_pwm();
    uint32_t idx = lv_dropdown_get_selected(s_pin_dropdown);
    if (idx < (uint32_t)s_pwm_pin_count) {
        s_selected_gpio = s_pwm_pins[idx];
    }
}

static void mode_btn_cb(lv_event_t *e)
{
    pwm_ui_mode_t mode = (pwm_ui_mode_t)(intptr_t)lv_event_get_user_data(e);
    if (s_running) stop_pwm();
    s_mode = mode;
    if (mode == PWM_UI_MODE_SERVO) {
        lv_obj_clear_flag(s_servo_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_generic_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_servo_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_generic_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void servo_slider_cb(lv_event_t *e)
{
    (void)e;
    uint32_t pulse = (uint32_t)lv_slider_get_value(s_servo_slider);
    update_servo_label(pulse);
    if (s_running) {
        pwm_module_set_servo_pulse(s_selected_gpio, pulse);
    }
}

static void duty_slider_cb(lv_event_t *e)
{
    (void)e;
    if (s_running && s_mode == PWM_UI_MODE_GENERIC) {
        uint8_t duty = (uint8_t)lv_slider_get_value(s_duty_slider);
        pwm_module_start_generic(s_selected_gpio, s_generic_freq_hz, duty);
    }
}

static void freq_dropdown_cb(lv_event_t *e)
{
    (void)e;
    static const uint16_t freqs[] = { 100, 500, 1000, 5000, 20000 };
    uint32_t idx = lv_dropdown_get_selected(s_freq_dropdown);
    if (idx < sizeof(freqs) / sizeof(freqs[0])) {
        s_generic_freq_hz = freqs[idx];
        if (s_running) {
            uint8_t duty = (uint8_t)lv_slider_get_value(s_duty_slider);
            pwm_module_start_generic(s_selected_gpio, s_generic_freq_hz, duty);
        }
    }
}

static void sweep_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_selected_gpio < 0) return;
    if (s_sweep_active) {
        stop_sweep();
        return;
    }
    if (!s_running) {
        start_stop_cb(NULL);
    }
    s_sweep_active = true;
    s_sweep_pulse_us = (uint32_t)lv_slider_get_value(s_servo_slider);
    s_sweep_dir = 1;
    lv_obj_set_style_bg_color(s_sweep_btn, THEME_COLOR_ACCENT, 0);
    s_sweep_timer = lv_timer_create(sweep_timer_cb, 30, NULL);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    stop_pwm();
    nav_show(NAV_SCREEN_DASHBOARD);
}

static lv_obj_t *pwm_screen_create(void)
{
    collect_pwm_pins();

    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_set_style_pad_top(scr, THEME_SCREEN_PAD_TOP, 0);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_style_pad_bottom(scr, 74, 0);  // Platz fuer den schwebenden Zurueck-Button
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 14, 0);

    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 40);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "PWM / Servo");
    theme_apply_title(title);

    back_button_create(scr, back_cb);

    // --- Pin-Auswahl ---
    lv_obj_t *pin_row = lv_obj_create(scr);
    lv_obj_remove_style_all(pin_row);
    lv_obj_set_size(pin_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pin_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pin_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pin_row, 10, 0);
    lv_obj_t *pin_label = lv_label_create(pin_row);
    lv_label_set_text(pin_label, "Pin:");
    lv_obj_set_style_text_color(pin_label, THEME_COLOR_TEXT_DIM, 0);

    static char pin_opts[512];
    pin_opts[0] = '\0';
    for (int i = 0; i < s_pwm_pin_count; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "GPIO %d\n", s_pwm_pins[i]);
        strncat(pin_opts, buf, sizeof(pin_opts) - strlen(pin_opts) - 1);
    }
    s_pin_dropdown = lv_dropdown_create(pin_row);
    lv_dropdown_set_options(s_pin_dropdown, pin_opts);
    lv_obj_add_event_cb(s_pin_dropdown, pin_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (s_pwm_pin_count > 0) {
        s_selected_gpio = s_pwm_pins[0];
    }

    lv_obj_t *mode_servo = lv_btn_create(pin_row);
    lv_obj_add_event_cb(mode_servo, mode_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)PWM_UI_MODE_SERVO);
    lv_obj_t *ms_l = lv_label_create(mode_servo); lv_label_set_text(ms_l, "Servo");
    lv_obj_t *mode_generic = lv_btn_create(pin_row);
    lv_obj_add_event_cb(mode_generic, mode_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)PWM_UI_MODE_GENERIC);
    lv_obj_t *mg_l = lv_label_create(mode_generic); lv_label_set_text(mg_l, "LED/PWM");

    // --- Servo-Panel ---
    s_servo_panel = lv_obj_create(scr);
    theme_apply_card(s_servo_panel);
    lv_obj_set_size(s_servo_panel, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_servo_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_servo_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_servo_panel, 10, 0);

    s_servo_label = lv_label_create(s_servo_panel);
    lv_obj_set_style_text_color(s_servo_label, THEME_COLOR_ACCENT, 0);
    update_servo_label(PWM_MODULE_SERVO_MIN_US);

    s_servo_slider = lv_slider_create(s_servo_panel);
    lv_obj_set_width(s_servo_slider, 400);
    lv_slider_set_range(s_servo_slider, PWM_MODULE_SERVO_MIN_US, PWM_MODULE_SERVO_MAX_US);
    lv_slider_set_value(s_servo_slider, PWM_MODULE_SERVO_MIN_US, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_servo_slider, servo_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_sweep_btn = lv_btn_create(s_servo_panel);
    lv_obj_set_style_bg_color(s_sweep_btn, THEME_COLOR_SURFACE_HI, 0);
    lv_obj_add_event_cb(s_sweep_btn, sweep_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sweep_l = lv_label_create(s_sweep_btn);
    lv_label_set_text(sweep_l, LV_SYMBOL_LOOP " Sweep");

    lv_obj_t *servo_warn = lv_label_create(s_servo_panel);
    lv_label_set_text(servo_warn, LV_SYMBOL_WARNING " Nur Steuersignal - keine Motoren direkt am GPIO betreiben");
    lv_obj_set_style_text_color(servo_warn, THEME_COLOR_WARNING, 0);

    // --- Generic-PWM-Panel ---
    s_generic_panel = lv_obj_create(scr);
    theme_apply_card(s_generic_panel);
    lv_obj_set_size(s_generic_panel, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_generic_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_generic_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_generic_panel, 10, 0);
    lv_obj_add_flag(s_generic_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *freq_label = lv_label_create(s_generic_panel);
    lv_label_set_text(freq_label, "Frequenz:");
    lv_obj_set_style_text_color(freq_label, THEME_COLOR_TEXT_DIM, 0);
    s_freq_dropdown = lv_dropdown_create(s_generic_panel);
    lv_dropdown_set_options(s_freq_dropdown, "100 Hz\n500 Hz\n1000 Hz\n5000 Hz\n20000 Hz");
    lv_dropdown_set_selected(s_freq_dropdown, 2);
    lv_obj_add_event_cb(s_freq_dropdown, freq_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *duty_label = lv_label_create(s_generic_panel);
    lv_label_set_text(duty_label, "Duty-Cycle:");
    lv_obj_set_style_text_color(duty_label, THEME_COLOR_TEXT_DIM, 0);
    s_duty_slider = lv_slider_create(s_generic_panel);
    lv_obj_set_width(s_duty_slider, 400);
    lv_slider_set_range(s_duty_slider, 0, 100);
    lv_slider_set_value(s_duty_slider, 50, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_duty_slider, duty_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // --- Start/Stop ---
    s_start_stop_btn = lv_btn_create(scr);
    lv_obj_set_size(s_start_stop_btn, 160, THEME_TOUCH_TARGET_MIN);
    lv_obj_set_style_bg_color(s_start_stop_btn, THEME_COLOR_SUCCESS, 0);
    lv_obj_add_event_cb(s_start_stop_btn, start_stop_cb, LV_EVENT_CLICKED, NULL);
    s_start_stop_label = lv_label_create(s_start_stop_btn);
    lv_label_set_text(s_start_stop_label, LV_SYMBOL_PLAY " Start");
    lv_obj_center(s_start_stop_label);

    s_running = false;
    s_sweep_active = false;
    s_sweep_timer = NULL;

    return scr;
}

void pwm_module_ui_register(void)
{
    nav_register(NAV_SCREEN_PWM, pwm_screen_create);
}
