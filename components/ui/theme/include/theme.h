/**
 * theme.h - Dark-Mode-Design-Tokens fuer das gesamte UI (Nutzeranforderung 3:
 * professionell, technisch, modern, Dark Mode, hohe Informationsdichte,
 * grosse Touch-Ziele, klare Statusanzeigen, konsistente Icons statt Emojis).
 *
 * Bewusste Vereinfachung Phase 1: nur LV_FONT_DEFAULT (Montserrat 14, per
 * LVGL-Kconfig-Default aktiv) wird verwendet, um Build-Fehler durch nicht
 * aktivierte Schriftgroessen zu vermeiden. Weitere Groessen (fuer visuelle
 * Hierarchie: Kartentitel vs. Statuswerte) werden in Phase 2 ueber
 * menuconfig (Component config -> LVGL -> Font) aktiviert und hier ergaenzt.
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

// Initialisiert wiederverwendbare lv_style_t-Objekte. Einmalig vor dem
// ersten Bildschirmaufbau aufrufen.
void theme_init(void);

// Wendet den Karten-Stil (Hintergrund, Rand, Radius) auf obj an.
void theme_apply_card(lv_obj_t *obj);

// Wendet den Bildschirm-Basisstil (Hintergrundfarbe) auf obj an.
void theme_apply_screen(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif
