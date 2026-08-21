#include "flash_ui.h"
#include "flash_manager.h"
#include "usb_device_manager.h"
#include "nav.h"
#include "theme.h"
#include "screen_header.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_timer.h"

static lv_obj_t *s_target_status;
static lv_obj_t *s_target_detail;
static lv_obj_t *s_source_status;
static lv_obj_t *s_state_label;
static lv_obj_t *s_bar;
static lv_obj_t *s_bar_label;
static lv_obj_t *s_file_label;
static lv_obj_t *s_address_label;
static lv_obj_t *s_transferred_label;
static lv_obj_t *s_speed_label;
static lv_obj_t *s_cancel_btn;
static lv_timer_t *s_poll_timer;

static bool state_is_active(flash_state_t s)
{
    return s != FLASH_STATE_IDLE && s != FLASH_STATE_SUCCESS &&
           s != FLASH_STATE_ERROR && s != FLASH_STATE_CANCELLED;
}

static void cancel_btn_cb(lv_event_t *e)
{
    (void)e;
    flash_manager_cancel();
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (s_poll_timer) lv_timer_pause(s_poll_timer);
    nav_show(NAV_SCREEN_DASHBOARD);
}

static void poll_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    usb_device_manager_target_t target;
    usb_device_manager_get_target(&target);
    if (target.connected) {
        lv_label_set_text_fmt(s_target_status, "%s", target.product);
        theme_apply_status_chip(s_target_status, target.serial_supported ? THEME_STATUS_SUCCESS : THEME_STATUS_WARNING);
        lv_label_set_text_fmt(s_target_detail, "%s\nVID:PID %04X:%04X\n%s",
                               target.bridge_label ? target.bridge_label : "?",
                               target.vid, target.pid,
                               target.serial_supported ? "Flash-faehig" : "Nicht unterstuetzt (siehe docs/usb_host.md)");
    } else {
        lv_label_set_text(s_target_status, "No target");
        theme_apply_status_chip(s_target_status, THEME_STATUS_NEUTRAL);
        lv_label_set_text(s_target_detail, "USB-Geraet anschliessen");
    }

    flash_status_t status;
    flash_manager_get_status(&status);
    lv_label_set_text(s_state_label, flash_state_str(status.state));

    theme_status_t chip_status = THEME_STATUS_NEUTRAL;
    if (status.state == FLASH_STATE_SUCCESS) chip_status = THEME_STATUS_SUCCESS;
    else if (status.state == FLASH_STATE_ERROR) chip_status = THEME_STATUS_DANGER;
    else if (state_is_active(status.state)) chip_status = THEME_STATUS_INFO;
    theme_apply_status_chip(s_state_label, chip_status);

    bool active = state_is_active(status.state);
    if (active) {
        lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_cancel_btn, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_bar, status.progress_percent, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_bar_label, "%d %%", status.progress_percent);
        lv_label_set_text_fmt(s_file_label, "Writing %s", status.current_file);
        lv_label_set_text_fmt(s_address_label, "Address 0x%08" PRIX32, status.current_address);

        char written_buf[16], total_buf[16];
        snprintf(written_buf, sizeof(written_buf), "%.0f kB", status.bytes_written_total / 1024.0);
        snprintf(total_buf, sizeof(total_buf), "%.2f MB", status.bytes_total_all / (1024.0 * 1024.0));
        lv_label_set_text_fmt(s_transferred_label, "%s / %s", written_buf, total_buf);

        double elapsed_s = (esp_timer_get_time() - status.started_at_us) / 1000000.0;
        double speed_kb_s = (elapsed_s > 0.1) ? (status.bytes_written_total / 1024.0) / elapsed_s : 0.0;
        lv_label_set_text_fmt(s_speed_label, "%.0f kB/s  ·  %.1fs", speed_kb_s, elapsed_s);
        lv_label_set_text(s_source_status, "Remote upload · in progress");
    } else {
        lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_cancel_btn, LV_OBJ_FLAG_HIDDEN);
        if (status.state == FLASH_STATE_ERROR) {
            lv_label_set_text_fmt(s_source_status, "Error: %s", flash_error_str(status.last_error));
        } else if (status.state == FLASH_STATE_SUCCESS) {
            lv_label_set_text(s_source_status, "Last flash: success");
        } else {
            lv_label_set_text(s_source_status, "Waiting for remote client...");
        }
        lv_label_set_text(s_file_label, "");
        lv_label_set_text(s_address_label, "");
        lv_label_set_text(s_transferred_label, "");
        lv_label_set_text(s_speed_label, "");
    }
}

static lv_obj_t *flash_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_style_pad_top(scr, THEME_SCREEN_PAD_TOP, 0);
    lv_obj_set_style_pad_bottom(scr, 74, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 12, 0);

    screen_header_create(scr, "Flash", back_cb);

    // --- Target-Karte ---
    lv_obj_t *target_card = lv_obj_create(scr);
    theme_apply_card(target_card);
    lv_obj_set_size(target_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(target_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(target_card, THEME_PAD_CARD, 0);
    lv_obj_set_style_pad_row(target_card, 4, 0);

    lv_obj_t *target_title = lv_label_create(target_card);
    lv_label_set_text(target_title, "TARGET");
    lv_obj_set_style_text_color(target_title, THEME_COLOR_TEXT_DIM, 0);

    s_target_status = lv_label_create(target_card);
    lv_label_set_text(s_target_status, "No target");

    s_target_detail = lv_label_create(target_card);
    lv_obj_set_style_text_color(s_target_detail, THEME_COLOR_TEXT_DIM, 0);

    // --- Source-Karte ---
    lv_obj_t *source_card = lv_obj_create(scr);
    theme_apply_card(source_card);
    lv_obj_set_size(source_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(source_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(source_card, THEME_PAD_CARD, 0);
    lv_obj_set_style_pad_row(source_card, 4, 0);

    lv_obj_t *source_title = lv_label_create(source_card);
    lv_label_set_text(source_title, "SOURCE");
    lv_obj_set_style_text_color(source_title, THEME_COLOR_TEXT_DIM, 0);
    s_source_status = lv_label_create(source_card);
    lv_label_set_text(s_source_status, "Waiting for remote client...");

    // --- Fortschritts-Karte ---
    lv_obj_t *progress_card = lv_obj_create(scr);
    theme_apply_card(progress_card);
    lv_obj_set_size(progress_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(progress_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(progress_card, THEME_PAD_CARD, 0);
    lv_obj_set_style_pad_row(progress_card, 6, 0);

    lv_obj_t *state_row = lv_obj_create(progress_card);
    lv_obj_remove_style_all(state_row);
    lv_obj_set_size(state_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *state_title = lv_label_create(state_row);
    lv_label_set_text(state_title, "STATUS");
    lv_obj_set_style_text_color(state_title, THEME_COLOR_TEXT_DIM, 0);
    s_state_label = lv_label_create(state_row);
    theme_apply_status_chip(s_state_label, THEME_STATUS_NEUTRAL);
    lv_label_set_text(s_state_label, "IDLE");

    s_file_label = lv_label_create(progress_card);
    s_address_label = lv_label_create(progress_card);
    lv_obj_set_style_text_color(s_address_label, THEME_COLOR_TEXT_DIM, 0);

    s_bar = lv_bar_create(progress_card);
    lv_obj_set_size(s_bar, LV_PCT(100), 14);
    lv_bar_set_range(s_bar, 0, 100);
    lv_obj_set_style_bg_color(s_bar, THEME_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);

    s_bar_label = lv_label_create(progress_card);
    theme_apply_title(s_bar_label);
    lv_label_set_text(s_bar_label, "");

    s_transferred_label = lv_label_create(progress_card);
    s_speed_label = lv_label_create(progress_card);
    lv_obj_set_style_text_color(s_speed_label, THEME_COLOR_TEXT_DIM, 0);

    s_cancel_btn = lv_btn_create(progress_card);
    theme_apply_button(s_cancel_btn, THEME_BTN_DANGER);
    lv_obj_add_event_cb(s_cancel_btn, cancel_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_l = lv_label_create(s_cancel_btn);
    lv_label_set_text(cancel_l, LV_SYMBOL_CLOSE " Cancel");
    lv_obj_add_flag(s_cancel_btn, LV_OBJ_FLAG_HIDDEN);

    return scr;
}

static void flash_on_show(void)
{
    flash_manager_init();  // idempotent, lazy USB-Host-Bringup (siehe usb_host_manager.h)
    if (s_poll_timer == NULL) {
        s_poll_timer = lv_timer_create(poll_timer_cb, 300, NULL);
    } else {
        lv_timer_resume(s_poll_timer);
    }
    poll_timer_cb(NULL);
}

void flash_module_ui_register(void)
{
    nav_register(NAV_SCREEN_FLASH, flash_screen_create);
    nav_register_on_show(NAV_SCREEN_FLASH, flash_on_show);
}
