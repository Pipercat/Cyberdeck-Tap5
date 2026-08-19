/**
 * io_expander.h - PI4IOE5V6408-Treiber fuer die beiden internen IO-Expander
 * (I2C 0x43, 0x44), die u.a. Display-Reset, Touch-Reset, Kamera-Reset,
 * Lautsprecher-Enable und die 5V/USB-Versorgung steuern.
 *
 * Quelle (verifiziert, nicht erfunden): offizielles M5Stack-Werksfirmware-
 * Repository github.com/m5stack/M5Tab5-UserDemo,
 * platforms/tab5/components/m5stack_tab5/m5stack_tab5.c,
 * Funktion bsp_io_expander_pi4ioe_init() - Register-Adressen und Bitmuster
 * 1:1 von dort uebernommen (siehe io_expander.c fuer die Kommentare mit der
 * Original-Pinbelegung P0-P7 je Expander).
 *
 * Diese Pins sind KEINE ESP32-P4-GPIOs und tauchen daher nicht in
 * pin_table.c auf - sie werden ausschliesslich von board_init.c ueber diese
 * Datei angesprochen, nie direkt von einem Fach-Modul.
 */
#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialisiert beide Expander und entlaesst LCD_RST, TP_RST, CAM_RST,
// SPK_EN, EXT5V_EN, WLAN_PWR_EN, USB5V_EN aus dem Reset-/Aus-Zustand.
// Muss VOR Display- und Touch-Init auf dem system_i2c-Bus aufgerufen werden.
esp_err_t io_expander_init(i2c_master_bus_handle_t bus_handle);

#ifdef __cplusplus
}
#endif
