#include "statusbar.h"
#include "theme.h"
#include <stdio.h>

#define STATUSBAR_HEIGHT 44

typedef struct {
    lv_obj_t *wifi;
    lv_obj_t *server;
    lv_obj_t *usb_target;
    lv_obj_t *battery;
    lv_obj_t *clock;
} statusbar_widgets_t;

static statusbar_widgets_t s_widgets;

static lv_obj_t *make_dot_item(lv_obj_t *bar, const char *symbol)
{
    lv_obj_t *label = lv_label_create(bar);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, THEME_COLOR_TEXT_DIM, 0);
    return label;
}

lv_obj_t *statusbar_create(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), STATUSBAR_HEIGHT);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bar, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_hor(bar, 16, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "CYBERDECK");
    lv_obj_set_style_text_color(title, THEME_COLOR_TEXT, 0);
    lv_obj_set_style_text_letter_space(title, 1, 0);

    // Rechte Gruppe: WiFi/Server/USB als kompakte Punkte + Akku + Uhr -
    // "nur die wichtigsten Informationen" (Nutzervorgabe). CPU/RAM/SD
    // bewusst nicht mehr hier (siehe System-Screen), das war zu dicht.
    lv_obj_t *right = lv_obj_create(bar);
    lv_obj_remove_style_all(right);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, 16, 0);

    s_widgets.wifi = make_dot_item(right, LV_SYMBOL_WIFI);
    s_widgets.server = make_dot_item(right, LV_SYMBOL_UPLOAD);
    s_widgets.usb_target = make_dot_item(right, LV_SYMBOL_USB " --");
    s_widgets.battery = make_dot_item(right, LV_SYMBOL_BATTERY_FULL " --");
    s_widgets.clock = make_dot_item(right, "--:--");
    lv_obj_set_style_text_color(s_widgets.clock, THEME_COLOR_TEXT, 0);

    return bar;
}

void statusbar_set_wifi(int rssi_dbm, bool connected)
{
    (void)rssi_dbm;
    lv_obj_set_style_text_color(s_widgets.wifi, connected ? THEME_COLOR_SUCCESS : THEME_COLOR_TEXT_DIM, 0);
}

void statusbar_set_server(bool connected)
{
    lv_obj_set_style_text_color(s_widgets.server, connected ? THEME_COLOR_SUCCESS : THEME_COLOR_TEXT_DIM, 0);
}

void statusbar_set_usb_target(const char *target_name)
{
    if (target_name == NULL) {
        lv_label_set_text(s_widgets.usb_target, LV_SYMBOL_USB " --");
        lv_obj_set_style_text_color(s_widgets.usb_target, THEME_COLOR_TEXT_DIM, 0);
    } else {
        lv_label_set_text_fmt(s_widgets.usb_target, LV_SYMBOL_USB " %s", target_name);
        lv_obj_set_style_text_color(s_widgets.usb_target, THEME_COLOR_SUCCESS, 0);
    }
}

// Zeigt die reale Akkuspannung, bewusst kein geschaetzter Prozentwert (siehe
// statusbar.h/system_module.h - die NP-F550-Entladekurve ist nicht
// kalibriert, ein erfundener Prozentwert waere irrefuehrend).
void statusbar_set_battery_voltage(float volts, bool valid)
{
    if (!valid) {
        lv_label_set_text_fmt(s_widgets.battery, "%s --", LV_SYMBOL_BATTERY_FULL);
        return;
    }
    int whole = (int)volts;
    int frac = (int)((volts - whole) * 100.0f + 0.5f);
    lv_label_set_text_fmt(s_widgets.battery, "%s %d.%02dV", LV_SYMBOL_BATTERY_FULL, whole, frac);
}

void statusbar_set_clock(const char *hh_mm)
{
    lv_label_set_text(s_widgets.clock, hh_mm);
}
