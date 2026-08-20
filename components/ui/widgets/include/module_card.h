/**
 * module_card.h - Wiederverwendbare Karten-Kachel fuer das Dashboard-Grid.
 * Grosse Touch-Ziele (>= THEME_TOUCH_TARGET_MIN), konsistentes Icon (LVGL-
 * Symbolfont, keine Emojis), Titel + optionaler Statustext.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// symbol: LV_SYMBOL_* oder NULL. status_text: z.B. "Coming Soon", oder NULL.
// on_click: Event-Callback (LV_EVENT_CLICKED) oder NULL fuer deaktivierte Karten.
lv_obj_t *module_card_create(lv_obj_t *parent, const char *symbol, const char *title,
                              const char *status_text, lv_event_cb_t on_click, void *user_data);

// Aktualisiert die Statuszeile einer bereits erstellten Karte nachtraeglich
// (fuer Live-Werte, die sich nach dem Erstellen aendern - z.B. Serial-
// Laufstatus, Netzwerk-IP/RSSI). Ohne Wirkung auf Karten ohne Klick-Handler
// (deaktivierte "Coming Soon"-Karten behalten ihren statischen Text).
void module_card_set_status(lv_obj_t *card, const char *status_text, lv_color_t color);

#ifdef __cplusplus
}
#endif
