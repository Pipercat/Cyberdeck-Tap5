#include "adc_module_ui.h"
#include "adc_module.h"
#include "nav.h"
#include "theme.h"
#include "pin_table.h"
#include "back_button.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

#define CHART_POINT_COUNT 100
#define STATS_WINDOW      CHART_POINT_COUNT

static int s_adc_pins[32];
static int s_adc_pin_count = 0;

static lv_obj_t *s_chart = NULL;
static lv_chart_series_t *s_series = NULL;
static lv_obj_t *s_pin_dropdown = NULL;
static lv_obj_t *s_rate_dropdown = NULL;
static lv_obj_t *s_pause_btn = NULL;
static lv_obj_t *s_pause_label = NULL;
static lv_obj_t *s_min_label = NULL;
static lv_obj_t *s_max_label = NULL;
static lv_obj_t *s_avg_label = NULL;
static lv_obj_t *s_current_label = NULL;
static lv_timer_t *s_sample_timer = NULL;

static int s_history_mv[STATS_WINDOW];
static int s_history_count = 0;
static int s_history_head = 0;
static bool s_paused = false;

static void collect_adc_pins(void)
{
    s_adc_pin_count = 0;
    for (size_t i = 0; i < g_pin_table_count && s_adc_pin_count < 32; i++) {
        if (g_pin_table[i].role == PIN_ROLE_FREE && g_pin_table[i].supports_adc) {
            s_adc_pins[s_adc_pin_count++] = g_pin_table[i].gpio_num;
        }
    }
}

static void recompute_stats(void)
{
    if (s_history_count == 0) {
        lv_label_set_text(s_min_label, "Min: --");
        lv_label_set_text(s_max_label, "Max: --");
        lv_label_set_text(s_avg_label, "Avg: --");
        return;
    }
    int min_v = s_history_mv[0], max_v = s_history_mv[0];
    long sum = 0;
    for (int i = 0; i < s_history_count; i++) {
        int v = s_history_mv[i];
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
        sum += v;
    }
    int avg_v = (int)(sum / s_history_count);
    lv_label_set_text_fmt(s_min_label, "Min: %d.%02d V", min_v / 1000, (min_v % 1000) / 10);
    lv_label_set_text_fmt(s_max_label, "Max: %d.%02d V", max_v / 1000, (max_v % 1000) / 10);
    lv_label_set_text_fmt(s_avg_label, "Avg: %d.%02d V", avg_v / 1000, (avg_v % 1000) / 10);
}

static void sample_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_paused) return;
    int raw = 0, mv = 0;
    if (adc_module_read(&raw, &mv) != ESP_OK) return;

    lv_chart_set_next_value(s_chart, s_series, mv);
    lv_label_set_text_fmt(s_current_label, "ca. %d.%02d V (Rohwert %d, unkalibriert)",
                           mv / 1000, (mv % 1000) / 10, raw);

    s_history_mv[s_history_head] = mv;
    s_history_head = (s_history_head + 1) % STATS_WINDOW;
    if (s_history_count < STATS_WINDOW) s_history_count++;
    recompute_stats();
}

static void pin_dropdown_cb(lv_event_t *e)
{
    (void)e;
    uint32_t idx = lv_dropdown_get_selected(s_pin_dropdown);
    if (idx < (uint32_t)s_adc_pin_count) {
        adc_module_select(s_adc_pins[idx]);
        s_history_count = 0;
        s_history_head = 0;
        lv_chart_set_all_value(s_chart, s_series, LV_CHART_POINT_NONE);
    }
}

static void rate_dropdown_cb(lv_event_t *e)
{
    (void)e;
    static const uint32_t periods_ms[] = { 100, 250, 500, 1000 };
    uint32_t idx = lv_dropdown_get_selected(s_rate_dropdown);
    if (idx < sizeof(periods_ms) / sizeof(periods_ms[0]) && s_sample_timer != NULL) {
        lv_timer_set_period(s_sample_timer, periods_ms[idx]);
    }
}

static void pause_btn_cb(lv_event_t *e)
{
    (void)e;
    s_paused = !s_paused;
    lv_label_set_text(s_pause_label, s_paused ? LV_SYMBOL_PLAY " Fortsetzen" : LV_SYMBOL_PAUSE " Pause");
    lv_obj_set_style_bg_color(s_pause_btn, s_paused ? THEME_COLOR_SUCCESS : THEME_COLOR_SURFACE_HI, 0);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (s_sample_timer) {
        lv_timer_pause(s_sample_timer);
    }
    adc_module_release();
    nav_show(NAV_SCREEN_DASHBOARD);
}

static lv_obj_t *adc_screen_create(void)
{
    collect_adc_pins();

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
    lv_label_set_text(title, "ADC / Mini-Oszilloskop");
    theme_apply_title(title);

    back_button_create(scr, back_cb);

    // --- Steuerzeile ---
    lv_obj_t *ctrl_row = lv_obj_create(scr);
    lv_obj_remove_style_all(ctrl_row);
    lv_obj_set_size(ctrl_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctrl_row, 10, 0);

    static char pin_opts[512];
    pin_opts[0] = '\0';
    for (int i = 0; i < s_adc_pin_count; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "GPIO %d\n", s_adc_pins[i]);
        strncat(pin_opts, buf, sizeof(pin_opts) - strlen(pin_opts) - 1);
    }
    s_pin_dropdown = lv_dropdown_create(ctrl_row);
    lv_dropdown_set_options(s_pin_dropdown, pin_opts);
    lv_obj_add_event_cb(s_pin_dropdown, pin_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_rate_dropdown = lv_dropdown_create(ctrl_row);
    lv_dropdown_set_options(s_rate_dropdown, "100 ms\n250 ms\n500 ms\n1000 ms");
    lv_dropdown_set_selected(s_rate_dropdown, 1);
    lv_obj_add_event_cb(s_rate_dropdown, rate_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_pause_btn = lv_btn_create(ctrl_row);
    lv_obj_add_event_cb(s_pause_btn, pause_btn_cb, LV_EVENT_CLICKED, NULL);
    s_pause_label = lv_label_create(s_pause_btn);
    lv_label_set_text(s_pause_label, LV_SYMBOL_PAUSE " Pause");

    // --- Aktueller Wert ---
    s_current_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_current_label, THEME_COLOR_ACCENT, 0);
    lv_label_set_text(s_current_label, "--");

    // --- Chart ---
    s_chart = lv_chart_create(scr);
    lv_obj_set_size(s_chart, LV_PCT(100), 260);
    theme_apply_card(s_chart);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, CHART_POINT_COUNT);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 3300);
    lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(s_chart, 4, 8);
    s_series = lv_chart_add_series(s_chart, THEME_COLOR_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(s_chart, s_series, LV_CHART_POINT_NONE);

    // --- Min/Max/Avg ---
    lv_obj_t *stats_row = lv_obj_create(scr);
    lv_obj_remove_style_all(stats_row);
    lv_obj_set_size(stats_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stats_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_min_label = lv_label_create(stats_row);
    lv_obj_set_style_text_color(s_min_label, THEME_COLOR_TEXT_DIM, 0);
    s_max_label = lv_label_create(stats_row);
    lv_obj_set_style_text_color(s_max_label, THEME_COLOR_TEXT_DIM, 0);
    s_avg_label = lv_label_create(stats_row);
    lv_obj_set_style_text_color(s_avg_label, THEME_COLOR_TEXT_DIM, 0);
    recompute_stats();

    lv_obj_t *note = lv_label_create(scr);
    lv_label_set_text(note, "Kein Laborersatz - unkalibrierte Naeherung, geeignet fuer Sensoren/Potentiometer/langsame Signale");
    lv_obj_set_style_text_color(note, THEME_COLOR_TEXT_DIM, 0);

    return scr;
}

// Wird beim ersten Aufbau UND bei jedem erneuten Betreten des Screens
// aufgerufen (siehe nav_register_on_show) - reaktiviert Pin-Auswahl und
// Sample-Timer, die back_cb() beim Verlassen deaktiviert hat.
static void adc_on_show(void)
{
    s_history_count = 0;
    s_history_head = 0;
    s_paused = false;
    if (s_pause_label != NULL) {
        lv_label_set_text(s_pause_label, LV_SYMBOL_PAUSE " Pause");
        lv_obj_set_style_bg_color(s_pause_btn, THEME_COLOR_SURFACE_HI, 0);
    }
    if (s_chart != NULL && s_series != NULL) {
        lv_chart_set_all_value(s_chart, s_series, LV_CHART_POINT_NONE);
    }
    if (s_adc_pin_count > 0) {
        adc_module_select(s_adc_pins[0]);
        if (s_pin_dropdown != NULL) {
            lv_dropdown_set_selected(s_pin_dropdown, 0);
        }
    }
    if (s_sample_timer == NULL) {
        s_sample_timer = lv_timer_create(sample_timer_cb, 250, NULL);
    } else {
        lv_timer_resume(s_sample_timer);
        lv_timer_set_period(s_sample_timer, 250);
    }
}

void adc_module_ui_register(void)
{
    nav_register(NAV_SCREEN_ADC, adc_screen_create);
    nav_register_on_show(NAV_SCREEN_ADC, adc_on_show);
}
