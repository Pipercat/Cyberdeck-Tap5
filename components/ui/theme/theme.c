#include "theme.h"

static lv_style_t s_style_screen;
static lv_style_t s_style_card;
static bool s_initialized = false;

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
