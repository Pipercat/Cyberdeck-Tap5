#include "dashboard.h"
#include "nav.h"
#include "theme.h"
#include "module_card.h"
#include "lvgl.h"

typedef struct {
    const char *symbol;
    const char *title;
    nav_screen_id_t target;
    bool implemented;  // steuert die Karten-Unterschrift, siehe dashboard_create()
} module_entry_t;

// Modul-Grid gemaess Architektur-Plan Abschnitt 7. Reihenfolge wie in der
// Nutzeranforderung 3 (Flash, GPIO, Serial, I2C, SPI, PWM, ADC/Scope,
// Sensors, Camera, Audio, Network, Files, Projects, System, Settings).
// "implemented" muss beim Fertigstellen eines Moduls hier mit umgestellt
// werden - sonst zeigt die Karte weiter "Coming Soon" trotz funktionierender
// Unterseite (auf echter Hardware als Bug gemeldet, siehe Git-Historie).
static const module_entry_t k_modules[] = {
    { LV_SYMBOL_DOWNLOAD, "Flash",       NAV_SCREEN_FLASH,   false },
    { LV_SYMBOL_SHUFFLE,  "GPIO",        NAV_SCREEN_GPIO,    true  },
    { LV_SYMBOL_KEYBOARD, "Serial",      NAV_SCREEN_SERIAL,  true  },
    { LV_SYMBOL_LIST,     "I2C Scanner", NAV_SCREEN_I2C,     true  },
    { LV_SYMBOL_LIST,     "SPI Tools",   NAV_SCREEN_SPI,     false },
    { LV_SYMBOL_LOOP,     "PWM",         NAV_SCREEN_PWM,     true  },
    { LV_SYMBOL_IMAGE,    "ADC / Scope", NAV_SCREEN_ADC,     true  },
    { LV_SYMBOL_EYE_OPEN, "Sensors",     NAV_SCREEN_SENSORS, true  },
    { LV_SYMBOL_VIDEO,    "Camera",      NAV_SCREEN_CAMERA,  false },
    { LV_SYMBOL_VOLUME_MAX,"Audio",      NAV_SCREEN_AUDIO,   true  },
    { LV_SYMBOL_WIFI,     "Network",     NAV_SCREEN_NETWORK, true  },
    { LV_SYMBOL_DIRECTORY,"Files",       NAV_SCREEN_FILES,   false },
    { LV_SYMBOL_COPY,     "Projects",    NAV_SCREEN_PROJECTS,false },
    { LV_SYMBOL_SETTINGS, "System",      NAV_SCREEN_SYSTEM,  true  },
    { LV_SYMBOL_SETTINGS, "Settings",    NAV_SCREEN_SETTINGS,true  },
};
#define MODULE_COUNT (sizeof(k_modules) / sizeof(k_modules[0]))

static void module_card_click_cb(lv_event_t *e)
{
    nav_screen_id_t target = (nav_screen_id_t)(intptr_t)lv_event_get_user_data(e);
    nav_show(target);
}

// Quick Actions (Architektur-Plan Abschnitt 7): direkte Sprungmarken zu den
// am haeufigsten gebrauchten, bereits fertigen Modulen - bewusst nur
// implementierte Ziele, kein "Coming Soon" in dieser Leiste.
typedef struct {
    const char *symbol;
    const char *label;
    nav_screen_id_t target;
} quick_action_t;

static const quick_action_t k_quick_actions[] = {
    { LV_SYMBOL_KEYBOARD, "Serial",  NAV_SCREEN_SERIAL },
    { LV_SYMBOL_SHUFFLE,  "GPIO",    NAV_SCREEN_GPIO },
    { LV_SYMBOL_LIST,     "I2C Scan",NAV_SCREEN_I2C },
    { LV_SYMBOL_VOLUME_MAX,"Audio",  NAV_SCREEN_AUDIO },
    { LV_SYMBOL_WIFI,     "Network", NAV_SCREEN_NETWORK },
};
#define QUICK_ACTION_COUNT (sizeof(k_quick_actions) / sizeof(k_quick_actions[0]))

static void quick_action_click_cb(lv_event_t *e)
{
    nav_screen_id_t target = (nav_screen_id_t)(intptr_t)lv_event_get_user_data(e);
    nav_show(target);
}

#define GRID_COLUMNS 3
#define GRID_ROWS    ((MODULE_COUNT + GRID_COLUMNS - 1) / GRID_COLUMNS)

static lv_obj_t *dashboard_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_style_pad_top(scr, THEME_SCREEN_PAD_TOP, 0); // ueberschreibt pad_all nur oben, Platz fuer Statusleiste
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 12, 0);

    // --- Quick Actions: schmale, horizontal scrollbare Leiste ---
    lv_obj_t *quick_row = lv_obj_create(scr);
    lv_obj_remove_style_all(quick_row);
    lv_obj_set_size(quick_row, LV_PCT(100), THEME_TOUCH_TARGET_MIN);
    lv_obj_set_flex_flow(quick_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(quick_row, 10, 0);
    lv_obj_set_scroll_dir(quick_row, LV_DIR_HOR);
    lv_obj_add_flag(quick_row, LV_OBJ_FLAG_SCROLL_ONE);

    for (size_t i = 0; i < QUICK_ACTION_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(quick_row);
        lv_obj_set_height(btn, LV_PCT(100));
        lv_obj_set_style_bg_color(btn, THEME_COLOR_SURFACE_HI, 0);
        lv_obj_add_event_cb(btn, quick_action_click_cb, LV_EVENT_CLICKED,
                             (void *)(intptr_t)k_quick_actions[i].target);
        lv_obj_t *row = lv_obj_create(btn);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 6, 0);
        lv_obj_t *icon = lv_label_create(row);
        lv_label_set_text(icon, k_quick_actions[i].symbol);
        lv_obj_set_style_text_color(icon, THEME_COLOR_ACCENT, 0);
        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text(label, k_quick_actions[i].label);
    }

    // --- Modul-Grid: Karten werden per LV_GRID_FR(1) dynamisch auf Breite/
    // Hoehe des verbleibenden Platzes gestreckt, damit keine grossen
    // Freiflaechen entstehen. ---
    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    static int32_t col_dsc[GRID_COLUMNS + 1];
    static int32_t row_dsc[GRID_ROWS + 1];
    for (int i = 0; i < GRID_COLUMNS; i++) col_dsc[i] = LV_GRID_FR(1);
    col_dsc[GRID_COLUMNS] = LV_GRID_TEMPLATE_LAST;
    for (int i = 0; i < GRID_ROWS; i++) row_dsc[i] = LV_GRID_FR(1);
    row_dsc[GRID_ROWS] = LV_GRID_TEMPLATE_LAST;

    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);

    for (size_t i = 0; i < MODULE_COUNT; i++) {
        const char *status = k_modules[i].implemented ? NULL : "Coming Soon";
        lv_obj_t *card = module_card_create(grid, k_modules[i].symbol, k_modules[i].title, status,
                                             module_card_click_cb, (void *)(intptr_t)k_modules[i].target);
        int col = i % GRID_COLUMNS;
        int row = i / GRID_COLUMNS;
        lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
    }

    return scr;
}

void dashboard_register(void)
{
    nav_register(NAV_SCREEN_DASHBOARD, dashboard_create);
}
