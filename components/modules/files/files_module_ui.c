#include "files_module_ui.h"
#include "storage.h"
#include "nav.h"
#include "theme.h"
#include "bottom_nav.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

#define FILES_MAX_LISTED 32

static lv_obj_t *s_usage_label = NULL;
static lv_obj_t *s_list = NULL;
static lv_timer_t *s_refresh_timer = NULL;

static void format_size(size_t bytes, char *out, size_t out_len)
{
    if (bytes >= 1024 * 1024) {
        snprintf(out, out_len, "%.1f MB", bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(out, out_len, "%.1f KB", bytes / 1024.0);
    } else {
        snprintf(out, out_len, "%u B", (unsigned)bytes);
    }
}

static void refresh_list(void);

static void delete_confirm_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *txt = lv_label_get_text(lv_obj_get_child(btn, 0));
    lv_obj_t *msgbox = (lv_obj_t *)lv_event_get_user_data(e);
    const char *name = (const char *)lv_obj_get_user_data(msgbox);
    if (txt != NULL && strcmp(txt, "Loeschen") == 0 && name != NULL) {
        storage_delete_file(name);
        refresh_list();
    }
    lv_msgbox_close(msgbox);
}

static void delete_btn_cb(lv_event_t *e)
{
    const char *name = (const char *)lv_event_get_user_data(e);
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Datei loeschen?");
    lv_msgbox_add_text(mbox, name);
    lv_obj_set_user_data(mbox, (void *)name);
    lv_obj_t *btn_cancel = lv_msgbox_add_footer_button(mbox, "Abbrechen");
    lv_obj_t *btn_confirm = lv_msgbox_add_footer_button(mbox, "Loeschen");
    theme_apply_button(btn_cancel, THEME_BTN_NEUTRAL);
    theme_apply_button(btn_confirm, THEME_BTN_DANGER);
    lv_obj_add_event_cb(btn_cancel, delete_confirm_cb, LV_EVENT_CLICKED, mbox);
    lv_obj_add_event_cb(btn_confirm, delete_confirm_cb, LV_EVENT_CLICKED, mbox);
}

static void refresh_list(void)
{
    size_t used = 0, total = 0;
    if (storage_get_usage(&used, &total)) {
        char used_str[16], total_str[16];
        format_size(used, used_str, sizeof(used_str));
        format_size(total, total_str, sizeof(total_str));
        lv_label_set_text_fmt(s_usage_label, "%s / %s belegt", used_str, total_str);
    } else {
        lv_label_set_text(s_usage_label, "Speicher nicht verfuegbar");
    }

    lv_obj_clean(s_list);

    static storage_file_t files[FILES_MAX_LISTED];
    static char name_storage[FILES_MAX_LISTED][STORAGE_MAX_NAME_LEN];
    size_t n = storage_list_files(files, FILES_MAX_LISTED);

    if (n == 0) {
        lv_obj_t *empty = lv_label_create(s_list);
        lv_label_set_text(empty, "Keine Dateien");
        lv_obj_set_style_text_color(empty, THEME_COLOR_TEXT_DIM, 0);
        return;
    }

    for (size_t i = 0; i < n; i++) {
        strncpy(name_storage[i], files[i].name, STORAGE_MAX_NAME_LEN - 1);
        name_storage[i][STORAGE_MAX_NAME_LEN - 1] = '\0';

        lv_obj_t *row = lv_obj_create(s_list);
        theme_apply_card(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *name_col = lv_obj_create(row);
        lv_obj_remove_style_all(name_col);
        lv_obj_set_size(name_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(name_col, LV_FLEX_FLOW_COLUMN);

        lv_obj_t *name_l = lv_label_create(name_col);
        lv_label_set_text(name_l, name_storage[i]);

        char size_str[16];
        format_size(files[i].size_bytes, size_str, sizeof(size_str));
        lv_obj_t *size_l = lv_label_create(name_col);
        lv_label_set_text(size_l, size_str);
        lv_obj_set_style_text_color(size_l, THEME_COLOR_TEXT_DIM, 0);

        lv_obj_t *del_btn = lv_btn_create(row);
        theme_apply_button(del_btn, THEME_BTN_DANGER);
        lv_obj_set_size(del_btn, 44, 44);
        lv_obj_add_event_cb(del_btn, delete_btn_cb, LV_EVENT_CLICKED, name_storage[i]);
        lv_obj_t *del_l = lv_label_create(del_btn);
        lv_label_set_text(del_l, LV_SYMBOL_TRASH);
        lv_obj_center(del_l);
    }
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_list();
}

static lv_obj_t *files_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_style_pad_top(scr, THEME_SCREEN_PAD_TOP, 0);
    lv_obj_set_style_pad_bottom(scr, BOTTOM_NAV_HEIGHT + 16, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 12, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Files");
    theme_apply_title(title);

    s_usage_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_usage_label, THEME_COLOR_TEXT_DIM, 0);
    lv_label_set_text(s_usage_label, "Lade...");

    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 6, 0);

    return scr;
}

static void files_on_show(void)
{
    storage_init();
    if (s_refresh_timer == NULL) {
        s_refresh_timer = lv_timer_create(refresh_timer_cb, 2000, NULL);
    } else {
        lv_timer_resume(s_refresh_timer);
    }
    refresh_list();
}

void files_module_ui_register(void)
{
    nav_register(NAV_SCREEN_FILES, files_screen_create);
    nav_register_on_show(NAV_SCREEN_FILES, files_on_show);
}
