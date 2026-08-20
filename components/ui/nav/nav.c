#include "nav.h"
#include "theme.h"
#include "statusbar.h"
#include "back_button.h"
#include "bottom_nav.h"
#include "esp_log.h"

static const char *TAG = "nav";

static const char *k_screen_titles[NAV_SCREEN_COUNT] = {
    [NAV_SCREEN_DASHBOARD] = "Dashboard",
    [NAV_SCREEN_FLASH]     = "Flash",
    [NAV_SCREEN_GPIO]      = "GPIO",
    [NAV_SCREEN_SERIAL]    = "Serial",
    [NAV_SCREEN_I2C]       = "I2C Scanner",
    [NAV_SCREEN_SPI]       = "SPI Tools",
    [NAV_SCREEN_PWM]       = "PWM",
    [NAV_SCREEN_ADC]       = "ADC / Scope",
    [NAV_SCREEN_SENSORS]   = "Sensors",
    [NAV_SCREEN_CAMERA]    = "Camera",
    [NAV_SCREEN_AUDIO]     = "Audio",
    [NAV_SCREEN_NETWORK]   = "Network",
    [NAV_SCREEN_FILES]     = "Files",
    [NAV_SCREEN_PROJECTS]  = "Projects",
    [NAV_SCREEN_SYSTEM]    = "System",
    [NAV_SCREEN_SETTINGS]  = "Settings",
    [NAV_SCREEN_BUS]       = "Bus",
    [NAV_SCREEN_SCOPE]     = "Scope",
    [NAV_SCREEN_MEDIA]     = "Media",
};

// Die 5 Bottom-Nav-Ziele (siehe bottom_nav.h fuer die Begruendung, warum
// hier int statt nav_screen_id_t verwendet wird - Vermeidung einer
// zirkulaeren Komponentenabhaengigkeit).
static const bottom_nav_item_t k_bottom_nav_items[] = {
    { LV_SYMBOL_HOME,      "HOME",     (int)NAV_SCREEN_DASHBOARD },
    { LV_SYMBOL_DIRECTORY, "FILES",    (int)NAV_SCREEN_FILES },
    { LV_SYMBOL_COPY,      "PROJECTS", (int)NAV_SCREEN_PROJECTS },
    { LV_SYMBOL_LIST,      "SYSTEM",   (int)NAV_SCREEN_SYSTEM },
    { LV_SYMBOL_SETTINGS,  "SETTINGS", (int)NAV_SCREEN_SETTINGS },
};
#define BOTTOM_NAV_ITEM_COUNT (sizeof(k_bottom_nav_items) / sizeof(k_bottom_nav_items[0]))

static nav_screen_create_fn_t s_create_fns[NAV_SCREEN_COUNT];
static nav_screen_show_fn_t s_on_show_fns[NAV_SCREEN_COUNT];
static lv_obj_t *s_screens[NAV_SCREEN_COUNT];
static nav_screen_id_t s_current = NAV_SCREEN_DASHBOARD;

static void back_button_cb(lv_event_t *e)
{
    (void)e;
    nav_show(NAV_SCREEN_DASHBOARD);
}

static void bottom_nav_navigate_wrapper(int screen_id)
{
    nav_show((nav_screen_id_t)screen_id);
}

// true fuer die 5 Screens, die ueber die Bottom-Navigation statt einen
// schwebenden Zurueck-Button erreicht werden (siehe bottom_nav.h) - ein
// Screen bekommt nie beide Navigationsmechanismen gleichzeitig.
static bool is_bottom_nav_target(nav_screen_id_t id)
{
    for (size_t i = 0; i < BOTTOM_NAV_ITEM_COUNT; i++) {
        if (k_bottom_nav_items[i].screen_id == (int)id) {
            return true;
        }
    }
    return false;
}

static lv_obj_t *create_placeholder_screen_for(nav_screen_id_t id)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(scr, THEME_SCREEN_PAD_TOP, 0); // Platz fuer Statusleiste

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text_fmt(title, "%s", k_screen_titles[id] ? k_screen_titles[id] : "?");
    theme_apply_title(title);

    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "Coming Soon - spaetere Phase");
    lv_obj_set_style_text_color(subtitle, THEME_COLOR_TEXT_DIM, 0);

    if (is_bottom_nav_target(id)) {
        lv_obj_set_style_pad_bottom(scr, BOTTOM_NAV_HEIGHT + 10, 0);
    } else {
        lv_obj_set_style_pad_bottom(scr, 74, 0);
        back_button_create(scr, back_button_cb);
    }

    return scr;
}

void nav_init(void)
{
    theme_init();
    statusbar_create(lv_layer_top());
    bottom_nav_create(lv_layer_top(), k_bottom_nav_items, BOTTOM_NAV_ITEM_COUNT);
    bottom_nav_set_navigate_fn(bottom_nav_navigate_wrapper);
    ESP_LOGI(TAG, "Navigation initialisiert");
}

void nav_register(nav_screen_id_t id, nav_screen_create_fn_t create_fn)
{
    if (id >= NAV_SCREEN_COUNT) {
        return;
    }
    s_create_fns[id] = create_fn;
}

void nav_register_on_show(nav_screen_id_t id, nav_screen_show_fn_t on_show_fn)
{
    if (id >= NAV_SCREEN_COUNT) {
        return;
    }
    s_on_show_fns[id] = on_show_fn;
}

void nav_show(nav_screen_id_t id)
{
    if (id >= NAV_SCREEN_COUNT) {
        ESP_LOGE(TAG, "nav_show: ungueltige Screen-ID %d", id);
        return;
    }

    if (s_screens[id] == NULL) {
        if (s_create_fns[id] != NULL) {
            s_screens[id] = s_create_fns[id]();
        } else {
            s_screens[id] = create_placeholder_screen_for(id);
        }
    }
    if (s_on_show_fns[id] != NULL) {
        s_on_show_fns[id]();
    }

    s_current = id;
    bottom_nav_update((int)id);
    lv_screen_load_anim(s_screens[id], LV_SCR_LOAD_ANIM_FADE_IN, 150, 0, false);
}
