#include "statusbar.h"
#include "theme.h"

#define STATUSBAR_HEIGHT 36

typedef struct {
    lv_obj_t *wifi;
    lv_obj_t *server;
    lv_obj_t *usb_target;
    lv_obj_t *sd;
    lv_obj_t *cpu;
    lv_obj_t *ram;
    lv_obj_t *battery;
    lv_obj_t *clock;
} statusbar_widgets_t;

static statusbar_widgets_t s_widgets;

static lv_obj_t *make_item(lv_obj_t *bar, const char *initial_text)
{
    lv_obj_t *label = lv_label_create(bar);
    lv_label_set_text(label, initial_text);
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
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 12, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    s_widgets.wifi        = make_item(bar, LV_SYMBOL_WIFI " --");
    s_widgets.server       = make_item(bar, LV_SYMBOL_UPLOAD " --");
    s_widgets.usb_target    = make_item(bar, LV_SYMBOL_USB " --");
    s_widgets.sd             = make_item(bar, LV_SYMBOL_SD_CARD " --");
    s_widgets.cpu              = make_item(bar, "CPU --%");
    s_widgets.ram                = make_item(bar, "RAM --%");
    s_widgets.battery              = make_item(bar, LV_SYMBOL_BATTERY_FULL " --");
    s_widgets.clock                  = make_item(bar, "--:--");

    return bar;
}

void statusbar_set_wifi(int rssi_dbm, bool connected)
{
    if (!connected) {
        lv_label_set_text(s_widgets.wifi, LV_SYMBOL_WIFI " --");
    } else {
        lv_label_set_text_fmt(s_widgets.wifi, LV_SYMBOL_WIFI " %d dBm", rssi_dbm);
    }
}

void statusbar_set_server(bool connected)
{
    lv_label_set_text(s_widgets.server, connected ? LV_SYMBOL_UPLOAD " OK" : LV_SYMBOL_UPLOAD " --");
}

void statusbar_set_usb_target(const char *target_name)
{
    if (target_name == NULL) {
        lv_label_set_text(s_widgets.usb_target, LV_SYMBOL_USB " --");
    } else {
        lv_label_set_text_fmt(s_widgets.usb_target, LV_SYMBOL_USB " %s", target_name);
    }
}

void statusbar_set_sd_present(bool present)
{
    lv_label_set_text(s_widgets.sd, present ? LV_SYMBOL_SD_CARD " OK" : LV_SYMBOL_SD_CARD " --");
}

void statusbar_set_cpu_percent(uint8_t percent)
{
    lv_label_set_text_fmt(s_widgets.cpu, "CPU %u%%", percent);
}

void statusbar_set_ram_percent(uint8_t percent)
{
    lv_label_set_text_fmt(s_widgets.ram, "RAM %u%%", percent);
}

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
