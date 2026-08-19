#include "gpio_module_ui.h"
#include "gpio_module.h"
#include "nav.h"
#include "theme.h"
#include "pin_table.h"
#include "back_button.h"
#include "lvgl.h"

static lv_obj_t *s_grid = NULL;
static lv_obj_t *s_modal = NULL;
static lv_obj_t *s_modal_body = NULL;
static lv_obj_t *s_modal_title = NULL;
static lv_timer_t *s_refresh_timer = NULL;
static int s_current_gpio = -1;

static void gpio_ui_rebuild_modal_body(void);

static const char *mode_label(gpio_module_mode_t m)
{
    switch (m) {
        case GPIO_MODULE_MODE_INPUT: return "IN";
        case GPIO_MODULE_MODE_INPUT_PULLUP: return "IN-PU";
        case GPIO_MODULE_MODE_INPUT_PULLDOWN: return "IN-PD";
        case GPIO_MODULE_MODE_OUTPUT: return "OUT";
        case GPIO_MODULE_MODE_PWM: return "PWM";
        case GPIO_MODULE_MODE_ADC: return "ADC";
        default: return "frei";
    }
}

static void refresh_grid_labels(void)
{
    if (s_grid == NULL) return;
    uint32_t n = lv_obj_get_child_count(s_grid);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *card = lv_obj_get_child(s_grid, i);
        int gpio_num = (int)(intptr_t)lv_obj_get_user_data(card);
        lv_obj_t *status = lv_obj_get_child(card, 1);
        lv_label_set_text(status, mode_label(gpio_module_get_mode(gpio_num)));
    }
}

static void close_modal(void)
{
    if (s_refresh_timer) {
        lv_timer_delete(s_refresh_timer);
        s_refresh_timer = NULL;
    }
    if (s_modal) {
        lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
    }
    s_current_gpio = -1;
    refresh_grid_labels();
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_current_gpio < 0 || s_modal_body == NULL) return;
    gpio_module_mode_t mode = gpio_module_get_mode(s_current_gpio);
    lv_obj_t *value_label = (lv_obj_t *)lv_obj_get_user_data(s_modal_body);
    if (value_label == NULL) return;

    if (mode == GPIO_MODULE_MODE_INPUT || mode == GPIO_MODULE_MODE_INPUT_PULLUP ||
        mode == GPIO_MODULE_MODE_INPUT_PULLDOWN) {
        bool level = false;
        gpio_module_read_input(s_current_gpio, &level);
        lv_label_set_text(value_label, level ? "HIGH" : "LOW");
    } else if (mode == GPIO_MODULE_MODE_ADC) {
        int raw = 0, mv = 0;
        if (gpio_module_read_adc(s_current_gpio, &raw, &mv) == ESP_OK) {
            lv_label_set_text_fmt(value_label, "Raw: %d\nca. %d.%02d V (unkalibriert)",
                                   raw, mv / 1000, (mv % 1000) / 10);
        }
    }
}

static void mode_button_cb(lv_event_t *e)
{
    gpio_module_mode_t mode = (gpio_module_mode_t)(intptr_t)lv_event_get_user_data(e);
    gpio_module_configure(s_current_gpio, mode);
    gpio_ui_rebuild_modal_body();
}

static void output_toggle_cb(lv_event_t *e)
{
    bool level = (bool)(intptr_t)lv_event_get_user_data(e);
    gpio_module_set_output(s_current_gpio, level);
    gpio_ui_rebuild_modal_body();
}

static void duty_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);
    gpio_module_set_pwm(s_current_gpio, 0, (uint8_t)val);
}

static void close_button_cb(lv_event_t *e)
{
    (void)e;
    close_modal();
}

static void back_to_dashboard_cb(lv_event_t *e)
{
    (void)e;
    nav_show(NAV_SCREEN_DASHBOARD);
}

// Baut den Inhaltsbereich des Modals fuer s_current_gpio neu auf
// (Modusabhaengige Steuerelemente).
static void gpio_ui_rebuild_modal_body(void)
{
    if (s_modal_body == NULL || s_current_gpio < 0) return;
    lv_obj_clean(s_modal_body);
    lv_obj_set_user_data(s_modal_body, NULL);

    const pin_descriptor_t *pin = pin_table_find(s_current_gpio);
    gpio_module_mode_t mode = gpio_module_get_mode(s_current_gpio);

    lv_label_set_text_fmt(s_modal_title, "GPIO %d - %s", s_current_gpio, pin ? pin->label : "?");

    // --- Modus-Buttons ---
    lv_obj_t *mode_row = lv_obj_create(s_modal_body);
    lv_obj_remove_style_all(mode_row);
    lv_obj_set_size(mode_row, LV_PCT(100), 56);
    lv_obj_set_flex_flow(mode_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(mode_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(mode_row, 6, 0);

    struct { const char *label; gpio_module_mode_t mode; bool enabled; } options[] = {
        { "IN",     GPIO_MODULE_MODE_INPUT,          pin && pin->supports_input },
        { "IN-PU",  GPIO_MODULE_MODE_INPUT_PULLUP,   pin && pin->supports_input },
        { "IN-PD",  GPIO_MODULE_MODE_INPUT_PULLDOWN, pin && pin->supports_input },
        { "OUT",    GPIO_MODULE_MODE_OUTPUT,         pin && pin->supports_output },
        { "PWM",    GPIO_MODULE_MODE_PWM,            pin && pin->supports_pwm },
        { "ADC",    GPIO_MODULE_MODE_ADC,            pin && pin->supports_adc },
    };
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
        if (!options[i].enabled) continue;
        lv_obj_t *btn = lv_btn_create(mode_row);
        lv_obj_set_size(btn, 70, THEME_TOUCH_TARGET_MIN);
        bool active = (mode == options[i].mode);
        lv_obj_set_style_bg_color(btn, active ? THEME_COLOR_ACCENT : THEME_COLOR_SURFACE_HI, 0);
        lv_obj_add_event_cb(btn, mode_button_cb, LV_EVENT_CLICKED, (void *)(intptr_t)options[i].mode);
        lv_obj_t *l = lv_label_create(btn);
        lv_label_set_text(l, options[i].label);
        lv_obj_center(l);
    }

    // --- Modusabhaengiger Inhalt ---
    lv_obj_t *content = lv_obj_create(s_modal_body);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(content, 16, 0);
    lv_obj_set_style_pad_row(content, 12, 0);

    if (mode == GPIO_MODULE_MODE_OUTPUT) {
        lv_obj_t *row = lv_obj_create(content);
        lv_obj_remove_style_all(row);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 12, 0);
        lv_obj_t *low_btn = lv_btn_create(row);
        lv_obj_set_size(low_btn, 100, THEME_TOUCH_TARGET_MIN);
        lv_obj_add_event_cb(low_btn, output_toggle_cb, LV_EVENT_CLICKED, (void *)(intptr_t)false);
        lv_obj_t *low_l = lv_label_create(low_btn); lv_label_set_text(low_l, "LOW"); lv_obj_center(low_l);
        lv_obj_t *high_btn = lv_btn_create(row);
        lv_obj_set_size(high_btn, 100, THEME_TOUCH_TARGET_MIN);
        lv_obj_set_style_bg_color(high_btn, THEME_COLOR_SUCCESS, 0);
        lv_obj_add_event_cb(high_btn, output_toggle_cb, LV_EVENT_CLICKED, (void *)(intptr_t)true);
        lv_obj_t *high_l = lv_label_create(high_btn); lv_label_set_text(high_l, "HIGH"); lv_obj_center(high_l);
    } else if (mode == GPIO_MODULE_MODE_INPUT || mode == GPIO_MODULE_MODE_INPUT_PULLUP ||
               mode == GPIO_MODULE_MODE_INPUT_PULLDOWN) {
        lv_obj_t *value_label = lv_label_create(content);
        lv_label_set_text(value_label, "--");
        lv_obj_set_style_text_color(value_label, THEME_COLOR_ACCENT, 0);
        lv_obj_set_user_data(s_modal_body, value_label);
    } else if (mode == GPIO_MODULE_MODE_PWM) {
        lv_obj_t *label = lv_label_create(content);
        lv_label_set_text(label, "Duty-Cycle (Start: 1 kHz / 50%)");
        lv_obj_set_style_text_color(label, THEME_COLOR_TEXT_DIM, 0);
        lv_obj_t *slider = lv_slider_create(content);
        lv_obj_set_width(slider, 220);
        lv_slider_set_range(slider, 0, 100);
        lv_slider_set_value(slider, 50, LV_ANIM_OFF);
        lv_obj_add_event_cb(slider, duty_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    } else if (mode == GPIO_MODULE_MODE_ADC) {
        lv_obj_t *value_label = lv_label_create(content);
        lv_label_set_text(value_label, "--");
        lv_obj_set_style_text_color(value_label, THEME_COLOR_ACCENT, 0);
        lv_obj_set_user_data(s_modal_body, value_label);
    } else {
        lv_obj_t *label = lv_label_create(content);
        lv_label_set_text(label, "Modus waehlen");
        lv_obj_set_style_text_color(label, THEME_COLOR_TEXT_DIM, 0);
    }

    if (s_refresh_timer == NULL) {
        s_refresh_timer = lv_timer_create(refresh_timer_cb, 250, NULL);
    }
}

static void open_modal_for_pin(int gpio_num)
{
    s_current_gpio = gpio_num;
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
    gpio_ui_rebuild_modal_body();
}

static void card_click_cb(lv_event_t *e)
{
    lv_obj_t *card = lv_event_get_target(e);
    int gpio_num = (int)(intptr_t)lv_obj_get_user_data(card);
    open_modal_for_pin(gpio_num);
}

static void build_modal(lv_obj_t *parent)
{
    s_modal = lv_obj_create(parent);
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_70, 0);
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *box = lv_obj_create(s_modal);
    theme_apply_card(box);
    lv_obj_set_size(box, 480, 380);
    lv_obj_center(box);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    lv_obj_t *header = lv_obj_create(box);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_modal_title = lv_label_create(header);
    lv_label_set_text(s_modal_title, "GPIO");
    lv_obj_set_style_text_color(s_modal_title, THEME_COLOR_TEXT, 0);

    lv_obj_t *close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 44, 44);
    lv_obj_add_event_cb(close_btn, close_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_l = lv_label_create(close_btn);
    lv_label_set_text(close_l, LV_SYMBOL_CLOSE);
    lv_obj_center(close_l);

    s_modal_body = lv_obj_create(box);
    lv_obj_remove_style_all(s_modal_body);
    lv_obj_set_size(s_modal_body, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_modal_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_modal_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
}

static lv_obj_t *gpio_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_style_pad_top(scr, THEME_SCREEN_PAD_TOP, 0);   // ueberschreibt pad_all nur oben
    lv_obj_set_style_pad_bottom(scr, 74, 0);  // Platz fuer den schwebenden Zurueck-Button
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 40);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "GPIO Control");
    theme_apply_title(title);

    back_button_create(scr, back_to_dashboard_cb);

    s_grid = lv_obj_create(scr);
    lv_obj_remove_style_all(s_grid);
    lv_obj_set_width(s_grid, LV_PCT(100));
    lv_obj_set_flex_grow(s_grid, 1);  // fuellt Restplatz statt 100% Elternhoehe (ueberlappte sonst mit Header/Zurueck-Button)
    lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_grid, 10, 0);
    lv_obj_set_style_pad_column(s_grid, 10, 0);

    for (size_t i = 0; i < g_pin_table_count; i++) {
        if (g_pin_table[i].role != PIN_ROLE_FREE) continue;
        int gpio_num = g_pin_table[i].gpio_num;

        lv_obj_t *card = lv_obj_create(s_grid);
        theme_apply_card(card);
        lv_obj_set_size(card, 130, 90);
        lv_obj_set_user_data(card, (void *)(intptr_t)gpio_num);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *num_label = lv_label_create(card);
        lv_label_set_text_fmt(num_label, "GPIO %d", gpio_num);
        lv_obj_set_style_text_color(num_label, THEME_COLOR_TEXT, 0);

        lv_obj_t *status_label = lv_label_create(card);
        lv_label_set_text(status_label, "frei");
        lv_obj_set_style_text_color(status_label, THEME_COLOR_TEXT_DIM, 0);
    }

    build_modal(scr);
    return scr;
}

void gpio_module_ui_register(void)
{
    nav_register(NAV_SCREEN_GPIO, gpio_screen_create);
}
