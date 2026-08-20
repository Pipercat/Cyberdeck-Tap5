#include "back_button.h"
#include "theme.h"

lv_obj_t *back_button_create(lv_obj_t *parent, lv_event_cb_t on_click)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(btn, 160, THEME_TOUCH_TARGET_MIN);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    theme_apply_button(btn, THEME_BTN_PRIMARY);
    lv_obj_move_foreground(btn);
    lv_obj_add_event_cb(btn, on_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, LV_SYMBOL_LEFT " Dashboard");
    lv_obj_center(label);

    return btn;
}
