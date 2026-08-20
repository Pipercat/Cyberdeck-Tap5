#include "network_module_ui.h"
#include "wifi_module.h"
#include "settings.h"
#include "nav.h"
#include "theme.h"
#include "back_button.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

static lv_obj_t *s_ssid_input = NULL;
static lv_obj_t *s_pass_input = NULL;
static lv_obj_t *s_keyboard = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_scan_list = NULL;
static lv_obj_t *s_scan_btn = NULL;
static lv_obj_t *s_back_btn = NULL;
static lv_timer_t *s_poll_timer = NULL;
static bool s_wifi_ready = false;

static void refresh_status_label(void)
{
    wifi_module_state_t state = wifi_module_get_state();
    char buf[96];
    switch (state) {
    case WIFI_MODULE_STATE_CONNECTED: {
        char ip[16] = "?";
        wifi_module_get_ip_str(ip, sizeof(ip));
        snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " Verbunden: %s (%d dBm, %s)",
                 wifi_module_get_connected_ssid(), wifi_module_get_rssi(), ip);
        lv_obj_set_style_text_color(s_status_label, THEME_COLOR_SUCCESS, 0);
        break;
    }
    case WIFI_MODULE_STATE_CONNECTING:
        snprintf(buf, sizeof(buf), "Verbinde mit %s...", wifi_module_get_connected_ssid());
        lv_obj_set_style_text_color(s_status_label, THEME_COLOR_WARNING, 0);
        break;
    case WIFI_MODULE_STATE_FAILED:
        snprintf(buf, sizeof(buf), "Verbindung zu %s fehlgeschlagen", wifi_module_get_connected_ssid());
        lv_obj_set_style_text_color(s_status_label, THEME_COLOR_DANGER, 0);
        break;
    default:
        snprintf(buf, sizeof(buf), "Getrennt");
        lv_obj_set_style_text_color(s_status_label, THEME_COLOR_TEXT_DIM, 0);
        break;
    }
    lv_label_set_text(s_status_label, buf);
}

static void scan_result_click_cb(lv_event_t *e)
{
    const char *ssid = (const char *)lv_event_get_user_data(e);
    lv_textarea_set_text(s_ssid_input, ssid);
    lv_obj_add_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);  // Tastatur deckt den Button sonst ab
    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_keyboard, s_pass_input);
    lv_obj_add_state(s_pass_input, LV_STATE_FOCUSED);
}

static void refresh_scan_list(void)
{
    lv_obj_clean(s_scan_list);
    wifi_module_scan_result_t results[WIFI_MODULE_MAX_SCAN_RESULTS];
    size_t n = wifi_module_get_scan_results(results, WIFI_MODULE_MAX_SCAN_RESULTS);
    if (n == 0) {
        lv_obj_t *empty = lv_label_create(s_scan_list);
        lv_label_set_text(empty, "Keine Netze gefunden");
        lv_obj_set_style_text_color(empty, THEME_COLOR_TEXT_DIM, 0);
        return;
    }
    for (size_t i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_create(s_scan_list);
        theme_apply_card(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        static char ssid_storage[WIFI_MODULE_MAX_SCAN_RESULTS][WIFI_MODULE_SSID_MAX_LEN + 1];
        strncpy(ssid_storage[i], results[i].ssid, WIFI_MODULE_SSID_MAX_LEN);
        ssid_storage[i][WIFI_MODULE_SSID_MAX_LEN] = '\0';
        lv_obj_add_event_cb(row, scan_result_click_cb, LV_EVENT_CLICKED, ssid_storage[i]);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text_fmt(name, "%s %s", results[i].secured ? LV_SYMBOL_CLOSE : LV_SYMBOL_OK,
                               results[i].ssid[0] ? results[i].ssid : "(verstecktes Netz)");
        lv_obj_t *rssi = lv_label_create(row);
        lv_label_set_text_fmt(rssi, "%d dBm", results[i].rssi_dbm);
        lv_obj_set_style_text_color(rssi, THEME_COLOR_TEXT_DIM, 0);
    }
}

static bool s_was_scanning = false;

static void poll_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_status_label();
    bool scanning = wifi_module_is_scanning();
    if (s_was_scanning && !scanning) {
        refresh_scan_list();
        if (s_scan_btn != NULL) {
            lv_label_set_text(lv_obj_get_child(s_scan_btn, 0), LV_SYMBOL_REFRESH " Scan");
            lv_obj_clear_state(s_scan_btn, LV_STATE_DISABLED);
        }
    }
    s_was_scanning = scanning;
}

static void scan_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!s_wifi_ready || wifi_module_is_scanning()) {
        return;
    }
    lv_obj_clean(s_scan_list);
    lv_obj_t *scanning = lv_label_create(s_scan_list);
    lv_label_set_text(scanning, "Scanne...");
    lv_obj_set_style_text_color(scanning, THEME_COLOR_TEXT_DIM, 0);
    lv_label_set_text(lv_obj_get_child(s_scan_btn, 0), "...");
    lv_obj_add_state(s_scan_btn, LV_STATE_DISABLED);
    wifi_module_start_scan();
}

static void connect_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!s_wifi_ready) {
        return;
    }
    const char *ssid = lv_textarea_get_text(s_ssid_input);
    const char *pass = lv_textarea_get_text(s_pass_input);
    wifi_module_connect(ssid, pass);
    refresh_status_label();
}

static void disconnect_btn_cb(lv_event_t *e)
{
    (void)e;
    wifi_module_disconnect();
    refresh_status_label();
}

static void input_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_obj_add_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);  // Tastatur deckt den Button sonst ab
    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_keyboard, ta);
}

static void input_defocus_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (s_poll_timer) {
        lv_timer_pause(s_poll_timer);
    }
    nav_show(NAV_SCREEN_DASHBOARD);
}

static lv_obj_t *network_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_style_pad_top(scr, THEME_SCREEN_PAD_TOP, 0);   // ueberschreibt pad_all nur oben
    lv_obj_set_style_pad_bottom(scr, 74, 0);  // Platz fuer den schwebenden Zurueck-Button
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);  // sonst verschiebt Scrollen den schwebenden Zurueck-Button
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 10, 0);

    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 40);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Network / Wi-Fi");
    theme_apply_title(title);

    s_back_btn = back_button_create(scr, back_cb);

    s_status_label = lv_label_create(scr);
    lv_label_set_text(s_status_label, "Initialisiere Wi-Fi (ESP32-C6)...");
    lv_obj_set_style_text_color(s_status_label, THEME_COLOR_TEXT_DIM, 0);

    // --- SSID / Passwort ---
    lv_obj_t *form_row = lv_obj_create(scr);
    lv_obj_remove_style_all(form_row);
    lv_obj_set_size(form_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(form_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(form_row, 8, 0);
    lv_obj_set_style_pad_row(form_row, 8, 0);

    s_ssid_input = lv_textarea_create(form_row);
    lv_obj_set_width(s_ssid_input, 260);
    lv_textarea_set_one_line(s_ssid_input, true);
    lv_textarea_set_placeholder_text(s_ssid_input, "SSID");
    lv_obj_add_event_cb(s_ssid_input, input_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ssid_input, input_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
    const settings_t *cfg = settings_get();
    if (cfg->wifi_ssid[0] != '\0') {
        lv_textarea_set_text(s_ssid_input, cfg->wifi_ssid);
    }

    s_pass_input = lv_textarea_create(form_row);
    lv_obj_set_width(s_pass_input, 260);
    lv_textarea_set_one_line(s_pass_input, true);
    lv_textarea_set_password_mode(s_pass_input, true);
    lv_textarea_set_placeholder_text(s_pass_input, "Passwort");
    lv_obj_add_event_cb(s_pass_input, input_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_pass_input, input_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    lv_obj_t *connect_btn = lv_btn_create(form_row);
    theme_apply_button(connect_btn, THEME_BTN_PRIMARY);
    lv_obj_set_height(connect_btn, THEME_TOUCH_TARGET_MIN);
    lv_obj_add_event_cb(connect_btn, connect_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *connect_l = lv_label_create(connect_btn);
    lv_label_set_text(connect_l, LV_SYMBOL_WIFI " Verbinden");

    lv_obj_t *disconnect_btn = lv_btn_create(form_row);
    theme_apply_button(disconnect_btn, THEME_BTN_NEUTRAL);
    lv_obj_set_height(disconnect_btn, THEME_TOUCH_TARGET_MIN);
    lv_obj_add_event_cb(disconnect_btn, disconnect_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *disconnect_l = lv_label_create(disconnect_btn);
    lv_label_set_text(disconnect_l, "Trennen");

    // --- Scan ---
    lv_obj_t *scan_row = lv_obj_create(scr);
    lv_obj_remove_style_all(scan_row);
    lv_obj_set_size(scan_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(scan_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scan_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *scan_title = lv_label_create(scan_row);
    lv_label_set_text(scan_title, "Verfuegbare Netze");
    s_scan_btn = lv_btn_create(scan_row);
    theme_apply_button(s_scan_btn, THEME_BTN_PRIMARY);
    lv_obj_add_event_cb(s_scan_btn, scan_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_l = lv_label_create(s_scan_btn);
    lv_label_set_text(scan_l, LV_SYMBOL_REFRESH " Scan");

    s_scan_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_scan_list);
    lv_obj_set_width(s_scan_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_scan_list, 1);
    lv_obj_set_flex_flow(s_scan_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_scan_list, 6, 0);

    s_keyboard = lv_keyboard_create(scr);
    // Schwebend statt im Flex-Flow, sonst verzerrt das Einblenden das Layout
    // (scan_list schrumpft) und die Tastatur kann den Zurueck-Button abdecken.
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);

    // Zurueck-Button zuletzt in den Vordergrund holen, damit spaeter
    // erzeugter/veraenderter Inhalt (Scan-Liste, Tastatur) ihn nie verdeckt.
    lv_obj_move_foreground(s_back_btn);

    return scr;
}

static void network_on_show(void)
{
    if (!s_wifi_ready) {
        s_wifi_ready = (wifi_module_init() == ESP_OK);
    }
    if (s_poll_timer == NULL) {
        s_poll_timer = lv_timer_create(poll_timer_cb, 500, NULL);
    } else {
        lv_timer_resume(s_poll_timer);
    }
    refresh_status_label();
    refresh_scan_list();
}

void network_module_ui_register(void)
{
    nav_register(NAV_SCREEN_NETWORK, network_screen_create);
    nav_register_on_show(NAV_SCREEN_NETWORK, network_on_show);
}
