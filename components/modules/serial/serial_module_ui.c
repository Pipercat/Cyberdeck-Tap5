#include "serial_module_ui.h"
#include "serial_module.h"
#include "nav.h"
#include "theme.h"
#include "pin_table.h"
#include "back_button.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_timer.h"

#define MAX_LOG_CHARS 8000
#define POLL_CHUNK    256

static int s_pins[32];
static int s_pin_count = 0;

static lv_obj_t *s_tx_dropdown = NULL;
static lv_obj_t *s_rx_dropdown = NULL;
static lv_obj_t *s_baud_dropdown = NULL;
static lv_obj_t *s_start_stop_btn = NULL;
static lv_obj_t *s_start_stop_label = NULL;
static lv_obj_t *s_hex_toggle = NULL;
static lv_obj_t *s_autoscroll_toggle = NULL;
static lv_obj_t *s_timestamp_toggle = NULL;
static lv_obj_t *s_log_area = NULL;
static lv_obj_t *s_send_input = NULL;
static lv_obj_t *s_keyboard = NULL;
static lv_obj_t *s_back_btn = NULL;
static lv_timer_t *s_poll_timer = NULL;

static bool s_hex_mode = false;
static bool s_autoscroll = true;
static bool s_timestamp = false;

static void collect_pins(void)
{
    s_pin_count = 0;
    for (size_t i = 0; i < g_pin_table_count && s_pin_count < 32; i++) {
        if (g_pin_table[i].role == PIN_ROLE_FREE) {
            s_pins[s_pin_count++] = g_pin_table[i].gpio_num;
        }
    }
}

static void append_log(const char *text)
{
    if (s_timestamp) {
        char ts[24];
        snprintf(ts, sizeof(ts), "[%" PRIu32 " ms] ", (uint32_t)(esp_timer_get_time() / 1000));
        lv_textarea_add_text(s_log_area, ts);
    }
    lv_textarea_add_text(s_log_area, text);

    if (lv_textarea_get_text(s_log_area) && strlen(lv_textarea_get_text(s_log_area)) > MAX_LOG_CHARS) {
        lv_textarea_set_text(s_log_area, "");  // einfache Kappung statt unbegrenztem Wachstum
    }
    if (s_autoscroll) {
        lv_textarea_set_cursor_pos(s_log_area, LV_TEXTAREA_CURSOR_LAST);
    }
}

static void poll_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!serial_module_is_running()) return;

    uint8_t buf[POLL_CHUNK];
    size_t n = serial_module_read(buf, sizeof(buf) - 1);
    if (n == 0) return;

    if (s_hex_mode) {
        char hex[POLL_CHUNK * 3 + 1];
        hex[0] = '\0';
        for (size_t i = 0; i < n; i++) {
            char b[4];
            snprintf(b, sizeof(b), "%02X ", buf[i]);
            strcat(hex, b);
        }
        append_log(hex);
    } else {
        buf[n] = '\0';
        append_log((const char *)buf);
    }
}

static void start_stop_cb(lv_event_t *e)
{
    (void)e;
    if (serial_module_is_running()) {
        serial_module_stop();
        lv_label_set_text(s_start_stop_label, LV_SYMBOL_PLAY " Start");
        theme_set_button_variant(s_start_stop_btn, THEME_BTN_SUCCESS);
        return;
    }
    if (s_pin_count == 0) return;

    static const int32_t bauds[] = { 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600 };
    uint32_t tx_idx = lv_dropdown_get_selected(s_tx_dropdown);
    uint32_t rx_idx = lv_dropdown_get_selected(s_rx_dropdown);
    uint32_t baud_idx = lv_dropdown_get_selected(s_baud_dropdown);
    if (tx_idx >= (uint32_t)s_pin_count || rx_idx >= (uint32_t)s_pin_count) return;

    esp_err_t err = serial_module_start(s_pins[tx_idx], s_pins[rx_idx], bauds[baud_idx]);
    if (err != ESP_OK) return;

    lv_label_set_text(s_start_stop_label, LV_SYMBOL_STOP " Stop");
    theme_set_button_variant(s_start_stop_btn, THEME_BTN_DANGER);
}

static void hex_toggle_cb(lv_event_t *e)
{
    (void)e;
    s_hex_mode = !s_hex_mode;
    theme_set_toggle_active(s_hex_toggle, s_hex_mode);
}

static void autoscroll_toggle_cb(lv_event_t *e)
{
    (void)e;
    s_autoscroll = !s_autoscroll;
    theme_set_toggle_active(s_autoscroll_toggle, s_autoscroll);
}

static void timestamp_toggle_cb(lv_event_t *e)
{
    (void)e;
    s_timestamp = !s_timestamp;
    theme_set_toggle_active(s_timestamp_toggle, s_timestamp);
}

static void clear_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_textarea_set_text(s_log_area, "");
}

static void send_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!serial_module_is_running()) return;
    const char *text = lv_textarea_get_text(s_send_input);
    if (text == NULL || text[0] == '\0') return;
    serial_module_write((const uint8_t *)text, strlen(text));
    serial_module_write((const uint8_t *)"\r\n", 2);
    lv_textarea_set_text(s_send_input, "");
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    serial_module_stop();
    if (s_poll_timer) lv_timer_pause(s_poll_timer);
    nav_show(NAV_SCREEN_DASHBOARD);
}

static void send_input_focus_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);  // Tastatur deckt den Button sonst ab
    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_keyboard, s_send_input);
}

static void send_input_defocus_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *serial_screen_create(void)
{
    collect_pins();

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
    lv_label_set_text(title, "Serial Terminal");
    theme_apply_title(title);

    s_back_btn = back_button_create(scr, back_cb);

    // --- Steuerzeile 1: Pins + Baud + Start/Stop ---
    lv_obj_t *ctrl_row = lv_obj_create(scr);
    lv_obj_remove_style_all(ctrl_row);
    lv_obj_set_size(ctrl_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctrl_row, 8, 0);
    lv_obj_set_style_pad_row(ctrl_row, 8, 0);

    static char pin_opts[512];
    pin_opts[0] = '\0';
    for (int i = 0; i < s_pin_count; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "GPIO %d\n", s_pins[i]);
        strncat(pin_opts, buf, sizeof(pin_opts) - strlen(pin_opts) - 1);
    }
    lv_obj_t *tx_label = lv_label_create(ctrl_row);
    lv_label_set_text(tx_label, "TX:");
    lv_obj_set_style_text_color(tx_label, THEME_COLOR_TEXT_DIM, 0);
    s_tx_dropdown = lv_dropdown_create(ctrl_row);
    lv_dropdown_set_options(s_tx_dropdown, pin_opts);

    lv_obj_t *rx_label = lv_label_create(ctrl_row);
    lv_label_set_text(rx_label, "RX:");
    lv_obj_set_style_text_color(rx_label, THEME_COLOR_TEXT_DIM, 0);
    s_rx_dropdown = lv_dropdown_create(ctrl_row);
    lv_dropdown_set_options(s_rx_dropdown, pin_opts);
    if (s_pin_count > 1) lv_dropdown_set_selected(s_rx_dropdown, 1);

    s_baud_dropdown = lv_dropdown_create(ctrl_row);
    lv_dropdown_set_options(s_baud_dropdown,
        "9600\n19200\n38400\n57600\n115200\n230400\n460800\n921600");
    lv_dropdown_set_selected(s_baud_dropdown, 4);  // 115200 Default

    s_start_stop_btn = lv_btn_create(ctrl_row);
    theme_apply_button(s_start_stop_btn, THEME_BTN_SUCCESS);
    lv_obj_add_event_cb(s_start_stop_btn, start_stop_cb, LV_EVENT_CLICKED, NULL);
    s_start_stop_label = lv_label_create(s_start_stop_btn);
    lv_label_set_text(s_start_stop_label, LV_SYMBOL_PLAY " Start");

    // --- Steuerzeile 2: Toggles ---
    lv_obj_t *toggle_row = lv_obj_create(scr);
    lv_obj_remove_style_all(toggle_row);
    lv_obj_set_size(toggle_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(toggle_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(toggle_row, 8, 0);

    s_hex_toggle = lv_btn_create(toggle_row);
    theme_apply_toggle(s_hex_toggle, s_hex_mode);
    lv_obj_add_event_cb(s_hex_toggle, hex_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hex_l = lv_label_create(s_hex_toggle); lv_label_set_text(hex_l, "HEX");

    s_autoscroll_toggle = lv_btn_create(toggle_row);
    theme_apply_toggle(s_autoscroll_toggle, s_autoscroll);
    lv_obj_add_event_cb(s_autoscroll_toggle, autoscroll_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *as_l = lv_label_create(s_autoscroll_toggle); lv_label_set_text(as_l, "Autoscroll");

    s_timestamp_toggle = lv_btn_create(toggle_row);
    theme_apply_toggle(s_timestamp_toggle, s_timestamp);
    lv_obj_add_event_cb(s_timestamp_toggle, timestamp_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ts_l = lv_label_create(s_timestamp_toggle); lv_label_set_text(ts_l, "Timestamp");

    lv_obj_t *clear_btn = lv_btn_create(toggle_row);
    theme_apply_button(clear_btn, THEME_BTN_WARNING);
    lv_obj_add_event_cb(clear_btn, clear_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *clear_l = lv_label_create(clear_btn); lv_label_set_text(clear_l, "Clear");

    // --- Log-Anzeige ---
    s_log_area = lv_textarea_create(scr);
    lv_obj_set_size(s_log_area, LV_PCT(100), LV_PCT(55));
    lv_textarea_set_text(s_log_area, "");
    lv_obj_clear_flag(s_log_area, LV_OBJ_FLAG_CLICKABLE);  // read-only: kein Fokus/Tastatur durch Touch
    theme_apply_card(s_log_area);

    // --- Senden ---
    lv_obj_t *send_row = lv_obj_create(scr);
    lv_obj_remove_style_all(send_row);
    lv_obj_set_size(send_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(send_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(send_row, 8, 0);
    s_send_input = lv_textarea_create(send_row);
    lv_obj_set_width(s_send_input, LV_PCT(75));
    lv_textarea_set_one_line(s_send_input, true);
    lv_textarea_set_placeholder_text(s_send_input, "Text senden...");
    lv_obj_t *send_btn = lv_btn_create(send_row);
    theme_apply_button(send_btn, THEME_BTN_PRIMARY);
    lv_obj_add_event_cb(send_btn, send_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *send_l = lv_label_create(send_btn);
    lv_label_set_text(send_l, LV_SYMBOL_RIGHT " Send");

    lv_obj_add_event_cb(s_send_input, send_input_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_send_input, send_input_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    s_keyboard = lv_keyboard_create(scr);
    lv_keyboard_set_textarea(s_keyboard, s_send_input);
    // Schwebend statt im Flex-Flow, sonst waechst der Screen-Inhalt beim
    // Einblenden ueber die Bildschirmhoehe hinaus und scr faengt an zu
    // scrollen - das verschiebt den schwebenden Zurueck-Button mit.
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);

    return scr;
}

static void serial_on_show(void)
{
    if (s_poll_timer == NULL) {
        s_poll_timer = lv_timer_create(poll_timer_cb, 80, NULL);
    } else {
        lv_timer_resume(s_poll_timer);
    }
}

void serial_module_ui_register(void)
{
    nav_register(NAV_SCREEN_SERIAL, serial_screen_create);
    nav_register_on_show(NAV_SCREEN_SERIAL, serial_on_show);
}
