/**
 * settings_module_ui.h - Settings-Screen: Backlight-Helligkeit (persistiert
 * ueber settings.c/NVS) und Werksreset. Kein Dark-/Light-Mode-Umschalter,
 * da die UI aktuell ausschliesslich Dark-Mode implementiert (settings_t.dark_mode
 * ist fuer eine spaetere Phase vorbereitet, aber ohne Wirkung - ein Schalter
 * dafuer waere ein vorgetaeuschtes Feature).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void settings_module_ui_register(void);

#ifdef __cplusplus
}
#endif
