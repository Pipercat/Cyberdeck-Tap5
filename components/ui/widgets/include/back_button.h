/**
 * back_button.h - Schwebender Zurueck-Button am unteren Bildschirmrand.
 *
 * Grund: die Kopfzeilen-Variante (Titel + Button in einer Reihe am oberen
 * Bildschirmrand) wurde auf echter Hardware von der persistenten
 * Statusleiste (lv_layer_top(), siehe statusbar.c) verdeckt. Diese Variante
 * ist per LV_OBJ_FLAG_IGNORE_LAYOUT unabhaengig vom Flex-Layout des
 * Screens am unteren Rand fixiert - unabhaengig vom Inhalt darueber und
 * garantiert ausserhalb der Statusleisten-Zone.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Erstellt einen "Zurueck"-Button, der unten mittig auf parent (i.d.R. der
// Screen selbst) schwebt, unabhaengig vom Flex-/Grid-Layout des parent.
lv_obj_t *back_button_create(lv_obj_t *parent, lv_event_cb_t on_click);

#ifdef __cplusplus
}
#endif
