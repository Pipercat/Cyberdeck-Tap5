#include "dashboard.h"
#include "nav.h"
#include "theme.h"
#include "module_card.h"
#include "lvgl.h"

typedef struct {
    const char *symbol;
    const char *title;
    nav_screen_id_t target;
} module_entry_t;

// Modul-Grid gemaess Architektur-Plan Abschnitt 7. Reihenfolge wie in der
// Nutzeranforderung 3 (Flash, GPIO, Serial, I2C, SPI, PWM, ADC/Scope,
// Sensors, Camera, Audio, Network, Files, Projects, System, Settings).
static const module_entry_t k_modules[] = {
    { LV_SYMBOL_DOWNLOAD, "Flash",       NAV_SCREEN_FLASH },
    { LV_SYMBOL_SHUFFLE,  "GPIO",        NAV_SCREEN_GPIO },
    { LV_SYMBOL_KEYBOARD, "Serial",      NAV_SCREEN_SERIAL },
    { LV_SYMBOL_LIST,     "I2C Scanner", NAV_SCREEN_I2C },
    { LV_SYMBOL_LIST,     "SPI Tools",   NAV_SCREEN_SPI },
    { LV_SYMBOL_LOOP,     "PWM",         NAV_SCREEN_PWM },
    { LV_SYMBOL_IMAGE,    "ADC / Scope", NAV_SCREEN_ADC },
    { LV_SYMBOL_EYE_OPEN, "Sensors",     NAV_SCREEN_SENSORS },
    { LV_SYMBOL_VIDEO,    "Camera",      NAV_SCREEN_CAMERA },
    { LV_SYMBOL_VOLUME_MAX,"Audio",      NAV_SCREEN_AUDIO },
    { LV_SYMBOL_WIFI,     "Network",     NAV_SCREEN_NETWORK },
    { LV_SYMBOL_DIRECTORY,"Files",       NAV_SCREEN_FILES },
    { LV_SYMBOL_COPY,     "Projects",    NAV_SCREEN_PROJECTS },
    { LV_SYMBOL_SETTINGS, "System",      NAV_SCREEN_SYSTEM },
    { LV_SYMBOL_SETTINGS, "Settings",    NAV_SCREEN_SETTINGS },
};
#define MODULE_COUNT (sizeof(k_modules) / sizeof(k_modules[0]))

static void module_card_click_cb(lv_event_t *e)
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
    lv_obj_set_style_pad_top(scr, 48, 0); // Platz fuer die persistente Statusleiste
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(scr, 12, 0);
    lv_obj_set_style_pad_column(scr, 12, 0);

    // Grid statt Flex-Wrap: Karten werden per LV_GRID_FR(1) dynamisch auf
    // Breite und Hoehe des Bildschirms gestreckt, damit bei unterschiedlichen
    // Aufloesungen/Orientierungen keine grossen Freiflaechen entstehen.
    static int32_t col_dsc[GRID_COLUMNS + 1];
    static int32_t row_dsc[GRID_ROWS + 1];
    for (int i = 0; i < GRID_COLUMNS; i++) col_dsc[i] = LV_GRID_FR(1);
    col_dsc[GRID_COLUMNS] = LV_GRID_TEMPLATE_LAST;
    for (int i = 0; i < GRID_ROWS; i++) row_dsc[i] = LV_GRID_FR(1);
    row_dsc[GRID_ROWS] = LV_GRID_TEMPLATE_LAST;

    lv_obj_set_grid_dsc_array(scr, col_dsc, row_dsc);
    lv_obj_set_layout(scr, LV_LAYOUT_GRID);

    for (size_t i = 0; i < MODULE_COUNT; i++) {
        lv_obj_t *card = module_card_create(scr, k_modules[i].symbol, k_modules[i].title, "Coming Soon",
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
