#include "dashboard.h"
#include "nav.h"
#include "theme.h"
#include "module_card.h"
#include "pin_table.h"
#include "gpio_module.h"
#include "serial_module.h"
#include "wifi_module.h"
#include "lvgl.h"
#include <stdio.h>

typedef struct {
    const char *symbol;
    const char *title;
    nav_screen_id_t target;
    bool implemented;  // steuert Karten-Unterschrift UND Klickbarkeit, siehe dashboard_create()
} module_entry_t;

typedef struct {
    const char *label;
    const module_entry_t *modules;
    size_t count;
} module_group_t;

// "implemented" muss beim Fertigstellen eines Moduls hier mit umgestellt
// werden - sonst zeigt die Karte weiter "Coming Soon" trotz funktionierender
// Unterseite (auf echter Hardware als Bug gemeldet, siehe Git-Historie).
//
// In thematische Gruppen sortiert statt einem einzigen 15-Kachel-Raster
// (Nutzerfeedback: wirkte wie ein generisches Admin-Dashboard, keine
// erkennbare Prioritaet zwischen z.B. GPIO und Settings).
static const module_entry_t k_group_target[] = {
    { LV_SYMBOL_DOWNLOAD,  "Flash",    NAV_SCREEN_FLASH,    false },
    { LV_SYMBOL_KEYBOARD,  "Serial",   NAV_SCREEN_SERIAL,   true  },
    { LV_SYMBOL_DIRECTORY, "Files",    NAV_SCREEN_FILES,    false },
    { LV_SYMBOL_COPY,      "Projects", NAV_SCREEN_PROJECTS, false },
};
static const module_entry_t k_group_electronics[] = {
    { LV_SYMBOL_SHUFFLE, "GPIO",        NAV_SCREEN_GPIO, true },
    { LV_SYMBOL_LOOP,    "PWM",         NAV_SCREEN_PWM,  true },
    { LV_SYMBOL_IMAGE,   "ADC / Scope", NAV_SCREEN_ADC,  true },
};
static const module_entry_t k_group_bus[] = {
    { LV_SYMBOL_LIST, "I2C Scanner", NAV_SCREEN_I2C, true  },
    { LV_SYMBOL_LIST, "SPI Tools",   NAV_SCREEN_SPI, false },
};
static const module_entry_t k_group_media[] = {
    { LV_SYMBOL_VIDEO,      "Camera", NAV_SCREEN_CAMERA, false },
    { LV_SYMBOL_VOLUME_MAX, "Audio",  NAV_SCREEN_AUDIO,  true  },
};
static const module_entry_t k_group_device[] = {
    { LV_SYMBOL_EYE_OPEN, "Sensors", NAV_SCREEN_SENSORS, true },
    { LV_SYMBOL_WIFI,     "Network", NAV_SCREEN_NETWORK, true },
};
static const module_entry_t k_group_system[] = {
    { LV_SYMBOL_SETTINGS, "System",   NAV_SCREEN_SYSTEM,   true },
    { LV_SYMBOL_SETTINGS, "Settings", NAV_SCREEN_SETTINGS, true },
};

static const module_group_t k_groups[] = {
    { "TARGET",       k_group_target,      sizeof(k_group_target) / sizeof(k_group_target[0]) },
    { "ELECTRONICS",  k_group_electronics, sizeof(k_group_electronics) / sizeof(k_group_electronics[0]) },
    { "BUS ANALYZER", k_group_bus,         sizeof(k_group_bus) / sizeof(k_group_bus[0]) },
    { "MEDIA",        k_group_media,       sizeof(k_group_media) / sizeof(k_group_media[0]) },
    { "DEVICE",       k_group_device,      sizeof(k_group_device) / sizeof(k_group_device[0]) },
    { "SYSTEM",       k_group_system,      sizeof(k_group_system) / sizeof(k_group_system[0]) },
};
#define GROUP_COUNT (sizeof(k_groups) / sizeof(k_groups[0]))

#define CARD_HEIGHT 108

static void module_card_click_cb(lv_event_t *e)
{
    nav_screen_id_t target = (nav_screen_id_t)(intptr_t)lv_event_get_user_data(e);
    nav_show(target);
}

// Live-Status auf den Dashboard-Karten (Nutzerfeedback: Karten sollten
// selbst schon ein Diagnoseinstrument sein statt nur Icon+Titel zu zeigen).
// Bewusst nur fuer Module mit bereits vorhandenen, guenstig abfragbaren
// Live-Werten - keine neuen Modul-APIs nur fuer diese Anzeige geschaffen.
static lv_obj_t *s_serial_card = NULL;
static lv_obj_t *s_network_card = NULL;
static lv_obj_t *s_gpio_card = NULL;
static lv_timer_t *s_refresh_timer = NULL;

static void refresh_live_status(lv_timer_t *timer)
{
    (void)timer;

    if (s_serial_card != NULL) {
        bool running = serial_module_is_running();
        module_card_set_status(s_serial_card, running ? "RUNNING" : "Idle",
                                running ? THEME_COLOR_SUCCESS : THEME_COLOR_TEXT_DIM);
    }

    if (s_network_card != NULL) {
        wifi_module_state_t state = wifi_module_get_state();
        char buf[48];
        if (state == WIFI_MODULE_STATE_CONNECTED) {
            char ip[16] = "?";
            wifi_module_get_ip_str(ip, sizeof(ip));
            snprintf(buf, sizeof(buf), "%s, %d dBm", ip, wifi_module_get_rssi());
            module_card_set_status(s_network_card, buf, THEME_COLOR_SUCCESS);
        } else if (state == WIFI_MODULE_STATE_CONNECTING) {
            module_card_set_status(s_network_card, "Verbinde...", THEME_COLOR_WARNING);
        } else {
            module_card_set_status(s_network_card, "Getrennt", THEME_COLOR_TEXT_DIM);
        }
    }

    if (s_gpio_card != NULL) {
        int active = 0;
        for (size_t i = 0; i < g_pin_table_count; i++) {
            if (g_pin_table[i].role == PIN_ROLE_FREE &&
                gpio_module_get_mode(g_pin_table[i].gpio_num) != GPIO_MODULE_MODE_UNCONFIGURED) {
                active++;
            }
        }
        char buf[24];
        if (active > 0) {
            snprintf(buf, sizeof(buf), "%d aktiv", active);
            module_card_set_status(s_gpio_card, buf, THEME_COLOR_SUCCESS);
        } else {
            module_card_set_status(s_gpio_card, "Frei", THEME_COLOR_TEXT_DIM);
        }
    }
}

static void dashboard_on_show(void)
{
    if (s_refresh_timer == NULL) {
        s_refresh_timer = lv_timer_create(refresh_live_status, 2000, NULL);
        refresh_live_status(NULL);  // sofort fuellen statt 2s auf den ersten Tick zu warten
    } else {
        lv_timer_resume(s_refresh_timer);
    }
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

    // --- Modul-Gruppen: eigene Ueberschrift je Themenbereich statt einem
    // einzigen 15-Kachel-Raster ohne erkennbare Prioritaet. Scrollbar, da
    // Ueberschriften+Zeilenumbrueche mehr Platz brauchen als das alte
    // Fill-Height-Raster. ---
    lv_obj_t *groups_area = lv_obj_create(scr);
    lv_obj_remove_style_all(groups_area);
    lv_obj_set_width(groups_area, LV_PCT(100));
    lv_obj_set_flex_grow(groups_area, 1);
    lv_obj_set_flex_flow(groups_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(groups_area, 14, 0);

    for (size_t g = 0; g < GROUP_COUNT; g++) {
        const module_group_t *group = &k_groups[g];

        lv_obj_t *header = lv_label_create(groups_area);
        lv_label_set_text(header, group->label);
        lv_obj_set_style_text_color(header, THEME_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_letter_space(header, 1, 0);

        lv_obj_t *row = lv_obj_create(groups_area);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_row(row, 10, 0);
        lv_obj_set_style_pad_column(row, 10, 0);

        for (size_t i = 0; i < group->count; i++) {
            const module_entry_t *m = &group->modules[i];
            const char *status = m->implemented ? NULL : "Coming Soon";
            lv_event_cb_t on_click = m->implemented ? module_card_click_cb : NULL;
            lv_obj_t *card = module_card_create(row, m->symbol, m->title, status,
                                                 on_click, (void *)(intptr_t)m->target);
            lv_obj_set_size(card, LV_PCT(31), CARD_HEIGHT);

            if (m->target == NAV_SCREEN_SERIAL) s_serial_card = card;
            else if (m->target == NAV_SCREEN_NETWORK) s_network_card = card;
            else if (m->target == NAV_SCREEN_GPIO) s_gpio_card = card;
        }
    }

    return scr;
}

void dashboard_register(void)
{
    nav_register(NAV_SCREEN_DASHBOARD, dashboard_create);
    nav_register_on_show(NAV_SCREEN_DASHBOARD, dashboard_on_show);
}
