#include "system_module_ui.h"
#include "system_module.h"
#include "self_test.h"
#include "log_sink.h"
#include "nav.h"
#include "theme.h"
#include "back_button.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static lv_obj_t *s_stats_label = NULL;
static lv_obj_t *s_modal = NULL;
static lv_obj_t *s_modal_title = NULL;
static lv_obj_t *s_modal_body = NULL;
static lv_timer_t *s_refresh_timer = NULL;

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    system_module_stats_t stats;
    if (system_module_get_stats(&stats) != ESP_OK) return;

    uint32_t heap_used_pct = stats.heap_total_bytes
        ? (100 * (stats.heap_total_bytes - stats.heap_free_bytes)) / stats.heap_total_bytes : 0;
    uint32_t psram_used_pct = stats.psram_total_bytes
        ? (100 * (stats.psram_total_bytes - stats.psram_free_bytes)) / stats.psram_total_bytes : 0;

    int64_t up_s = stats.uptime_s;
    int up_h = (int)(up_s / 3600);
    int up_m = (int)((up_s % 3600) / 60);
    int up_sec = (int)(up_s % 60);

    char battery_line[48];
    if (stats.battery_voltage_v >= 0.0f) {
        snprintf(battery_line, sizeof(battery_line), "%d.%02d V", (int)stats.battery_voltage_v,
                 (int)(stats.battery_voltage_v * 100) % 100);
    } else {
        snprintf(battery_line, sizeof(battery_line), "nicht erreichbar");
    }

    char temp_line[32];
    if (stats.chip_temp_c > -900.0f) {
        snprintf(temp_line, sizeof(temp_line), "%d.%01d C (Die, nicht Umgebung)",
                 (int)stats.chip_temp_c, (int)(stats.chip_temp_c * 10) % 10);
    } else {
        snprintf(temp_line, sizeof(temp_line), "nicht verfuegbar");
    }

    lv_label_set_text_fmt(s_stats_label,
        "RAM (intern):  %" PRIu32 "%% belegt  (%" PRIu32 " / %" PRIu32 " KB frei)\n"
        "PSRAM:         %" PRIu32 "%% belegt  (%" PRIu32 " / %" PRIu32 " KB frei)\n"
        "Flash:         %" PRIu32 " MB\n"
        "Chip-Temperatur: %s\n"
        "Akku-Spannung:   %s\n"
        "Laufzeit:      %02d:%02d:%02d\n"
        "SD-Karte:      nicht eingebunden (Phase 4)\n"
        "Wi-Fi:         nicht verbunden (Phase 4)\n"
        "ESP-IDF:       %s\n"
        "Firmware:      %s",
        heap_used_pct, stats.heap_free_bytes / 1024, stats.heap_total_bytes / 1024,
        psram_used_pct, stats.psram_free_bytes / 1024, stats.psram_total_bytes / 1024,
        stats.flash_size_bytes / (1024 * 1024),
        temp_line, battery_line, up_h, up_m, up_sec,
        stats.idf_version, stats.project_version);
}

static void close_modal_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
}

static void open_modal(const char *title)
{
    lv_label_set_text(s_modal_title, title);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
}

static void logs_btn_cb(lv_event_t *e)
{
    (void)e;
    static char buf[4096];
    size_t n = log_sink_read_recent(buf, sizeof(buf));
    lv_obj_clean(s_modal_body);
    lv_obj_t *ta = lv_textarea_create(s_modal_body);
    lv_obj_set_size(ta, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(ta, LV_OBJ_FLAG_CLICKABLE);
    lv_textarea_set_text(ta, n > 0 ? buf : "(keine Log-Eintraege)");
    open_modal("Logs");
}

static void selftest_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_clean(s_modal_body);
    lv_obj_set_flex_flow(s_modal_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_modal_body, 6, 0);

    self_test_result_t results[32];
    size_t count = 0;
    self_test_run_all(results, 32, &count);

    if (count == 0) {
        lv_obj_t *l = lv_label_create(s_modal_body);
        lv_label_set_text(l, "Keine Selbsttests registriert");
        lv_obj_set_style_text_color(l, THEME_COLOR_TEXT_DIM, 0);
    }
    for (size_t i = 0; i < count; i++) {
        lv_obj_t *row = lv_obj_create(s_modal_body);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, results[i].name);
        lv_obj_set_style_text_color(name, THEME_COLOR_TEXT, 0);

        const char *status_str;
        lv_color_t color;
        switch (results[i].status) {
            case SELF_TEST_PASS: status_str = "PASS"; color = THEME_COLOR_SUCCESS; break;
            case SELF_TEST_FAIL: status_str = "FAIL"; color = THEME_COLOR_DANGER; break;
            case SELF_TEST_SKIPPED: status_str = "SKIPPED"; color = THEME_COLOR_WARNING; break;
            default: status_str = "N/A"; color = THEME_COLOR_TEXT_DIM; break;
        }
        lv_obj_t *status = lv_label_create(row);
        if (results[i].detail != NULL) {
            lv_label_set_text_fmt(status, "%s (%s)", status_str, results[i].detail);
        } else {
            lv_label_set_text(status, status_str);
        }
        lv_obj_set_style_text_color(status, color, 0);
    }
    open_modal("Self Test");
}

static void restart_confirm_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *txt = lv_label_get_text(lv_obj_get_child(btn, 0));
    lv_obj_t *msgbox = (lv_obj_t *)lv_event_get_user_data(e);
    if (txt != NULL && strcmp(txt, "Neustart") == 0) {
        system_module_restart_device();
    }
    lv_msgbox_close(msgbox);
}

static void restart_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Geraet neu starten?");
    lv_msgbox_add_text(mbox, "Alle nicht gespeicherten Zustaende (z.B. laufende PWM-/Serial-Tests) gehen verloren.");
    lv_obj_t *btn_cancel = lv_msgbox_add_footer_button(mbox, "Abbrechen");
    lv_obj_t *btn_confirm = lv_msgbox_add_footer_button(mbox, "Neustart");
    lv_obj_set_style_bg_color(btn_confirm, THEME_COLOR_DANGER, 0);
    lv_obj_add_event_cb(btn_cancel, restart_confirm_event_cb, LV_EVENT_CLICKED, mbox);
    lv_obj_add_event_cb(btn_confirm, restart_confirm_event_cb, LV_EVENT_CLICKED, mbox);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (s_refresh_timer) lv_timer_pause(s_refresh_timer);
    nav_show(NAV_SCREEN_DASHBOARD);
}

static lv_obj_t *system_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_set_style_pad_top(scr, 48, 0);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_style_pad_bottom(scr, 74, 0);  // Platz fuer den schwebenden Zurueck-Button
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 12, 0);

    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 40);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "System Monitor");
    lv_obj_set_style_text_color(title, THEME_COLOR_TEXT, 0);

    back_button_create(scr, back_cb);

    lv_obj_t *stats_card = lv_obj_create(scr);
    theme_apply_card(stats_card);
    lv_obj_set_size(stats_card, LV_PCT(100), LV_SIZE_CONTENT);
    s_stats_label = lv_label_create(stats_card);
    lv_obj_set_style_text_color(s_stats_label, THEME_COLOR_TEXT, 0);
    lv_label_set_text(s_stats_label, "Lade...");

    lv_obj_t *action_row = lv_obj_create(scr);
    lv_obj_remove_style_all(action_row);
    lv_obj_set_size(action_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(action_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(action_row, 10, 0);
    lv_obj_set_style_pad_row(action_row, 10, 0);

    lv_obj_t *logs_btn = lv_btn_create(action_row);
    lv_obj_add_event_cb(logs_btn, logs_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *logs_l = lv_label_create(logs_btn); lv_label_set_text(logs_l, "Logs");

    lv_obj_t *selftest_btn = lv_btn_create(action_row);
    lv_obj_add_event_cb(selftest_btn, selftest_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *st_l = lv_label_create(selftest_btn); lv_label_set_text(st_l, "Self Test");

    lv_obj_t *restart_btn = lv_btn_create(action_row);
    lv_obj_set_style_bg_color(restart_btn, THEME_COLOR_DANGER, 0);
    lv_obj_add_event_cb(restart_btn, restart_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *restart_l = lv_label_create(restart_btn); lv_label_set_text(restart_l, LV_SYMBOL_POWER " Restart");

    // --- Modal fuer Logs/Self-Test ---
    s_modal = lv_obj_create(scr);
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_70, 0);
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *box = lv_obj_create(s_modal);
    theme_apply_card(box);
    lv_obj_set_size(box, LV_PCT(90), LV_PCT(80));
    lv_obj_center(box);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *modal_header = lv_obj_create(box);
    lv_obj_remove_style_all(modal_header);
    lv_obj_set_size(modal_header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(modal_header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(modal_header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_modal_title = lv_label_create(modal_header);
    lv_obj_set_style_text_color(s_modal_title, THEME_COLOR_TEXT, 0);
    lv_obj_t *close_btn = lv_btn_create(modal_header);
    lv_obj_set_size(close_btn, 44, 44);
    lv_obj_add_event_cb(close_btn, close_modal_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_l = lv_label_create(close_btn);
    lv_label_set_text(close_l, LV_SYMBOL_CLOSE);
    lv_obj_center(close_l);
    s_modal_body = lv_obj_create(box);
    lv_obj_remove_style_all(s_modal_body);
    lv_obj_set_size(s_modal_body, LV_PCT(100), LV_PCT(90));

    return scr;
}

static void system_on_show(void)
{
    if (s_refresh_timer == NULL) {
        s_refresh_timer = lv_timer_create(refresh_timer_cb, 1000, NULL);
        refresh_timer_cb(NULL);
    } else {
        lv_timer_resume(s_refresh_timer);
    }
}

void system_module_ui_register(void)
{
    nav_register(NAV_SCREEN_SYSTEM, system_screen_create);
    nav_register_on_show(NAV_SCREEN_SYSTEM, system_on_show);
}
