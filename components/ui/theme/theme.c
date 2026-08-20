#include "theme.h"

static lv_style_t s_style_screen;
static lv_style_t s_style_card;

// Ein Style pro Button-Variante (nur Grundfarbe) + zwei geteilte
// Feedback-Styles (Pressed/Disabled ueber Deckkraft, farbunabhaengig - so
// braucht es kein separates "abgedunkelt"-Style pro Variante).
static lv_style_t s_style_btn_variant[5];
static lv_style_t s_style_btn_pressed;
static lv_style_t s_style_btn_disabled;

static lv_style_t s_style_toggle_checked;

static bool s_initialized = false;

// lv_color_hex() ist in LVGL v9 keine Compile-Zeit-Konstante - THEME_COLOR_*
// laesst sich deshalb nicht als static-Array-Initialisierer verwenden,
// sondern muss zur Laufzeit zugewiesen werden (siehe theme_init()).
static lv_color_t k_btn_variant_color[5];

void theme_init(void)
{
    if (s_initialized) {
        return;
    }

    lv_style_init(&s_style_screen);
    lv_style_set_bg_color(&s_style_screen, THEME_COLOR_BG);
    lv_style_set_bg_opa(&s_style_screen, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_screen, THEME_COLOR_TEXT);
    lv_style_set_text_font(&s_style_screen, THEME_FONT_BODY);

    lv_style_init(&s_style_card);
    lv_style_set_bg_color(&s_style_card, THEME_COLOR_SURFACE);
    lv_style_set_bg_opa(&s_style_card, LV_OPA_COVER);
    lv_style_set_border_color(&s_style_card, THEME_COLOR_BORDER);
    lv_style_set_border_width(&s_style_card, 1);
    lv_style_set_radius(&s_style_card, THEME_RADIUS_CARD);
    lv_style_set_pad_all(&s_style_card, THEME_PAD_CARD);
    lv_style_set_text_color(&s_style_card, THEME_COLOR_TEXT);
    lv_style_set_text_font(&s_style_card, THEME_FONT_BODY);

    k_btn_variant_color[THEME_BTN_PRIMARY] = THEME_COLOR_ACCENT;
    k_btn_variant_color[THEME_BTN_SUCCESS] = THEME_COLOR_SUCCESS;
    k_btn_variant_color[THEME_BTN_WARNING] = THEME_COLOR_WARNING;
    k_btn_variant_color[THEME_BTN_DANGER]  = THEME_COLOR_DANGER;
    k_btn_variant_color[THEME_BTN_NEUTRAL] = THEME_COLOR_SURFACE_HI;
    for (int i = 0; i < 5; i++) {
        lv_style_init(&s_style_btn_variant[i]);
        lv_style_set_bg_color(&s_style_btn_variant[i], k_btn_variant_color[i]);
    }

    lv_style_init(&s_style_btn_pressed);
    lv_style_set_opa(&s_style_btn_pressed, LV_OPA_70);

    lv_style_init(&s_style_btn_disabled);
    lv_style_set_opa(&s_style_btn_disabled, LV_OPA_40);

    lv_style_init(&s_style_toggle_checked);
    lv_style_set_bg_color(&s_style_toggle_checked, THEME_COLOR_ACCENT);

    s_initialized = true;
}

void theme_apply_card(lv_obj_t *obj)
{
    lv_obj_add_style(obj, &s_style_card, 0);
}

void theme_apply_screen(lv_obj_t *obj)
{
    lv_obj_add_style(obj, &s_style_screen, 0);
}

void theme_apply_title(lv_obj_t *label)
{
    lv_obj_set_style_text_font(label, THEME_FONT_TITLE, 0);
    lv_obj_set_style_text_color(label, THEME_COLOR_TEXT, 0);
}

void theme_apply_button(lv_obj_t *btn, theme_button_variant_t variant)
{
    lv_obj_add_style(btn, &s_style_btn_variant[variant], 0);
    lv_obj_add_style(btn, &s_style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(btn, &s_style_btn_disabled, LV_STATE_DISABLED);
}

void theme_apply_toggle(lv_obj_t *btn, bool active)
{
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(btn, THEME_COLOR_SURFACE_HI, 0);
    lv_obj_add_style(btn, &s_style_toggle_checked, LV_STATE_CHECKED);
    lv_obj_add_style(btn, &s_style_btn_pressed, LV_STATE_PRESSED);
    theme_set_toggle_active(btn, active);
}

void theme_set_toggle_active(lv_obj_t *btn, bool active)
{
    if (active) {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(btn, LV_STATE_CHECKED);
    }
}

void theme_apply_status_chip(lv_obj_t *label, theme_status_t status)
{
    lv_color_t color;
    switch (status) {
        case THEME_STATUS_SUCCESS: color = THEME_COLOR_SUCCESS; break;
        case THEME_STATUS_WARNING: color = THEME_COLOR_WARNING; break;
        case THEME_STATUS_DANGER:  color = THEME_COLOR_DANGER;  break;
        case THEME_STATUS_INFO:    color = THEME_COLOR_ACCENT;  break;
        default:                   color = THEME_COLOR_TEXT_DIM; break;
    }
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_bg_color(label, color, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_20, 0);
    lv_obj_set_style_radius(label, THEME_RADIUS_CARD / 2, 0);
    lv_obj_set_style_pad_hor(label, 10, 0);
    lv_obj_set_style_pad_ver(label, 4, 0);
}
