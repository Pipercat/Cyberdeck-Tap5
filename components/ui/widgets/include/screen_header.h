/**
 * screen_header.h - Einheitliche Kopfzeile (Titel + schwebender Zurueck-
 * Button) fuer Modul-Screens. Buendelt ein Bau-Muster, das vorher in fast
 * jedem Modul identisch dupliziert war (Header-Row, Titel-Label,
 * back_button_create-Aufruf) - Aenderungen an Hoehe/Abstand/Stil wirken
 * jetzt an einer Stelle statt zehn.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Erstellt Header-Row (Titel links) + schwebenden Zurueck-Button auf scr.
// Rueckgabe ist der Zurueck-Button (wie back_button_create) - fuer Module,
// die ihn spaeter ausblenden muessen (z.B. waehrend eine Bildschirmtastatur
// offen ist, siehe serial_module_ui.c/network_module_ui.c).
lv_obj_t *screen_header_create(lv_obj_t *scr, const char *title, lv_event_cb_t back_cb);

#ifdef __cplusplus
}
#endif
