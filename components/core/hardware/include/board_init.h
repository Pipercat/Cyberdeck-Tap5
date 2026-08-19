/**
 * board_init.h - Bring-up fuer Display (MIPI-DSI), Touch (I2C) und LVGL.
 *
 * Panel-Generation wird zur Laufzeit per I2C-Sondierung erkannt (ST7121 vs.
 * ST7123, siehe board_init.c); Gen1-Geraete (ILI9881C+GT911) werden erkannt,
 * aber ihr Treiberpfad ist noch nicht implementiert (board_init() bricht
 * dann kontrolliert mit einer Fehlermeldung ab).
 *
 * Display laeuft in seiner physischen Portrait-Ausrichtung (720x1280) - eine
 * Landscape-Rotation wurde auf echter Hardware ausprobiert (esp_lvgl_port
 * SW-Rotation), fuehrte aber zu fehlerhaftem/unvollstaendigem Rendering bzw.
 * Abstuerzen und wurde wieder entfernt. Siehe docs/hardware_reference.md.
 */
#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialisiert internen I2C-Bus, Display, Touch und LVGL-Port.
// Muss vor jedem UI-/LVGL-Zugriff einmalig aufgerufen werden.
esp_err_t board_init(void);

// Liefert das LVGL-Display-Handle (gueltig nach erfolgreichem board_init()).
lv_display_t *board_get_display(void);

// Backlight-Helligkeit 0-100% (LEDC-PWM auf GPIO 22).
esp_err_t board_set_backlight(uint8_t percent);

// Liefert den internen System-I2C-Bus (G31/G32) fuer Module, die auf dort
// vorhandene Sensoren lesend zugreifen (INA226 0x41, RX8130CE 0x32, BMI270
// 0x68 etc.) - siehe pin_table.c PIN_ROLE_SHARED_BUS. NIE fuer Schreib-
// zugriffe verwenden, die Display/Touch/Kamera/Audio stoeren koennten.
i2c_master_bus_handle_t board_get_system_i2c_bus(void);

#ifdef __cplusplus
}
#endif
