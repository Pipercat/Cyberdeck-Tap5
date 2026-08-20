/**
 * bottom_nav.h - Persistente Bottom-Navigation (Home/Files/Projects/System/
 * Settings) auf lv_layer_top(), ersetzt eine permanente Sidebar/Kachel-
 * Leiste. Nur auf den 5 Top-Level-Screens sichtbar - Werkzeug-Screens
 * (GPIO/Serial/PWM/...) blenden sie aus und nutzen stattdessen den
 * schwebenden Zurueck-Button (back_button.h), damit ein Werkzeug den
 * Bildschirm nicht mit zwei gleichzeitigen Navigationsmechanismen teilt.
 *
 * Nutzt bewusst int statt nav_screen_id_t: das nav-Component haengt bereits
 * von widgets ab (statusbar/back_button) - eine Rueckabhaengigkeit hier
 * waere zirkulaer. nav.c castet beim Aufruf/Registrieren entsprechend.
 */
#pragma once

#include <stddef.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOTTOM_NAV_HEIGHT 64

// Callback-Signatur fuer Item-Taps - i.d.R. von nav.c mit einem duennen
// Wrapper um nav_show() registriert (siehe bottom_nav_set_navigate_fn()).
typedef void (*bottom_nav_navigate_fn_t)(int screen_id);

typedef struct {
    const char *symbol;   // LV_SYMBOL_*
    const char *label;
    int screen_id;        // von nav.c aus nav_screen_id_t gecastet
} bottom_nav_item_t;

// Erstellt die Bottom-Navigation auf parent (i.d.R. lv_layer_top()) mit den
// angegebenen Items (Zeiger muss fuer die Laufzeit gueltig bleiben - i.d.R.
// ein static const-Array beim Aufrufer). Einmalig aus nav_init() aufrufen.
lv_obj_t *bottom_nav_create(lv_obj_t *parent, const bottom_nav_item_t *items, size_t count);

// Legt fest, welche Funktion beim Antippen eines Items aufgerufen wird.
void bottom_nav_set_navigate_fn(bottom_nav_navigate_fn_t fn);

// Blendet die Bar ein (mit hervorgehobenem aktivem Item) fuer die 5 Top-
// Level-Ziele, sonst aus. Von nav_show() nach jedem Screen-Wechsel
// aufgerufen - deckt damit auch Navigation ab, die nicht durch Antippen
// der Bar selbst ausgeloest wurde (z.B. ueber eine Dashboard-Kachel).
void bottom_nav_update(int screen_id);

#ifdef __cplusplus
}
#endif
