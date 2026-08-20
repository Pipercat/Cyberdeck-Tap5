#include "sensors_module_ui.h"
#include "sensors_module.h"
#include "nav.h"
#include "theme.h"
#include "screen_header.h"
#include "lvgl.h"
#include <stdio.h>

static lv_obj_t *s_rtc_label = NULL;
static lv_timer_t *s_poll_timer = NULL;

static void refresh_rtc(void)
{
    sensors_module_rtc_t rtc;
    esp_err_t err = sensors_module_read_rtc(&rtc);
    if (err == ESP_OK && rtc.ok) {
        lv_label_set_text_fmt(s_rtc_label, "%04d-%02d-%02d  %02d:%02d:%02d",
                               rtc.year, rtc.month, rtc.day, rtc.hour, rtc.minute, rtc.second);
        lv_obj_set_style_text_color(s_rtc_label, THEME_COLOR_TEXT, 0);
    } else if (err == ESP_OK && !rtc.ok) {
        // Chip antwortet (I2C ok), liefert aber ein ungueltiges Datum - RTC
        // wurde offenbar noch nie gestellt. Auf echter Hardware beobachtet.
        lv_label_set_text(s_rtc_label, "RTC nicht gestellt (Chip antwortet, Datum ungueltig)");
        lv_obj_set_style_text_color(s_rtc_label, THEME_COLOR_WARNING, 0);
    } else {
        lv_label_set_text(s_rtc_label, "RX8130 (0x32) nicht erreichbar");
        lv_obj_set_style_text_color(s_rtc_label, THEME_COLOR_DANGER, 0);
    }
}

static void poll_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_rtc();
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (s_poll_timer) {
        lv_timer_pause(s_poll_timer);
    }
    nav_show(NAV_SCREEN_DASHBOARD);
}

static lv_obj_t *sensors_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_style_pad_top(scr, THEME_SCREEN_PAD_TOP, 0);   // ueberschreibt pad_all nur oben
    lv_obj_set_style_pad_bottom(scr, 74, 0);  // Platz fuer den schwebenden Zurueck-Button
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);  // sonst verschiebt Scrollen den schwebenden Zurueck-Button
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 14, 0);

    screen_header_create(scr, "Sensors", back_cb);

    // --- RTC (RX8130CE) ---
    lv_obj_t *rtc_card = lv_obj_create(scr);
    theme_apply_card(rtc_card);
    lv_obj_set_size(rtc_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rtc_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(rtc_card, 6, 0);
    lv_obj_t *rtc_title = lv_label_create(rtc_card);
    lv_label_set_text(rtc_title, "RTC (RX8130CE)");
    lv_obj_set_style_text_color(rtc_title, THEME_COLOR_TEXT_DIM, 0);
    s_rtc_label = lv_label_create(rtc_card);
    lv_label_set_text(s_rtc_label, "...");

    // --- IMU: ehrlich als nicht implementiert gekennzeichnet, siehe
    // sensors_module.h fuer die Begruendung (BMI270-Firmware-Blob-Upload) ---
    lv_obj_t *imu_card = lv_obj_create(scr);
    theme_apply_card(imu_card);
    lv_obj_set_size(imu_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(imu_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(imu_card, 6, 0);
    lv_obj_t *imu_title = lv_label_create(imu_card);
    lv_label_set_text(imu_title, "IMU (BMI270)");
    lv_obj_set_style_text_color(imu_title, THEME_COLOR_TEXT_DIM, 0);
    lv_obj_t *imu_status = lv_label_create(imu_card);
    lv_label_set_text(imu_status, "Noch nicht implementiert (braucht Firmware-Upload)");
    lv_obj_set_style_text_color(imu_status, THEME_COLOR_WARNING, 0);

    return scr;
}

static void sensors_on_show(void)
{
    if (s_poll_timer == NULL) {
        s_poll_timer = lv_timer_create(poll_timer_cb, 1000, NULL);
    } else {
        lv_timer_resume(s_poll_timer);
    }
    refresh_rtc();
}

void sensors_module_ui_register(void)
{
    nav_register(NAV_SCREEN_SENSORS, sensors_screen_create);
    nav_register_on_show(NAV_SCREEN_SENSORS, sensors_on_show);
}
