/**
 * pin_table.c - Verifizierte Pin-Tabelle M5Stack Tab5.
 * Siehe pin_table.h fuer Quellenlage und /docs/hardware_reference.md fuer Details.
 *
 * ADC-Kanalzuordnung (Phase 2): 1:1 aus der lokalen ESP-IDF-Primaerquelle
 * components/soc/esp32p4/include/soc/adc_channel.h uebernommen (Espressif-
 * eigene SoC-Headerdatei, keine Sekundaerquelle). Nur bei Pins eingetragen,
 * die in dieser Tabelle bereits PIN_ROLE_FREE sind - interne Pins (z.B.
 * G20-G23 waeren rein rechnerisch auch ADC-Kanaele) bleiben ADC-los, weil
 * ihre GPIO-Funktion bereits anderweitig fest belegt bzw. nicht verifiziert ist.
 *
 * PWM (LEDC) und Interrupt gelten auf ESP32-P4 fuer praktisch jeden freien GPIO
 * (GPIO-Matrix-Routing) - das ist eine allgemeine ESP32-Architektur-Eigenschaft,
 * keine Tab5-spezifische Annahme.
 */
#include "pin_table.h"
#include <string.h>

#define FREE_GPIO(num, lbl) \
    { (num), PIN_ROLE_FREE, (lbl), NULL, true, true, false, true, true, -1, -1 }

#define FREE_GPIO_ADC(num, lbl, unit, chan) \
    { (num), PIN_ROLE_FREE, (lbl), NULL, true, true, true, true, true, (unit), (chan) }

#define INTERNAL_GPIO(num, lbl, fn) \
    { (num), PIN_ROLE_INTERNAL, (lbl), (fn), false, false, false, false, false, -1, -1 }

// supports_input = true: hal_gpio_request() erlaubt fuer SHARED_BUS-Pins
// ausschliesslich HAL_GPIO_MODE_INPUT (siehe hal_gpio.c) - das ist der
// "nur lesend/diagnostisch"-Zugriff, den die Bus-Init (I2C-Treiber) braucht.
// Alle anderen Modi bleiben false, damit kein Modul den Bus umkonfigurieren kann.
#define SHARED_BUS_GPIO(num, lbl, fn) \
    { (num), PIN_ROLE_SHARED_BUS, (lbl), (fn), true, false, false, false, false, -1, -1 }

const pin_descriptor_t g_pin_table[] = {
    // --- SDIO2: ESP32-C6 Wireless-Co-Prozessor (Wi-Fi 6 / BLE / Thread / Zigbee) ---
    INTERNAL_GPIO(8,  "SDIO2 D3", "Wi-Fi-Modul (ESP32-C6) Anbindung"),
    INTERNAL_GPIO(9,  "SDIO2 D2", "Wi-Fi-Modul (ESP32-C6) Anbindung"),
    INTERNAL_GPIO(10, "SDIO2 D1", "Wi-Fi-Modul (ESP32-C6) Anbindung"),
    INTERNAL_GPIO(11, "SDIO2 D0", "Wi-Fi-Modul (ESP32-C6) Anbindung"),
    INTERNAL_GPIO(12, "SDIO2 CLK", "Wi-Fi-Modul (ESP32-C6) Anbindung"),
    INTERNAL_GPIO(13, "SDIO2 CMD", "Wi-Fi-Modul (ESP32-C6) Anbindung"),
    INTERNAL_GPIO(15, "SDIO2 RST", "Wi-Fi-Modul (ESP32-C6) Reset"),

    // --- RS485 (SIT3088) ---
    INTERNAL_GPIO(20, "RS485 TX", "RS485-Transceiver SIT3088"),
    INTERNAL_GPIO(21, "RS485 RX", "RS485-Transceiver SIT3088"),
    INTERNAL_GPIO(34, "RS485 DIR", "RS485-Transceiver SIT3088 (Richtungssteuerung)"),

    // --- Audio (ES8388 Codec + ES7210 Mic-Frontend, teils geteilte Leitungen) ---
    INTERNAL_GPIO(26, "I2S DOUT", "Audio-Codec ES8388 (Speaker-Datenausgang)"),
    INTERNAL_GPIO(27, "I2S SCLK", "Audio-Codec ES8388 / Mic-Frontend ES7210 (geteilt)"),
    INTERNAL_GPIO(28, "I2S DIN", "Mic-Frontend ES7210 (Mikrofon-Dateneingang)"),
    INTERNAL_GPIO(29, "I2S LRCK", "Audio-Codec ES8388 / Mic-Frontend ES7210 (geteilt)"),
    INTERNAL_GPIO(30, "I2S MCLK", "Audio-Codec ES8388 / Mic-Frontend ES7210 (geteilt)"),

    // --- System-I2C-Bus: Touch, Kamera, Audio-Codecs, IMU, RTC, Power-Monitor, IO-Expander ---
    SHARED_BUS_GPIO(31, "System I2C SDA",
        "Interner I2C-Bus (Touch 0x55, ES8388 0x10, ES7210 0x40, BMI270 0x68, "
        "RX8130CE 0x32, INA226 0x41, PI4IOE5V6408 0x43/0x44) + Kamera CAM_SDA"),
    SHARED_BUS_GPIO(32, "System I2C SCL",
        "Interner I2C-Bus (siehe SDA) + Kamera CAM_SCL"),

    // --- Kamera (SC2356, MIPI-CSI) ---
    INTERNAL_GPIO(36, "CAM_MCLK", "Kamerasensor SC2356 Takteingang"),

    // --- Display-Backlight (verifiziert aus M5Tab5-UserDemo BSP: BSP_LCD_BACKLIGHT) ---
    INTERNAL_GPIO(22, "LCD_BACKLIGHT", "Display-Hintergrundbeleuchtung (LEDC-PWM)"),
    // Hinweis: dedizierte CSI-Diffpaare (CAM_D1P/N, CAM_CSI_CKP/N, CSI_DOP/N) sind
    // keine normalen GPIOs und daher nicht in dieser Tabelle enthalten.

    // --- microSD (SPI-Datenleitungen + SDIO-Takt/Kommando) ---
    INTERNAL_GPIO(39, "SD D0 / MISO", "microSD-Slot"),
    INTERNAL_GPIO(40, "SD D1 / CS",   "microSD-Slot"),
    INTERNAL_GPIO(41, "SD D2 / SCK",  "microSD-Slot"),
    INTERNAL_GPIO(42, "SD D3 / MOSI", "microSD-Slot"),
    INTERNAL_GPIO(43, "SD CLK (SDIO-Modus)", "microSD-Slot"),
    INTERNAL_GPIO(44, "SD CMD (SDIO-Modus)", "microSD-Slot"),

    // --- Port A (HY2.0-4P, Grove-kompatibel) - externer I2C-Bus, frei nutzbar ---
    // G53/G54 sind gleichzeitig ADC2 CH4/CH5 (adc_channel.h) - Doppelnutzung
    // (I2C ODER ADC) wird durch die Reservierungslogik in hal_gpio.c erzwungen.
    FREE_GPIO_ADC(53, "Port A SDA / EX_I2C_SDA", 2, 4),
    FREE_GPIO_ADC(54, "Port A SCL / EX_I2C_SCL", 2, 5),

    // --- Port B / Port C (weitere HY2.0-Anschluesse, Teilmenge des M-Bus) ---
    FREE_GPIO_ADC(17, "Port B Pin1", 1, 1),
    FREE_GPIO_ADC(52, "Port B Pin2", 2, 3),
    FREE_GPIO(7,  "Port C Pin1"),
    FREE_GPIO(6,  "Port C Pin2"),

    // --- M-Bus (30-polig) - weitere freie GPIOs laut M5Unified-Quellcode ---
    // Quelle: m5stack/M5Unified, src/M5Unified.cpp, Tab5-Boardtabelle.
    // TODO: gegen Schematic-PDF (docs.m5stack.com/en/products/sku/K145)
    // gegenpruefen, bevor hohe Stroeme/kritische Anwendungen darauf laufen.
    FREE_GPIO(2,  "M-Bus GPIO"),
    FREE_GPIO(3,  "M-Bus GPIO"),
    FREE_GPIO(4,  "M-Bus GPIO"),
    FREE_GPIO(5,  "M-Bus GPIO"),
    FREE_GPIO_ADC(16, "M-Bus GPIO", 1, 0),
    FREE_GPIO_ADC(18, "M-Bus GPIO", 1, 2),
    FREE_GPIO_ADC(19, "M-Bus GPIO", 1, 3),
    FREE_GPIO(35, "M-Bus GPIO"),
    FREE_GPIO(37, "M-Bus GPIO"),
    FREE_GPIO(38, "M-Bus GPIO"),
    FREE_GPIO(45, "M-Bus GPIO"),
    FREE_GPIO(47, "M-Bus GPIO"),
    FREE_GPIO_ADC(51, "M-Bus GPIO", 2, 2),
};

const size_t g_pin_table_count = sizeof(g_pin_table) / sizeof(g_pin_table[0]);

const pin_descriptor_t *pin_table_find(int gpio_num)
{
    for (size_t i = 0; i < g_pin_table_count; i++) {
        if (g_pin_table[i].gpio_num == gpio_num) {
            return &g_pin_table[i];
        }
    }
    return NULL;
}
