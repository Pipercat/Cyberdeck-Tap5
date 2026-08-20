#include "settings_module_ui.h"
#include "settings.h"
#include "board_init.h"
#include "nav.h"
#include "theme.h"
#include "screen_header.h"
#include "lvgl.h"
#include <stdio.h>
#include <inttypes.h>

static lv_obj_t *s_backlight_slider = NULL;
static lv_obj_t *s_backlight_value_label = NULL;

static void backlight_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t percent = lv_slider_get_value(slider);

    lv_label_set_text_fmt(s_backlight_value_label, "%" PRId32 "%%", percent);
    board_set_backlight((uint8_t)percent);

    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        settings_t cfg = *settings_get();
        cfg.backlight_percent = (uint8_t)percent;
        settings_save(&cfg);
    }
}

static void reset_defaults_cb(lv_event_t *e)
{
    (void)e;
    settings_reset_defaults();
    const settings_t *cfg = settings_get();
    board_set_backlight(cfg->backlight_percent);
    lv_slider_set_value(s_backlight_slider, cfg->backlight_percent, LV_ANIM_ON);
    lv_label_set_text_fmt(s_backlight_value_label, "%u%%", cfg->backlight_percent);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    nav_show(NAV_SCREEN_DASHBOARD);
}

static lv_obj_t *settings_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_style_pad_top(scr, THEME_SCREEN_PAD_TOP, 0);   // ueberschreibt pad_all nur oben
    lv_obj_set_style_pad_bottom(scr, 74, 0);  // Platz fuer den schwebenden Zurueck-Button
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);  // sonst verschiebt Scrollen den schwebenden Zurueck-Button
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 20, 0);

    screen_header_create(scr, "Settings", back_cb);

    // --- Backlight ---
    lv_obj_t *bl_card = lv_obj_create(scr);
    theme_apply_card(bl_card);
    lv_obj_set_size(bl_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bl_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(bl_card, 8, 0);

    lv_obj_t *bl_row = lv_obj_create(bl_card);
    lv_obj_remove_style_all(bl_row);
    lv_obj_set_size(bl_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bl_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *bl_label = lv_label_create(bl_row);
    lv_label_set_text(bl_label, "Display-Helligkeit");
    s_backlight_value_label = lv_label_create(bl_row);
    lv_obj_set_style_text_color(s_backlight_value_label, THEME_COLOR_ACCENT, 0);

    s_backlight_slider = lv_slider_create(bl_card);
    lv_obj_set_width(s_backlight_slider, LV_PCT(100));
    lv_obj_set_style_height(s_backlight_slider, THEME_TOUCH_TARGET_MIN / 2, 0);
    lv_slider_set_range(s_backlight_slider, 5, 100);  // 0% waere ein dunkler Screen ohne erkennbare Bedienbarkeit
    lv_obj_set_style_bg_color(s_backlight_slider, THEME_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_backlight_slider, THEME_COLOR_ACCENT, LV_PART_KNOB);
    lv_obj_add_event_cb(s_backlight_slider, backlight_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_backlight_slider, backlight_slider_cb, LV_EVENT_RELEASED, NULL);

    const settings_t *cfg = settings_get();
    lv_slider_set_value(s_backlight_slider, cfg->backlight_percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_backlight_value_label, "%u%%", cfg->backlight_percent);

    // --- Werksreset ---
    lv_obj_t *reset_card = lv_obj_create(scr);
    theme_apply_card(reset_card);
    lv_obj_set_size(reset_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(reset_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(reset_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *reset_label = lv_label_create(reset_card);
    lv_label_set_text(reset_label, "Werkseinstellungen");
    lv_obj_t *reset_btn = lv_btn_create(reset_card);
    theme_apply_button(reset_btn, THEME_BTN_WARNING);
    lv_obj_set_height(reset_btn, THEME_TOUCH_TARGET_MIN);
    lv_obj_add_event_cb(reset_btn, reset_defaults_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *reset_l = lv_label_create(reset_btn);
    lv_label_set_text(reset_l, LV_SYMBOL_REFRESH " Zuruecksetzen");

    return scr;
}

void settings_module_ui_register(void)
{
    nav_register(NAV_SCREEN_SETTINGS, settings_screen_create);
}
