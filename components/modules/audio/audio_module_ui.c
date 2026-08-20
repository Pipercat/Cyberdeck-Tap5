#include "audio_module_ui.h"
#include "audio_module.h"
#include "nav.h"
#include "theme.h"
#include "back_button.h"
#include "lvgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum { AUDIO_UI_MODE_MIC, AUDIO_UI_MODE_SPEAKER } audio_ui_mode_t;

static audio_ui_mode_t s_mode = AUDIO_UI_MODE_MIC;

static lv_obj_t *s_mic_mode_btn = NULL;
static lv_obj_t *s_spk_mode_btn = NULL;
static lv_obj_t *s_mic_panel = NULL;
static lv_obj_t *s_speaker_panel = NULL;
static lv_obj_t *s_level_bar = NULL;
static lv_obj_t *s_level_label = NULL;
static lv_obj_t *s_peak_label = NULL;
static lv_obj_t *s_mic_toggle_btn = NULL;
static lv_obj_t *s_mic_toggle_label = NULL;
static lv_timer_t *s_mic_refresh_timer = NULL;

static lv_obj_t *s_freq_slider = NULL;
static lv_obj_t *s_freq_label = NULL;
static lv_obj_t *s_volume_slider = NULL;
static lv_obj_t *s_play_btn = NULL;
static lv_obj_t *s_play_label = NULL;
static bool s_playing = false;

static float linear_to_db(float linear)
{
    if (linear < 0.0001f) return -80.0f;
    return 20.0f * log10f(linear);
}

static void mic_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    float rms, peak;
    audio_module_get_mic_level(&rms, &peak);

    int32_t bar_val = (int32_t)(rms * 100.0f);
    if (bar_val > 100) bar_val = 100;
    lv_bar_set_value(s_level_bar, bar_val, LV_ANIM_ON);

    lv_label_set_text_fmt(s_level_label, "Pegel: %d.%01d dB (relativ, unkalibriert)",
                           (int)linear_to_db(rms), abs((int)(linear_to_db(rms) * 10) % 10));
    lv_label_set_text_fmt(s_peak_label, "Peak: %d.%01d dB",
                           (int)linear_to_db(peak), abs((int)(linear_to_db(peak) * 10) % 10));
}

static void mic_toggle_cb(lv_event_t *e)
{
    (void)e;
    if (audio_module_init() != ESP_OK) return;

    static bool running = false;
    running = !running;
    if (running) {
        audio_module_start_mic();
        lv_label_set_text(s_mic_toggle_label, LV_SYMBOL_STOP " Stop");
        theme_set_button_variant(s_mic_toggle_btn, THEME_BTN_DANGER);
        if (s_mic_refresh_timer == NULL) {
            s_mic_refresh_timer = lv_timer_create(mic_refresh_timer_cb, 150, NULL);
        } else {
            lv_timer_resume(s_mic_refresh_timer);
        }
    } else {
        audio_module_stop_mic();
        lv_label_set_text(s_mic_toggle_label, LV_SYMBOL_PLAY " Start");
        theme_set_button_variant(s_mic_toggle_btn, THEME_BTN_SUCCESS);
        if (s_mic_refresh_timer) lv_timer_pause(s_mic_refresh_timer);
        lv_bar_set_value(s_level_bar, 0, LV_ANIM_OFF);
    }
}

static void mode_btn_cb(lv_event_t *e)
{
    audio_ui_mode_t mode = (audio_ui_mode_t)(intptr_t)lv_event_get_user_data(e);
    s_mode = mode;
    if (mode == AUDIO_UI_MODE_MIC) {
        lv_obj_clear_flag(s_mic_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_speaker_panel, LV_OBJ_FLAG_HIDDEN);
        audio_module_stop_playback();
        s_playing = false;
        lv_label_set_text(s_play_label, LV_SYMBOL_PLAY " Play");
        theme_set_button_variant(s_play_btn, THEME_BTN_SUCCESS);
    } else {
        lv_obj_add_flag(s_mic_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_speaker_panel, LV_OBJ_FLAG_HIDDEN);
        audio_module_stop_mic();
        if (s_mic_refresh_timer) lv_timer_pause(s_mic_refresh_timer);
    }
    theme_set_toggle_active(s_mic_mode_btn, mode == AUDIO_UI_MODE_MIC);
    theme_set_toggle_active(s_spk_mode_btn, mode == AUDIO_UI_MODE_SPEAKER);
}

static void freq_slider_cb(lv_event_t *e)
{
    (void)e;
    int32_t hz = lv_slider_get_value(s_freq_slider);
    lv_label_set_text_fmt(s_freq_label, "%d Hz", (int)hz);
    if (s_playing) {
        audio_module_start_tone((float)hz, (uint8_t)lv_slider_get_value(s_volume_slider));
    }
}

static void volume_slider_cb(lv_event_t *e)
{
    (void)e;
    if (s_playing) {
        int32_t hz = lv_slider_get_value(s_freq_slider);
        audio_module_start_tone((float)hz, (uint8_t)lv_slider_get_value(s_volume_slider));
    }
}

static void play_btn_cb(lv_event_t *e)
{
    (void)e;
    if (audio_module_init() != ESP_OK) return;

    if (s_playing) {
        audio_module_stop_playback();
        s_playing = false;
        lv_label_set_text(s_play_label, LV_SYMBOL_PLAY " Play");
        theme_set_button_variant(s_play_btn, THEME_BTN_SUCCESS);
        return;
    }
    int32_t hz = lv_slider_get_value(s_freq_slider);
    uint8_t vol = (uint8_t)lv_slider_get_value(s_volume_slider);
    audio_module_start_tone((float)hz, vol);
    s_playing = true;
    lv_label_set_text(s_play_label, LV_SYMBOL_STOP " Stop");
    theme_set_button_variant(s_play_btn, THEME_BTN_DANGER);
}

static void sweep_btn_cb(lv_event_t *e)
{
    (void)e;
    if (audio_module_init() != ESP_OK) return;
    uint8_t vol = (uint8_t)lv_slider_get_value(s_volume_slider);
    audio_module_start_sweep(100.0f, 10000.0f, vol, 4000);
    s_playing = true;
    lv_label_set_text(s_play_label, LV_SYMBOL_STOP " Stop");
    theme_set_button_variant(s_play_btn, THEME_BTN_DANGER);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    audio_module_stop_playback();
    audio_module_stop_mic();
    if (s_mic_refresh_timer) lv_timer_pause(s_mic_refresh_timer);
    s_playing = false;
    nav_show(NAV_SCREEN_DASHBOARD);
}

static lv_obj_t *audio_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_style_pad_top(scr, THEME_SCREEN_PAD_TOP, 0);   // ueberschreibt pad_all nur oben
    lv_obj_set_style_pad_bottom(scr, 74, 0);  // Platz fuer den schwebenden Zurueck-Button
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);  // sonst verschiebt Scrollen den schwebenden Zurueck-Button
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 12, 0);

    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 40);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Audio");
    theme_apply_title(title);

    back_button_create(scr, back_cb);

    lv_obj_t *mode_row = lv_obj_create(scr);
    lv_obj_remove_style_all(mode_row);
    lv_obj_set_size(mode_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(mode_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(mode_row, 10, 0);
    s_mic_mode_btn = lv_btn_create(mode_row);
    theme_apply_toggle(s_mic_mode_btn, true);
    lv_obj_add_event_cb(s_mic_mode_btn, mode_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)AUDIO_UI_MODE_MIC);
    lv_obj_t *mm_l = lv_label_create(s_mic_mode_btn); lv_label_set_text(mm_l, LV_SYMBOL_AUDIO " Mikrofon");
    s_spk_mode_btn = lv_btn_create(mode_row);
    theme_apply_toggle(s_spk_mode_btn, false);
    lv_obj_add_event_cb(s_spk_mode_btn, mode_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)AUDIO_UI_MODE_SPEAKER);
    lv_obj_t *sm_l = lv_label_create(s_spk_mode_btn); lv_label_set_text(sm_l, LV_SYMBOL_VOLUME_MAX " Lautsprecher");

    // --- Mikrofon-Panel ---
    s_mic_panel = lv_obj_create(scr);
    theme_apply_card(s_mic_panel);
    lv_obj_set_size(s_mic_panel, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_mic_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_mic_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_mic_panel, 10, 0);

    s_level_bar = lv_bar_create(s_mic_panel);
    lv_obj_set_size(s_level_bar, LV_PCT(90), 28);
    lv_bar_set_range(s_level_bar, 0, 100);
    lv_obj_set_style_bg_color(s_level_bar, THEME_COLOR_ACCENT, LV_PART_INDICATOR);

    s_level_label = lv_label_create(s_mic_panel);
    lv_label_set_text(s_level_label, "Pegel: --");
    lv_obj_set_style_text_color(s_level_label, THEME_COLOR_TEXT, 0);
    s_peak_label = lv_label_create(s_mic_panel);
    lv_label_set_text(s_peak_label, "Peak: --");
    lv_obj_set_style_text_color(s_peak_label, THEME_COLOR_TEXT_DIM, 0);

    s_mic_toggle_btn = lv_btn_create(s_mic_panel);
    theme_apply_button(s_mic_toggle_btn, THEME_BTN_SUCCESS);
    lv_obj_add_event_cb(s_mic_toggle_btn, mic_toggle_cb, LV_EVENT_CLICKED, NULL);
    s_mic_toggle_label = lv_label_create(s_mic_toggle_btn);
    lv_label_set_text(s_mic_toggle_label, LV_SYMBOL_PLAY " Start");

    lv_obj_t *mic_note = lv_label_create(s_mic_panel);
    lv_label_set_text(mic_note, "Relative Pegelmessung - kein kalibriertes dB-SPL-Messgeraet");
    lv_obj_set_style_text_color(mic_note, THEME_COLOR_WARNING, 0);

    // --- Lautsprecher-Panel ---
    s_speaker_panel = lv_obj_create(scr);
    theme_apply_card(s_speaker_panel);
    lv_obj_set_size(s_speaker_panel, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_speaker_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_speaker_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_speaker_panel, 10, 0);
    lv_obj_add_flag(s_speaker_panel, LV_OBJ_FLAG_HIDDEN);

    s_freq_label = lv_label_create(s_speaker_panel);
    lv_label_set_text(s_freq_label, "440 Hz");
    lv_obj_set_style_text_color(s_freq_label, THEME_COLOR_ACCENT, 0);
    s_freq_slider = lv_slider_create(s_speaker_panel);
    lv_obj_set_width(s_freq_slider, 400);
    lv_slider_set_range(s_freq_slider, 20, 15000);
    lv_slider_set_value(s_freq_slider, 440, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_freq_slider, freq_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *vol_label = lv_label_create(s_speaker_panel);
    lv_label_set_text(vol_label, "Lautstaerke:");
    lv_obj_set_style_text_color(vol_label, THEME_COLOR_TEXT_DIM, 0);
    s_volume_slider = lv_slider_create(s_speaker_panel);
    lv_obj_set_width(s_volume_slider, 400);
    lv_slider_set_range(s_volume_slider, 0, 100);
    lv_slider_set_value(s_volume_slider, 50, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_volume_slider, volume_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *btn_row = lv_obj_create(s_speaker_panel);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btn_row, 10, 0);
    s_play_btn = lv_btn_create(btn_row);
    theme_apply_button(s_play_btn, THEME_BTN_SUCCESS);
    lv_obj_add_event_cb(s_play_btn, play_btn_cb, LV_EVENT_CLICKED, NULL);
    s_play_label = lv_label_create(s_play_btn);
    lv_label_set_text(s_play_label, LV_SYMBOL_PLAY " Play");
    lv_obj_t *sweep_btn = lv_btn_create(btn_row);
    theme_apply_button(sweep_btn, THEME_BTN_NEUTRAL);
    lv_obj_add_event_cb(sweep_btn, sweep_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sweep_l = lv_label_create(sweep_btn);
    lv_label_set_text(sweep_l, LV_SYMBOL_LOOP " Sweep 100Hz-10kHz");

    return scr;
}

static void audio_on_show(void)
{
    audio_module_init();
}

void audio_module_ui_register(void)
{
    nav_register(NAV_SCREEN_AUDIO, audio_screen_create);
    nav_register_on_show(NAV_SCREEN_AUDIO, audio_on_show);
}
