#include "dashboard.h"
#include "nav.h"
#include "theme.h"
#include "module_card.h"
#include "screen_header.h"
#include "bottom_nav.h"
#include "lvgl.h"

typedef struct {
    const char *symbol;
    const char *title;
    const char *description;
    nav_screen_id_t target;
    bool implemented;
} module_entry_t;

// HOME: max. 6 Kacheln (Nutzervorgabe 2026-08-20 - "EIN TOOL = EIN SCREEN",
// keine 15 kleinen Kacheln mehr). Drei davon sind Kategorien, die je zwei
// verwandte Werkzeuge buendeln (siehe NAV_SCREEN_BUS/SCOPE/MEDIA), damit
// trotzdem alle bestehenden Module erreichbar bleiben.
static const module_entry_t k_home_tiles[] = {
    { LV_SYMBOL_DOWNLOAD,   "FLASH",  "Firmware & Devices", NAV_SCREEN_FLASH,  false },
    { LV_SYMBOL_SHUFFLE,    "GPIO",   "Pin Control",        NAV_SCREEN_GPIO,   true  },
    { LV_SYMBOL_KEYBOARD,   "SERIAL", "Terminal",           NAV_SCREEN_SERIAL, true  },
    { LV_SYMBOL_LIST,       "BUS",    "I2C / SPI",          NAV_SCREEN_BUS,    true  },
    { LV_SYMBOL_IMAGE,      "SCOPE",  "ADC / PWM",          NAV_SCREEN_SCOPE,  true  },
    { LV_SYMBOL_VOLUME_MAX, "MEDIA",  "Camera / Audio",     NAV_SCREEN_MEDIA,  true  },
};
#define HOME_TILE_COUNT (sizeof(k_home_tiles) / sizeof(k_home_tiles[0]))

static const module_entry_t k_bus_options[] = {
    { LV_SYMBOL_LIST, "I2C Scanner", "Port A", NAV_SCREEN_I2C, true  },
    { LV_SYMBOL_LIST, "SPI Tools",   NULL,      NAV_SCREEN_SPI, false },
};
static const module_entry_t k_scope_options[] = {
    { LV_SYMBOL_IMAGE, "ADC / Scope", "Mini-Oszilloskop",     NAV_SCREEN_ADC, true },
    { LV_SYMBOL_LOOP,  "PWM / Servo", "Pulsbreiten-Steuerung", NAV_SCREEN_PWM, true },
};
static const module_entry_t k_media_options[] = {
    { LV_SYMBOL_VOLUME_MAX, "Audio",  "Mic / Speaker Test", NAV_SCREEN_AUDIO,  true  },
    { LV_SYMBOL_VIDEO,      "Camera", NULL,                  NAV_SCREEN_CAMERA, false },
};

#define CARD_HEIGHT 130

static void module_card_click_cb(lv_event_t *e)
{
    nav_screen_id_t target = (nav_screen_id_t)(intptr_t)lv_event_get_user_data(e);
    nav_show(target);
}

static void add_tiles(lv_obj_t *parent, const module_entry_t *tiles, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        const module_entry_t *t = &tiles[i];
        const char *status = t->implemented ? t->description : "Coming Soon";
        lv_event_cb_t on_click = t->implemented ? module_card_click_cb : NULL;
        lv_obj_t *card = module_card_create(parent, t->symbol, t->title, status,
                                             on_click, (void *)(intptr_t)t->target);
        lv_obj_set_size(card, LV_PCT(47), CARD_HEIGHT);
    }
}

static lv_obj_t *dashboard_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_set_style_pad_all(scr, 20, 0);
    lv_obj_set_style_pad_top(scr, THEME_SCREEN_PAD_TOP, 0);
    lv_obj_set_style_pad_bottom(scr, BOTTOM_NAV_HEIGHT + 16, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 16, 0);

    // --- Kopf: "EMBEDDED ENGINEERING" / "Ready." - bewusst kein erfundener
    // Target-Status (z.B. "ESP32-S3 ●"), solange es keine echte
    // Geraeteerkennung gibt (Flash-Modul existiert noch nicht). ---
    lv_obj_t *intro = lv_obj_create(scr);
    lv_obj_remove_style_all(intro);
    lv_obj_set_size(intro, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(intro, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(intro, 2, 0);

    lv_obj_t *eyebrow = lv_label_create(intro);
    lv_label_set_text(eyebrow, "EMBEDDED ENGINEERING");
    lv_obj_set_style_text_color(eyebrow, THEME_COLOR_ACCENT, 0);
    lv_obj_set_style_text_letter_space(eyebrow, 1, 0);

    lv_obj_t *ready = lv_label_create(intro);
    lv_label_set_text(ready, "Ready.");
    theme_apply_title(ready);

    // --- 6 Kacheln, 2 Spalten (portrait-tauglich) ---
    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, 16, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    add_tiles(grid, k_home_tiles, HOME_TILE_COUNT);

    return scr;
}

// --- Kategorie-Screens: je 2 grosse Optionskarten + Zurueck-Button (kein
// Bottom-Nav-Ziel, siehe nav.c is_bottom_nav_target()). ---
static void bus_back_cb(lv_event_t *e) { (void)e; nav_show(NAV_SCREEN_DASHBOARD); }
static void scope_back_cb(lv_event_t *e) { (void)e; nav_show(NAV_SCREEN_DASHBOARD); }
static void media_back_cb(lv_event_t *e) { (void)e; nav_show(NAV_SCREEN_DASHBOARD); }

static lv_obj_t *create_category_screen(const char *title, const module_entry_t *options,
                                         size_t count, lv_event_cb_t back_cb)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_set_style_pad_all(scr, 20, 0);
    lv_obj_set_style_pad_top(scr, THEME_SCREEN_PAD_TOP, 0);
    lv_obj_set_style_pad_bottom(scr, 74, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 16, 0);

    screen_header_create(scr, title, back_cb);

    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 16, 0);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < count; i++) {
        const module_entry_t *t = &options[i];
        const char *status = t->implemented ? t->description : "Coming Soon";
        lv_event_cb_t on_click = t->implemented ? module_card_click_cb : NULL;
        lv_obj_t *card = module_card_create(list, t->symbol, t->title, status,
                                             on_click, (void *)(intptr_t)t->target);
        lv_obj_set_size(card, LV_PCT(100), CARD_HEIGHT);
    }

    return scr;
}

static lv_obj_t *bus_screen_create(void)
{
    return create_category_screen("Bus", k_bus_options,
                                   sizeof(k_bus_options) / sizeof(k_bus_options[0]), bus_back_cb);
}

static lv_obj_t *scope_screen_create(void)
{
    return create_category_screen("Scope", k_scope_options,
                                   sizeof(k_scope_options) / sizeof(k_scope_options[0]), scope_back_cb);
}

static lv_obj_t *media_screen_create(void)
{
    return create_category_screen("Media", k_media_options,
                                   sizeof(k_media_options) / sizeof(k_media_options[0]), media_back_cb);
}

void dashboard_register(void)
{
    nav_register(NAV_SCREEN_DASHBOARD, dashboard_create);
    nav_register(NAV_SCREEN_BUS, bus_screen_create);
    nav_register(NAV_SCREEN_SCOPE, scope_screen_create);
    nav_register(NAV_SCREEN_MEDIA, media_screen_create);
}
