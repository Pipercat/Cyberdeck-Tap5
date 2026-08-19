#include "i2c_module_ui.h"
#include "i2c_module.h"
#include "nav.h"
#include "theme.h"
#include "back_button.h"
#include "lvgl.h"

static lv_obj_t *s_list = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_speed_dropdown = NULL;

static lv_obj_t *s_modal = NULL;
static lv_obj_t *s_modal_title = NULL;
static lv_obj_t *s_reg_label = NULL;
static lv_obj_t *s_val_label = NULL;
static lv_obj_t *s_read_result_label = NULL;
static uint8_t s_current_addr = 0;
static int s_reg = 0;
static int s_val = 0;

static void update_reg_label(void) { lv_label_set_text_fmt(s_reg_label, "Register: 0x%02X (%d)", s_reg, s_reg); }
static void update_val_label(void) { lv_label_set_text_fmt(s_val_label, "Wert: 0x%02X (%d)", s_val, s_val); }

static void reg_step_cb(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    s_reg = (s_reg + delta) & 0xFF;
    update_reg_label();
}

static void val_step_cb(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    s_val = (s_val + delta) & 0xFF;
    update_val_label();
}

static void read_btn_cb(lv_event_t *e)
{
    (void)e;
    uint8_t out = 0;
    esp_err_t err = i2c_module_read_reg(s_current_addr, (uint8_t)s_reg, &out);
    if (err == ESP_OK) {
        lv_label_set_text_fmt(s_read_result_label, "Gelesen: 0x%02X (%d)", out, out);
        lv_obj_set_style_text_color(s_read_result_label, THEME_COLOR_SUCCESS, 0);
    } else {
        lv_label_set_text_fmt(s_read_result_label, "Fehler: %s", esp_err_to_name(err));
        lv_obj_set_style_text_color(s_read_result_label, THEME_COLOR_DANGER, 0);
    }
}

static void write_btn_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t err = i2c_module_write_reg(s_current_addr, (uint8_t)s_reg, (uint8_t)s_val);
    if (err == ESP_OK) {
        lv_label_set_text(s_read_result_label, "Geschrieben");
        lv_obj_set_style_text_color(s_read_result_label, THEME_COLOR_SUCCESS, 0);
    } else {
        lv_label_set_text_fmt(s_read_result_label, "Fehler: %s", esp_err_to_name(err));
        lv_obj_set_style_text_color(s_read_result_label, THEME_COLOR_DANGER, 0);
    }
}

static void close_modal_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
}

static void open_modal_for_addr(uint8_t addr)
{
    s_current_addr = addr;
    s_reg = 0;
    s_val = 0;
    update_reg_label();
    update_val_label();
    lv_label_set_text(s_read_result_label, "--");
    lv_obj_set_style_text_color(s_read_result_label, THEME_COLOR_TEXT_DIM, 0);

    const char *guess = i2c_module_guess_device_name(addr);
    if (guess) {
        lv_label_set_text_fmt(s_modal_title, "0x%02X - moegl. %s", addr, guess);
    } else {
        lv_label_set_text_fmt(s_modal_title, "0x%02X - unbekanntes Geraet", addr);
    }
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
}

static void device_row_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    uint8_t addr = (uint8_t)(intptr_t)lv_obj_get_user_data(row);
    open_modal_for_addr(addr);
}

static void scan_btn_cb(lv_event_t *e)
{
    (void)e;
    i2c_module_speed_t speed = (lv_dropdown_get_selected(s_speed_dropdown) == 0)
                                    ? I2C_MODULE_SPEED_100K : I2C_MODULE_SPEED_400K;
    if (i2c_module_init(speed) != ESP_OK) {
        lv_label_set_text(s_status_label, "Bus-Init fehlgeschlagen");
        return;
    }

    lv_label_set_text(s_status_label, "Scanne...");
    lv_obj_clean(s_list);

    static uint8_t found[120];
    size_t count = 0;
    i2c_module_scan(found, sizeof(found), &count);

    if (count == 0) {
        lv_label_set_text(s_status_label, "Keine Geraete gefunden");
        return;
    }
    lv_label_set_text_fmt(s_status_label, "%u Geraet(e) gefunden", (unsigned)count);

    for (size_t i = 0; i < count; i++) {
        lv_obj_t *row = lv_obj_create(s_list);
        theme_apply_card(row);
        lv_obj_set_size(row, LV_PCT(100), 56);
        lv_obj_set_user_data(row, (void *)(intptr_t)found[i]);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, device_row_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *addr_label = lv_label_create(row);
        lv_label_set_text_fmt(addr_label, "0x%02X", found[i]);
        lv_obj_set_style_text_color(addr_label, THEME_COLOR_ACCENT, 0);

        const char *guess = i2c_module_guess_device_name(found[i]);
        lv_obj_t *name_label = lv_label_create(row);
        lv_label_set_text(name_label, guess ? guess : "unbekannt");
        lv_obj_set_style_text_color(name_label, THEME_COLOR_TEXT_DIM, 0);
    }
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    i2c_module_deinit();
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
    lv_obj_set_size(box, 460, 360);
    lv_obj_center(box);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
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

    // --- Register-Stepper ---
    s_reg_label = lv_label_create(box);
    lv_obj_set_style_text_color(s_reg_label, THEME_COLOR_TEXT, 0);
    lv_obj_t *reg_row = lv_obj_create(box);
    lv_obj_remove_style_all(reg_row);
    lv_obj_set_flex_flow(reg_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(reg_row, 6, 0);
    struct { const char *l; int d; } reg_btns[] = { {"-16",-16}, {"-1",-1}, {"+1",1}, {"+16",16} };
    for (size_t i = 0; i < 4; i++) {
        lv_obj_t *b = lv_btn_create(reg_row);
        lv_obj_set_size(b, 60, 44);
        lv_obj_add_event_cb(b, reg_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)reg_btns[i].d);
        lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, reg_btns[i].l); lv_obj_center(l);
    }

    // --- Wert-Stepper ---
    s_val_label = lv_label_create(box);
    lv_obj_set_style_text_color(s_val_label, THEME_COLOR_TEXT, 0);
    lv_obj_t *val_row = lv_obj_create(box);
    lv_obj_remove_style_all(val_row);
    lv_obj_set_flex_flow(val_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(val_row, 6, 0);
    struct { const char *l; int d; } val_btns[] = { {"-16",-16}, {"-1",-1}, {"+1",1}, {"+16",16} };
    for (size_t i = 0; i < 4; i++) {
        lv_obj_t *b = lv_btn_create(val_row);
        lv_obj_set_size(b, 60, 44);
        lv_obj_add_event_cb(b, val_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)val_btns[i].d);
        lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, val_btns[i].l); lv_obj_center(l);
    }

    // --- Aktionen ---
    lv_obj_t *action_row = lv_obj_create(box);
    lv_obj_remove_style_all(action_row);
    lv_obj_set_flex_flow(action_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(action_row, 10, 0);
    lv_obj_t *read_btn = lv_btn_create(action_row);
    lv_obj_set_style_bg_color(read_btn, THEME_COLOR_ACCENT, 0);
    lv_obj_add_event_cb(read_btn, read_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *read_l = lv_label_create(read_btn); lv_label_set_text(read_l, "Read");
    lv_obj_t *write_btn = lv_btn_create(action_row);
    lv_obj_set_style_bg_color(write_btn, THEME_COLOR_WARNING, 0);
    lv_obj_add_event_cb(write_btn, write_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *write_l = lv_label_create(write_btn); lv_label_set_text(write_l, "Write");

    s_read_result_label = lv_label_create(box);
    lv_label_set_text(s_read_result_label, "--");
    lv_obj_set_style_text_color(s_read_result_label, THEME_COLOR_TEXT_DIM, 0);
}

static lv_obj_t *i2c_screen_create(void)
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
    lv_label_set_text(title, "I2C Scanner (Port A)");
    theme_apply_title(title);

    back_button_create(scr, back_cb);

    lv_obj_t *ctrl_row = lv_obj_create(scr);
    lv_obj_remove_style_all(ctrl_row);
    lv_obj_set_size(ctrl_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctrl_row, 10, 0);

    s_speed_dropdown = lv_dropdown_create(ctrl_row);
    lv_dropdown_set_options(s_speed_dropdown, "100 kHz\n400 kHz");
    lv_obj_t *scan_btn = lv_btn_create(ctrl_row);
    lv_obj_set_style_bg_color(scan_btn, THEME_COLOR_ACCENT, 0);
    lv_obj_add_event_cb(scan_btn, scan_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_l = lv_label_create(scan_btn);
    lv_label_set_text(scan_l, LV_SYMBOL_REFRESH " Scan");

    s_status_label = lv_label_create(scr);
    lv_label_set_text(s_status_label, "Bereit");
    lv_obj_set_style_text_color(s_status_label, THEME_COLOR_TEXT_DIM, 0);

    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);  // fuellt Restplatz statt 100% Elternhoehe (ueberlappte sonst Header/Zurueck-Button)
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 8, 0);

    build_modal(scr);
    return scr;
}

void i2c_module_ui_register(void)
{
    // Kein on_show noetig: der Bus wird erst beim ersten "Scan"-Tap
    // initialisiert, nicht automatisch beim Betreten des Screens - vermeidet
    // unnoetige Pin-Reservierung ohne Nutzeraktion.
    nav_register(NAV_SCREEN_I2C, i2c_screen_create);
}
