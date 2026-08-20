#include "screen_header.h"
#include "theme.h"
#include "back_button.h"

lv_obj_t *screen_header_create(lv_obj_t *scr, const char *title, lv_event_cb_t back_cb)
{
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 40);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title_label = lv_label_create(header);
    lv_label_set_text(title_label, title);
    theme_apply_title(title_label);

    return back_button_create(scr, back_cb);
}
