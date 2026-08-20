/**
 * theme.h - Dark-Mode-Design-Tokens fuer das gesamte UI (Nutzeranforderung 3:
 * professionell, technisch, modern, Dark Mode, hohe Informationsdichte,
 * grosse Touch-Ziele, klare Statusanzeigen, konsistente Icons statt Emojis).
 *
 * Schriftgroessen: das 5"-Panel hat bei 1280x720 eine hohe Pixeldichte
 * (~294 PPI) - der LVGL-Default (Montserrat 14) wirkt darauf sehr klein
 * (auf echter Hardware bemaengelt). THEME_FONT_BODY (20) und
 * THEME_FONT_TITLE (24) werden ueber sdkconfig.defaults aktiviert
 * (CONFIG_LV_FONT_MONTSERRAT_20/24) und via theme_apply_screen()/
 * theme_apply_card() automatisch an alle Kind-Widgets vererbt, sodass
 * nicht jedes einzelne Label manuell umgestellt werden muss.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Farbpalette (Dark Digital Engineering, Nutzervorgabe 2026-08-20) ---
// Bewusst ueberwiegend Schwarz/Anthrazit/Weiss/Grau - Farbe zeigt Bedeutung
// und Status, nicht Dekoration ("Farben sparsam einsetzen").
#define THEME_COLOR_BG         lv_color_hex(0x05080d)   // App-Hintergrund - einheitlich auf allen Screens
#define THEME_COLOR_SURFACE    lv_color_hex(0x0d141e)   // Karten/Panels
#define THEME_COLOR_SURFACE_HI lv_color_hex(0x101923)   // Karten, gedrueckt/hover/aktive Nav
#define THEME_COLOR_BORDER     lv_color_hex(0x1c2530)
#define THEME_COLOR_TEXT       lv_color_hex(0xe6edf3)
#define THEME_COLOR_TEXT_DIM   lv_color_hex(0x8b949e)
#define THEME_COLOR_ACCENT     lv_color_hex(0x22d3ee)   // Electric Cyan - Primaerakzent (Aktionen, Fokus)
#define THEME_COLOR_SUCCESS    lv_color_hex(0x34d399)   // Grün - OK/Connected
#define THEME_COLOR_WARNING    lv_color_hex(0xf59e0b)   // Orange - Warning
#define THEME_COLOR_DANGER     lv_color_hex(0xef4444)   // Rot - Error
#define THEME_COLOR_PURPLE     lv_color_hex(0xa78bfa)   // Spezielle Tools / Serial / Debug

// --- Layout-Konstanten ---
#define THEME_RADIUS_CARD     18   // 14-20px Vorgabe
#define THEME_PAD_CARD        16
#define THEME_TOUCH_TARGET_MIN 64   // Mindestgroesse fuer Touch-Ziele in px

// Freiraum, den jeder Screen oben reservieren muss, damit die persistente
// Statusleiste (36px, auf lv_layer_top()) Titel/Inhalt nicht verdeckt.
// Groesser als die reine Statusleistenhoehe, da THEME_FONT_TITLE (24px)
// zusaetzlichen vertikalen Platz braucht (auf echter Hardware bemaengelt).
#define THEME_SCREEN_PAD_TOP  72

// --- Schriftgroessen ---
#define THEME_FONT_BODY  (&lv_font_montserrat_20)  // normaler Screen-Text, Buttons
#define THEME_FONT_TITLE (&lv_font_montserrat_24)  // Screen-/Karten-Ueberschriften

typedef enum {
    THEME_BTN_PRIMARY,
    THEME_BTN_SUCCESS,
    THEME_BTN_WARNING,
    THEME_BTN_DANGER,
    THEME_BTN_NEUTRAL,
} theme_button_variant_t;

typedef enum {
    THEME_STATUS_SUCCESS,
    THEME_STATUS_WARNING,
    THEME_STATUS_DANGER,
    THEME_STATUS_INFO,
    THEME_STATUS_NEUTRAL,
} theme_status_t;

// Initialisiert wiederverwendbare lv_style_t-Objekte. Einmalig vor dem
// ersten Bildschirmaufbau aufrufen.
void theme_init(void);

// Wendet den Karten-Stil (Hintergrund, Rand, Radius) auf obj an.
void theme_apply_card(lv_obj_t *obj);

// Wendet den Bildschirm-Basisstil (Hintergrundfarbe, THEME_FONT_BODY als
// vererbter Default fuer alle Kind-Widgets) auf obj an.
void theme_apply_screen(lv_obj_t *obj);

// Wendet THEME_FONT_TITLE + Textfarbe auf ein Titel-/Ueberschriften-Label an.
void theme_apply_title(lv_obj_t *label);

void theme_apply_button(lv_obj_t *btn, theme_button_variant_t variant);

// Wechselt die Variante eines bereits mit theme_apply_button() erzeugten
// Buttons zur Laufzeit (z.B. Start/Stop-Button: SUCCESS waehrend gestoppt,
// DANGER waehrend laeuft). Pressed-/Disabled-Feedback bleibt erhalten, wird
// nicht erneut angewendet.
void theme_set_button_variant(lv_obj_t *btn, theme_button_variant_t variant);
void theme_apply_toggle(lv_obj_t *btn, bool active);
void theme_set_toggle_active(lv_obj_t *btn, bool active);
void theme_apply_status_chip(lv_obj_t *label, theme_status_t status);

#ifdef __cplusplus
}
#endif
