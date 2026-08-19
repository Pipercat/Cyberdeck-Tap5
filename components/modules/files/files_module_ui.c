#include "files_module_ui.h"
#include "sd_module.h"
#include "nav.h"
#include "theme.h"
#include "back_button.h"
#include "lvgl.h"
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>

static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_list = NULL;

static void format_size(uint64_t bytes, char *buf, size_t buf_len)
{
    const char *unit = "B";
    double val = (double)bytes;
    if (val >= 1024.0 * 1024.0 * 1024.0) { val /= 1024.0 * 1024.0 * 1024.0; unit = "GB"; }
    else if (val >= 1024.0 * 1024.0) { val /= 1024.0 * 1024.0; unit = "MB"; }
    else if (val >= 1024.0) { val /= 1024.0; unit = "KB"; }
    snprintf(buf, buf_len, "%.1f %s", val, unit);
}

static void refresh_listing(void)
{
    lv_obj_clean(s_list);
    if (!sd_module_is_mounted()) {
        lv_obj_t *l = lv_label_create(s_list);
        lv_label_set_text(l, "Keine Karte gemountet");
        lv_obj_set_style_text_color(l, THEME_COLOR_TEXT_DIM, 0);
        return;
    }

    DIR *dir = opendir(SD_MODULE_MOUNT_POINT);
    if (dir == NULL) {
        lv_obj_t *l = lv_label_create(s_list);
        lv_label_set_text(l, "Root-Verzeichnis konnte nicht gelesen werden");
        lv_obj_set_style_text_color(l, THEME_COLOR_DANGER, 0);
        return;
    }

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        char full_path[300];
        snprintf(full_path, sizeof(full_path), "%s/%s", SD_MODULE_MOUNT_POINT, entry->d_name);
        struct stat st;
        bool is_dir = (stat(full_path, &st) == 0) && S_ISDIR(st.st_mode);

        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text_fmt(name, "%s %s", is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, entry->d_name);

        lv_obj_t *size_l = lv_label_create(row);
        lv_obj_set_style_text_color(size_l, THEME_COLOR_TEXT_DIM, 0);
        if (is_dir) {
            lv_label_set_text(size_l, "DIR");
        } else {
            char size_buf[16];
            format_size((uint64_t)st.st_size, size_buf, sizeof(size_buf));
            lv_label_set_text(size_l, size_buf);
        }
        count++;
    }
    closedir(dir);

    if (count == 0) {
        lv_obj_t *l = lv_label_create(s_list);
        lv_label_set_text(l, "(leer)");
        lv_obj_set_style_text_color(l, THEME_COLOR_TEXT_DIM, 0);
    }
}

static void refresh_status(void)
{
    sd_module_status_t status;
    if (sd_module_mount(&status) == ESP_OK && status.mounted) {
        char size_buf[16];
        format_size(status.capacity_bytes, size_buf, sizeof(size_buf));
        lv_label_set_text_fmt(s_status_label, LV_SYMBOL_SD_CARD " %s (%s)",
                               status.card_name[0] ? status.card_name : "SD-Karte", size_buf);
        lv_obj_set_style_text_color(s_status_label, THEME_COLOR_SUCCESS, 0);
    } else {
        lv_label_set_text(s_status_label, LV_SYMBOL_SD_CARD " Keine Karte erkannt");
        lv_obj_set_style_text_color(s_status_label, THEME_COLOR_TEXT_DIM, 0);
    }
    refresh_listing();
}

static void retry_btn_cb(lv_event_t *e)
{
    (void)e;
    refresh_status();
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    nav_show(NAV_SCREEN_DASHBOARD);
}

static lv_obj_t *files_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_style_pad_top(scr, THEME_SCREEN_PAD_TOP, 0);   // ueberschreibt pad_all nur oben
    lv_obj_set_style_pad_bottom(scr, 74, 0);  // Platz fuer den schwebenden Zurueck-Button
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 10, 0);

    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 40);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Files (microSD)");
    theme_apply_title(title);

    back_button_create(scr, back_cb);

    lv_obj_t *status_row = lv_obj_create(scr);
    lv_obj_remove_style_all(status_row);
    lv_obj_set_size(status_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_status_label = lv_label_create(status_row);
    lv_label_set_text(s_status_label, "...");
    lv_obj_t *retry_btn = lv_btn_create(status_row);
    lv_obj_set_style_bg_color(retry_btn, THEME_COLOR_ACCENT, 0);
    lv_obj_add_event_cb(retry_btn, retry_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *retry_l = lv_label_create(retry_btn);
    lv_label_set_text(retry_l, LV_SYMBOL_REFRESH " Erneut pruefen");

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
    refresh_status();
}

void files_module_ui_register(void)
{
    nav_register(NAV_SCREEN_FILES, files_screen_create);
    nav_register_on_show(NAV_SCREEN_FILES, files_on_show);
}
