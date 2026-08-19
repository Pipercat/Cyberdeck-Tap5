/**
 * pin_table.h - Zentrale, verifizierte Pin-Wahrheit fuer M5Stack Tab5 (ESP32-P4).
 *
 * WICHTIG: Diese Tabelle ist die EINZIGE Stelle im Projekt, die GPIO-Rollen
 * definiert. Kein Modul darf einen Pin ansprechen, ohne zuvor hal_gpio_request()
 * (siehe hal_gpio.h) aufzurufen, das gegen diese Tabelle prueft.
 *
 * Quellenlage (Stand Phase 1, siehe /docs/hardware_reference.md fuer Details):
 *   - PIN_ROLE_INTERNAL / PIN_ROLE_SHARED_BUS: bestaetigt durch docs.m5stack.com/en/core/Tab5
 *     und den offenen Quellcode von m5stack/M5Unified (M5Unified.cpp, Board-Tabelle Tab5).
 *   - PIN_ROLE_FREE (Port A/B/C, M-Bus-GPIOs): ebenfalls aus M5Unified.cpp entnommen
 *     (offizielle M5Stack-HAL). Vor produktivem Einsatz mit hoeherer Strombelastung
 *     zusaetzlich gegen das Schematic-PDF (docs.m5stack.com/en/products/sku/K145)
 *     gegenpruefen - siehe TODO-Markierungen in pin_table.c.
 *   - PIN_ROLE_UNVERIFIED: GPIO_EXT-Header und exakte PI4IOE5V6408-Bitbelegung
 *     sind aus verfuegbaren Quellen NICHT zweifelsfrei zu belegen. Diese Pins
 *     bleiben gesperrt, bis eine Primaerquelle das bestaetigt.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PIN_ROLE_FREE = 0,     // frei fuer Nutzer-Konfiguration (Port A/B/C, M-Bus)
    PIN_ROLE_INTERNAL,     // fest durch Board-Peripherie belegt - GESPERRT, kein Zugriff
    PIN_ROLE_SHARED_BUS,   // interner Systembus (I2C etc.), nur lesend/diagnostisch einsehbar
    PIN_ROLE_UNVERIFIED    // Funktion nicht aus Primaerquelle bestaetigt - GESPERRT bis Verifikation
} pin_role_t;

typedef struct {
    int8_t       gpio_num;           // -1 = kein echter ESP32-GPIO (z.B. dedizierte CSI-Leitung)
    pin_role_t   role;
    const char  *label;              // z.B. "Port A SDA", "SDIO2 CLK (Wi-Fi-Modul)"
    const char  *internal_function;  // NULL wenn PIN_ROLE_FREE
    bool         supports_input;
    bool         supports_output;
    bool         supports_adc;
    bool         supports_pwm;
    bool         supports_interrupt;
    int8_t       adc_unit;           // -1 wenn supports_adc == false
    int8_t       adc_channel;
} pin_descriptor_t;

// Anzahl Eintraege in g_pin_table
extern const size_t g_pin_table_count;
extern const pin_descriptor_t g_pin_table[];

// Sucht einen Pin-Deskriptor anhand der GPIO-Nummer. Gibt NULL zurueck,
// wenn der Pin nicht in der Tabelle ist (=> als UNVERIFIED/gesperrt behandeln).
const pin_descriptor_t *pin_table_find(int gpio_num);

// ESP32-P4-Datenblatt-Grenzwerte (Espressif ESP32-P4 Datasheet, Kap. 5.4 DC
// Characteristics): Default-Treiberstaerke 20 mA je Pin (konfigurierbar bis
// 40 mA). Als konservativer Default fuer Warnungen im PWM/GPIO-Modul verwendet.
#define TAB5_GPIO_DEFAULT_DRIVE_MA   20
#define TAB5_GPIO_MAX_DRIVE_MA       40
#define TAB5_GPIO_LOGIC_VOLTAGE_MV   3300

#ifdef __cplusplus
}
#endif
