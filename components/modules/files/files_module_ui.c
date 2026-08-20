#include "files_module_ui.h"
#include "files_module.h"
#include "wifi_module.h"
#include "nav.h"
#include "theme.h"
#include "back_button.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_back_btn = NULL;
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_usage_label = NULL;
static lv_obj_t *s_hint_label = NULL;

static lv_obj_t *s_modal = NULL;
static lv_obj_t *s_modal_title = NULL;
static lv_obj_t *s_modal_body = NULL;
static char s_current_name[STORAGE_MODULE_MAX_NAME_LEN];

static void format_size(char *out, size_t out_len, size_t bytes)
{
    if (bytes < 1024) {
        snprintf(out, out_len, "%u B", (unsigned)bytes);
    } else {
        snprintf(out, out_len, "%.1f KB", bytes / 1024.0f);
    }
}

static void refresh_usage_label(void)
{
    size_t used = 0, total = 0;
    if (files_module_get_usage(&used, &total) == ESP_OK && total > 0) {
        char used_s[24], total_s[24];
        format_size(used_s, sizeof(used_s), used);
        format_size(total_s, sizeof(total_s), total);
        lv_label_set_text_fmt(s_usage_label, "%s / %s belegt", used_s, total_s);
    } else {
        lv_label_set_text(s_usage_label, "Speicherbelegung unbekannt");
    }
}

static void refresh_hint_label(void)
{
    char ip[16];
    if (wifi_module_get_state() == WIFI_MODULE_STATE_CONNECTED && wifi_module_get_ip_str(ip, sizeof(ip))) {
        lv_label_set_text_fmt(s_hint_label, "Hochladen: curl --data-binary @datei http://%s/api/files/datei", ip);
    } else {
        lv_label_set_text(s_hint_label, "Fuer Datei-Upload zuerst im Network-Screen mit Wi-Fi verbinden");
    }
}

static void close_modal_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_list(void);

static void delete_btn_cb(lv_event_t *e)
{
    (void)e;
    files_module_delete(s_current_name);
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
    refresh_list();
    refresh_usage_label();
}

static void open_modal_for(const char *name)
{
    strncpy(s_current_name, name, sizeof(s_current_name) - 1);
    s_current_name[sizeof(s_current_name) - 1] = '\0';
    lv_label_set_text(s_modal_title, name);

    static char preview[2048];
    bool is_text = false;
    if (files_module_read_preview(name, preview, sizeof(preview), &is_text) == ESP_OK && is_text) {
        lv_label_set_text(s_modal_body, preview[0] != '\0' ? preview : "(leere Datei)");
    } else {
        lv_label_set_text(s_modal_body, "Binaerdatei - keine Textvorschau");
    }
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
}

static void row_click_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    const char *name = (const char *)lv_obj_get_user_data(row);
    open_modal_for(name);
}

static void refresh_list(void)
{
    lv_obj_clean(s_list);

    storage_module_entry_t entries[32];
    size_t n = files_module_list(entries, 32);

    if (n == 0) {
        lv_obj_t *empty = lv_label_create(s_list);
        lv_label_set_text(empty, "Keine Dateien auf /storage");
        lv_obj_set_style_text_color(empty, THEME_COLOR_TEXT_DIM, 0);
        return;
    }

    static char name_storage[32][STORAGE_MODULE_MAX_NAME_LEN];
    for (size_t i = 0; i < n; i++) {
        strncpy(name_storage[i], entries[i].name, STORAGE_MODULE_MAX_NAME_LEN - 1);
        name_storage[i][STORAGE_MODULE_MAX_NAME_LEN - 1] = '\0';

        lv_obj_t *row = lv_obj_create(s_list);
        theme_apply_card(row);
        lv_obj_set_size(row, LV_PCT(100), 56);
        lv_obj_set_user_data(row, name_storage[i]);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *name_label = lv_label_create(row);
        lv_label_set_text(name_label, entries[i].name);

        char size_s[24];
        format_size(size_s, sizeof(size_s), entries[i].size_bytes);
        lv_obj_t *size_label = lv_label_create(row);
        lv_label_set_text(size_label, size_s);
        lv_obj_set_style_text_color(size_label, THEME_COLOR_TEXT_DIM, 0);
    }
}

static void refresh_btn_cb(lv_event_t *e)
{
    (void)e;
    refresh_list();
    refresh_usage_label();
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (s_modal) {
        lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);  // sonst bleibt das Modal beim naechsten Aufruf offen
    }
    nav_show(NAV_SCREEN_DASHBOARD);
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
    lv_obj_set_size(box, LV_PCT(90), LV_PCT(75));
    lv_obj_center(box);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 10, 0);

    lv_obj_t *header = lv_obj_create(box);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_modal_title = lv_label_create(header);
    lv_obj_set_style_text_color(s_modal_title, THEME_COLOR_TEXT, 0);

    lv_obj_t *close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 44, 44);
    lv_obj_add_event_cb(close_btn, close_modal_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_l = lv_label_create(close_btn);
    lv_label_set_text(close_l, LV_SYMBOL_CLOSE);
    lv_obj_center(close_l);

    s_modal_body = lv_label_create(box);
    lv_obj_set_width(s_modal_body, LV_PCT(100));
    lv_obj_set_flex_grow(s_modal_body, 1);
    lv_label_set_long_mode(s_modal_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_modal_body, THEME_COLOR_TEXT_DIM, 0);

    lv_obj_t *delete_btn = lv_btn_create(box);
    lv_obj_set_size(delete_btn, LV_PCT(100), THEME_TOUCH_TARGET_MIN);
    lv_obj_set_style_bg_color(delete_btn, THEME_COLOR_DANGER, 0);
    lv_obj_add_event_cb(delete_btn, delete_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *delete_l = lv_label_create(delete_btn);
    lv_label_set_text(delete_l, LV_SYMBOL_TRASH " Loeschen");
    lv_obj_center(delete_l);
}

static lv_obj_t *files_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_style_pad_top(scr, THEME_SCREEN_PAD_TOP, 0);   // ueberschreibt pad_all nur oben
    lv_obj_set_style_pad_bottom(scr, 74, 0);  // Platz fuer den schwebenden Zurueck-Button
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);  // sonst verschiebt Scrollen den schwebenden Zurueck-Button
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 8, 0);

    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 40);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Files");
    theme_apply_title(title);
    lv_obj_t *refresh_btn = lv_btn_create(header);
    lv_obj_add_event_cb(refresh_btn, refresh_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_l = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_l, LV_SYMBOL_REFRESH);

    s_back_btn = back_button_create(scr, back_cb);

    s_usage_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_usage_label, THEME_COLOR_TEXT_DIM, 0);

    s_hint_label = lv_label_create(scr);
    lv_obj_set_width(s_hint_label, LV_PCT(100));
    lv_label_set_long_mode(s_hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_hint_label, THEME_COLOR_TEXT_DIM, 0);

    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);  // fuellt Restplatz statt 100% Elternhoehe (ueberlappte sonst Header/Zurueck-Button)
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 6, 0);

    build_modal(scr);
    // Modal wird zuletzt angelegt und liegt damit im Z-Order ueber dem
    // Zurueck-Button - ohne dies waere der Button bei offenem Modal verdeckt.
    lv_obj_move_foreground(s_back_btn);

    return scr;
}

static void files_on_show(void)
{
    refresh_list();
    refresh_usage_label();
    refresh_hint_label();
}

void files_module_ui_register(void)
{
    nav_register(NAV_SCREEN_FILES, files_screen_create);
    nav_register_on_show(NAV_SCREEN_FILES, files_on_show);
}
