#include "settings_module_ui.h"
#include "settings.h"
#include "board_init.h"
#include "nav.h"
#include "theme.h"
#include "bottom_nav.h"
#include "remote_server.h"
#include "remote_auth.h"
#include "wifi_module.h"
#include "lvgl.h"
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

static lv_obj_t *s_backlight_slider = NULL;
static lv_obj_t *s_backlight_value_label = NULL;

static lv_obj_t *s_remote_toggle = NULL;
static lv_obj_t *s_pairing_toggle = NULL;
static lv_obj_t *s_ip_label = NULL;
static lv_obj_t *s_clients_label = NULL;
static lv_obj_t *s_pair_code_label = NULL;
static lv_timer_t *s_remote_refresh_timer = NULL;

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

static void remote_toggle_cb(lv_event_t *e)
{
    (void)e;
    settings_t cfg = *settings_get();
    cfg.remote_access_enabled = !cfg.remote_access_enabled;
    settings_save(&cfg);
    theme_set_toggle_active(s_remote_toggle, cfg.remote_access_enabled);
    if (cfg.remote_access_enabled) {
        remote_server_start();
    } else {
        remote_server_stop();
    }
}

static void pairing_toggle_cb(lv_event_t *e)
{
    (void)e;
    settings_t cfg = *settings_get();
    cfg.remote_require_pairing = !cfg.remote_require_pairing;
    settings_save(&cfg);
    theme_set_toggle_active(s_pairing_toggle, cfg.remote_require_pairing);
}

static void show_pair_code_cb(lv_event_t *e)
{
    (void)e;
    char code[REMOTE_AUTH_CODE_LEN + 1];
    int valid_s = 0;
    remote_auth_generate_code(code, &valid_s);
    lv_label_set_text_fmt(s_pair_code_label, "%s  ·  %02d:%02d", code, valid_s / 60, valid_s % 60);
}

static void revoke_all_cb(lv_event_t *e)
{
    (void)e;
    remote_auth_revoke_all();
}

static void remote_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    char ip[16] = "--";
    if (!wifi_module_get_ip_str(ip, sizeof(ip))) {
        strcpy(ip, "--");
    }
    lv_label_set_text_fmt(s_ip_label, "%s%s", ip, remote_server_is_running() ? "  ·  Running" : "  ·  Stopped");

    remote_auth_client_t clients[REMOTE_AUTH_MAX_CLIENTS];
    size_t n = remote_auth_get_clients(clients, REMOTE_AUTH_MAX_CLIENTS);
    lv_label_set_text_fmt(s_clients_label, "%u", (unsigned)n);

    char code[REMOTE_AUTH_CODE_LEN + 1];
    int remaining_s = 0;
    if (remote_auth_get_active_code(code, &remaining_s)) {
        lv_label_set_text_fmt(s_pair_code_label, "%s  ·  %02d:%02d", code, remaining_s / 60, remaining_s % 60);
    }
}

static lv_obj_t *settings_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_style_pad_top(scr, THEME_SCREEN_PAD_TOP, 0);   // ueberschreibt pad_all nur oben
    lv_obj_set_style_pad_bottom(scr, BOTTOM_NAV_HEIGHT + 16, 0);  // Platz fuer die Bottom-Nav (Top-Level-Screen, kein Zurueck-Button)
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 20, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Settings");
    theme_apply_title(title);

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

    // --- Remote Access (Nutzeranforderung 16) ---
    const settings_t *rcfg = settings_get();
    lv_obj_t *remote_card = lv_obj_create(scr);
    theme_apply_card(remote_card);
    lv_obj_set_size(remote_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(remote_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(remote_card, 10, 0);

    lv_obj_t *remote_title = lv_label_create(remote_card);
    lv_label_set_text(remote_title, "REMOTE ACCESS");
    lv_obj_set_style_text_color(remote_title, THEME_COLOR_TEXT_DIM, 0);

    lv_obj_t *remote_row = lv_obj_create(remote_card);
    lv_obj_remove_style_all(remote_row);
    lv_obj_set_size(remote_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(remote_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(remote_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *remote_label = lv_label_create(remote_row);
    lv_label_set_text_fmt(remote_label, "%s", rcfg->device_name);
    s_remote_toggle = lv_btn_create(remote_row);
    theme_apply_toggle(s_remote_toggle, rcfg->remote_access_enabled);
    lv_obj_add_event_cb(s_remote_toggle, remote_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *remote_toggle_l = lv_label_create(s_remote_toggle);
    lv_label_set_text(remote_toggle_l, "ON");

    s_ip_label = lv_label_create(remote_card);
    lv_obj_set_style_text_color(s_ip_label, THEME_COLOR_TEXT_DIM, 0);
    lv_label_set_text(s_ip_label, "--");

    lv_obj_t *pairing_row = lv_obj_create(remote_card);
    lv_obj_remove_style_all(pairing_row);
    lv_obj_set_size(pairing_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pairing_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pairing_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *pairing_label = lv_label_create(pairing_row);
    lv_label_set_text(pairing_label, "Require Pairing");
    s_pairing_toggle = lv_btn_create(pairing_row);
    theme_apply_toggle(s_pairing_toggle, rcfg->remote_require_pairing);
    lv_obj_add_event_cb(s_pairing_toggle, pairing_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pairing_toggle_l = lv_label_create(s_pairing_toggle);
    lv_label_set_text(pairing_toggle_l, "ON");

    lv_obj_t *clients_row = lv_obj_create(remote_card);
    lv_obj_remove_style_all(clients_row);
    lv_obj_set_size(clients_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(clients_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(clients_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *clients_label = lv_label_create(clients_row);
    lv_label_set_text(clients_label, "Paired Clients");
    s_clients_label = lv_label_create(clients_row);
    lv_obj_set_style_text_color(s_clients_label, THEME_COLOR_ACCENT, 0);
    lv_label_set_text(s_clients_label, "0");

    s_pair_code_label = lv_label_create(remote_card);
    theme_apply_title(s_pair_code_label);
    lv_label_set_text(s_pair_code_label, "------");

    lv_obj_t *pair_btn_row = lv_obj_create(remote_card);
    lv_obj_remove_style_all(pair_btn_row);
    lv_obj_set_size(pair_btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pair_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(pair_btn_row, 8, 0);
    lv_obj_t *pair_btn = lv_btn_create(pair_btn_row);
    theme_apply_button(pair_btn, THEME_BTN_PRIMARY);
    lv_obj_add_event_cb(pair_btn, show_pair_code_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pair_l = lv_label_create(pair_btn);
    lv_label_set_text(pair_l, "Show Pair Code");
    lv_obj_t *revoke_btn = lv_btn_create(pair_btn_row);
    theme_apply_button(revoke_btn, THEME_BTN_DANGER);
    lv_obj_add_event_cb(revoke_btn, revoke_all_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *revoke_l = lv_label_create(revoke_btn);
    lv_label_set_text(revoke_l, "Revoke All");

    return scr;
}

static void settings_on_show(void)
{
    remote_server_init();
    if (s_remote_refresh_timer == NULL) {
        s_remote_refresh_timer = lv_timer_create(remote_refresh_timer_cb, 1000, NULL);
    } else {
        lv_timer_resume(s_remote_refresh_timer);
    }
    remote_refresh_timer_cb(NULL);
}

void settings_module_ui_register(void)
{
    nav_register(NAV_SCREEN_SETTINGS, settings_screen_create);
    nav_register_on_show(NAV_SCREEN_SETTINGS, settings_on_show);
}
