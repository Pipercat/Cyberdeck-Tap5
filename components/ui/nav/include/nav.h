/**
 * nav.h - Screen-Router/Navigationsstack (Architektur Abschnitt 4/7).
 *
 * Jedes Modul registriert seinen Screen einmalig ueber nav_register() mit
 * einer Erzeugerfunktion; der eigentliche lv_obj_t-Screen wird erst beim
 * ersten nav_show() erzeugt (lazy) und danach fuer die Laufzeit zwischen-
 * gespeichert. Die Statusleiste liegt auf lv_layer_top() und bleibt damit
 * ueber alle Screens hinweg sichtbar, ohne dass jedes Modul sie selbst
 * einbauen muss.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NAV_SCREEN_DASHBOARD = 0,
    NAV_SCREEN_FLASH,
    NAV_SCREEN_GPIO,
    NAV_SCREEN_SERIAL,
    NAV_SCREEN_I2C,
    NAV_SCREEN_SPI,
    NAV_SCREEN_PWM,
    NAV_SCREEN_ADC,
    NAV_SCREEN_SENSORS,
    NAV_SCREEN_CAMERA,
    NAV_SCREEN_AUDIO,
    NAV_SCREEN_NETWORK,
    NAV_SCREEN_FILES,
    NAV_SCREEN_PROJECTS,
    NAV_SCREEN_SYSTEM,
    NAV_SCREEN_SETTINGS,
    NAV_SCREEN_COUNT,
} nav_screen_id_t;

typedef lv_obj_t *(*nav_screen_create_fn_t)(void);
typedef void (*nav_screen_show_fn_t)(void);

// Initialisiert das Navigationssystem und die persistente Statusleiste.
// Einmalig nach board_init() aufrufen.
void nav_init(void);

// Registriert die Erzeugerfunktion fuer einen Screen. Wird der Screen nie
// gezeigt, wird create_fn nie aufgerufen (kein unnoetiger Speicherverbrauch).
void nav_register(nav_screen_id_t id, nav_screen_create_fn_t create_fn);

// Registriert eine optionale on_show-Callback, die JEDES Mal aufgerufen
// wird, wenn dieser (bereits erzeugte) Screen angezeigt wird - nicht nur
// beim ersten Aufbau. Fuer Module, die beim Wiederbetreten Ressourcen neu
// aktivieren muessen (z.B. Sample-Timer fortsetzen, Pin neu selektieren).
void nav_register_on_show(nav_screen_id_t id, nav_screen_show_fn_t on_show_fn);

// Zeigt den Screen an (erzeugt ihn beim ersten Aufruf). IDs ohne registrierte
// Erzeugerfunktion (nav_register) bekommen automatisch eine generische
// "Coming Soon"-Platzhalterseite.
void nav_show(nav_screen_id_t id);

#ifdef __cplusplus
}
#endif
