#include "module_card.h"
#include "theme.h"

lv_obj_t *module_card_create(lv_obj_t *parent, const char *symbol, const char *title,
                              const char *status_text, lv_event_cb_t on_click, void *user_data)
{
    lv_obj_t *card = lv_obj_create(parent);
    theme_apply_card(card);
    // Keine feste Groesse: der Elterncontainer (Grid-Layout in dashboard.c)
    // steuert Breite/Hoehe dynamisch per LV_GRID_ALIGN_STRETCH, damit keine
    // grossen Freiflaechen bei unterschiedlichen Aufloesungen entstehen.
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (symbol != NULL) {
        lv_obj_t *icon = lv_label_create(card);
        lv_label_set_text(icon, symbol);
        lv_obj_set_style_text_color(icon, THEME_COLOR_ACCENT, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
    }

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, THEME_COLOR_TEXT, 0);

    if (status_text != NULL) {
        lv_obj_t *status_label = lv_label_create(card);
        lv_label_set_text(status_label, status_text);
        lv_obj_set_style_text_color(status_label, THEME_COLOR_TEXT_DIM, 0);
    }

    if (on_click != NULL) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, on_click, LV_EVENT_CLICKED, user_data);
    } else {
        lv_obj_set_style_bg_opa(card, LV_OPA_60, 0);
    }

    return card;
}
