#include "bottom_nav.h"
#include "theme.h"

#define MAX_ITEMS 8

static lv_obj_t *s_bar = NULL;
static const bottom_nav_item_t *s_items = NULL;
static size_t s_item_count = 0;
static lv_obj_t *s_icon_labels[MAX_ITEMS];
static lv_obj_t *s_text_labels[MAX_ITEMS];
static bottom_nav_navigate_fn_t s_navigate_fn = NULL;

static void item_click_cb(lv_event_t *e)
{
    if (s_navigate_fn != NULL) {
        int screen_id = (int)(intptr_t)lv_event_get_user_data(e);
        s_navigate_fn(screen_id);
    }
}

lv_obj_t *bottom_nav_create(lv_obj_t *parent, const bottom_nav_item_t *items, size_t count)
{
    s_items = items;
    s_item_count = count < MAX_ITEMS ? count : MAX_ITEMS;

    s_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(s_bar);
    lv_obj_set_size(s_bar, LV_PCT(100), BOTTOM_NAV_HEIGHT);
    lv_obj_align(s_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_bar, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_bar, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_bar, 1, 0);
    lv_obj_set_style_border_side(s_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_flex_flow(s_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < s_item_count; i++) {
        lv_obj_t *item = lv_obj_create(s_bar);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, LV_SIZE_CONTENT, LV_PCT(100));
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(item, 2, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(item, item_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)s_items[i].screen_id);

        lv_obj_t *icon = lv_label_create(item);
        lv_label_set_text(icon, s_items[i].symbol);
        lv_obj_set_style_text_font(icon, THEME_FONT_TITLE, 0);
        s_icon_labels[i] = icon;

        lv_obj_t *text = lv_label_create(item);
        lv_label_set_text(text, s_items[i].label);
        lv_obj_set_style_text_font(text, THEME_FONT_BODY, 0);
        s_text_labels[i] = text;
    }

    return s_bar;
}

void bottom_nav_set_navigate_fn(bottom_nav_navigate_fn_t fn)
{
    s_navigate_fn = fn;
}

void bottom_nav_update(int screen_id)
{
    if (s_bar == NULL) {
        return;
    }

    bool is_top_level = false;
    for (size_t i = 0; i < s_item_count; i++) {
        if (s_items[i].screen_id == screen_id) {
            is_top_level = true;
            break;
        }
    }

    if (!is_top_level) {
        lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_HIDDEN);

    for (size_t i = 0; i < s_item_count; i++) {
        bool active = (s_items[i].screen_id == screen_id);
        lv_color_t color = active ? THEME_COLOR_ACCENT : THEME_COLOR_TEXT_DIM;
        lv_obj_set_style_text_color(s_icon_labels[i], color, 0);
        lv_obj_set_style_text_color(s_text_labels[i], color, 0);
    }
}
