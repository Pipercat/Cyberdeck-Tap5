/**
 * audio_module.h - Lautsprecher-/Mikrofon-Logik (Nutzeranforderungen 13/14).
 *
 * I2S-Pins (G26/27/28/29/30) sind PIN_ROLE_INTERNAL (siehe pin_table.c) -
 * dieses Modul ist wie board_init.c der legitime Systembesitzer und
 * konfiguriert sie direkt, ohne hal_gpio_request().
 *
 * Init-Sequenz (I2S STD/TDM-Konfiguration, ES8388/ES7210-Codec-Setup, I2C-
 * Adressen ES8388_CODEC_DEFAULT_ADDR/ES7210_CODEC_DEFAULT_ADDR) 1:1 aus dem
 * offiziellen M5Stack-Werksfirmware-Repository uebernommen:
 * github.com/m5stack/M5Tab5-UserDemo, m5stack_tab5.c (bsp_audio_init(),
 * bsp_audio_codec_speaker_init(), bsp_audio_codec_microphone_init()).
 * Lautsprecher-Enable (SPK_EN) laeuft laut dieser Referenz ueber PI4IOE1 P1 -
 * bereits in io_expander.c auf High gesetzt (Phase 1), hier keine weitere
 * IO-Expander-Aktion noetig.
 *
 * Mikrofon-Pegel ist eine RELATIVE Groesse (unkalibriertes dB-SPL) - siehe
 * Nutzeranforderung 13: klare Unterscheidung zwischen nuetzlicher relativer
 * Lautstaerkemessung und kalibriertem Messgeraet.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialisiert I2S-Peripherie und beide Codec-Geraete (Lautsprecher ES8388,
// Mikrofon ES7210). Einmalig nach board_init() aufrufen.
esp_err_t audio_module_init(void);

// Startet die Wiedergabe eines Sinustons in einem Hintergrund-Task
// (nicht blockierend). Ein vorheriger Ton/Sweep wird automatisch gestoppt.
esp_err_t audio_module_start_tone(float freq_hz, uint8_t volume_percent);

// Startet einen linearen Sweep von start_hz nach end_hz ueber duration_ms,
// danach stoppt die Wiedergabe automatisch.
esp_err_t audio_module_start_sweep(float start_hz, float end_hz, uint8_t volume_percent, uint32_t duration_ms);

// Bricht eine laufende Wiedergabe (Ton oder Sweep) ab.
void audio_module_stop_playback(void);

// Startet kontinuierliches Mikrofon-Sampling im Hintergrund-Task.
esp_err_t audio_module_start_mic(void);
void audio_module_stop_mic(void);

// Liefert den zuletzt gemessenen Pegel (0.0-1.0 linear, RELATIV/unkalibriert)
// sowie den Spitzenwert seit dem letzten Aufruf (wird danach zurueckgesetzt).
void audio_module_get_mic_level(float *out_rms, float *out_peak);

#ifdef __cplusplus
}
#endif
