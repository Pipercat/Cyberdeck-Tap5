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

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Farbpalette (Dark-Mode-first) ---
#define THEME_COLOR_BG        lv_color_hex(0x0d1117)   // App-Hintergrund
#define THEME_COLOR_SURFACE   lv_color_hex(0x161b22)   // Karten/Panels
#define THEME_COLOR_SURFACE_HI lv_color_hex(0x21262d)  // Karten, gedrueckt/hover
#define THEME_COLOR_BORDER    lv_color_hex(0x30363d)
#define THEME_COLOR_TEXT      lv_color_hex(0xe6edf3)
#define THEME_COLOR_TEXT_DIM  lv_color_hex(0x8b949e)
#define THEME_COLOR_ACCENT    lv_color_hex(0x2f81f7)   // Primaerakzent (Aktionen, Fokus)
#define THEME_COLOR_SUCCESS   lv_color_hex(0x3fb950)
#define THEME_COLOR_WARNING   lv_color_hex(0xd29922)
#define THEME_COLOR_DANGER    lv_color_hex(0xf85149)

// --- Layout-Konstanten ---
#define THEME_RADIUS_CARD     10
#define THEME_PAD_CARD        12
#define THEME_TOUCH_TARGET_MIN 64   // Mindestgroesse fuer Touch-Ziele in px

// Freiraum, den jeder Screen oben reservieren muss, damit die persistente
// Statusleiste (36px, auf lv_layer_top()) Titel/Inhalt nicht verdeckt.
// Groesser als die reine Statusleistenhoehe, da THEME_FONT_TITLE (24px)
// zusaetzlichen vertikalen Platz braucht (auf echter Hardware bemaengelt).
#define THEME_SCREEN_PAD_TOP  72

// --- Schriftgroessen ---
#define THEME_FONT_BODY  (&lv_font_montserrat_20)  // normaler Screen-Text, Buttons
#define THEME_FONT_TITLE (&lv_font_montserrat_24)  // Screen-/Karten-Ueberschriften

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

#ifdef __cplusplus
}
#endif
