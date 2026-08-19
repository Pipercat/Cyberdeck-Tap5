#include "sensors_module.h"
#include "board_init.h"
#include "driver/i2c_master.h"
#include <string.h>

#define RX8130_ADDR    0x32
#define RX8130_REG_SEC 0x10  // 7 aufeinanderfolgende Register: SEC,MIN,HOUR,WDAY,MDAY,MONTH,YEAR

static uint8_t bcd2dec(uint8_t val)
{
    return (uint8_t)((val >> 4) * 10 + (val & 0x0f));
}

esp_err_t sensors_module_read_rtc(sensors_module_rtc_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    i2c_master_bus_handle_t bus = board_get_system_i2c_bus();
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RX8130_ADDR,
        .scl_speed_hz = 400000,
    };
    i2c_master_dev_handle_t dev;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) != ESP_OK) {
        return ESP_FAIL;
    }

    uint8_t reg = RX8130_REG_SEC;
    uint8_t raw[7] = {0};
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, raw, sizeof(raw), 200);
    i2c_master_bus_rm_device(dev);
    if (err != ESP_OK) {
        return err;
    }

    out->second = bcd2dec(raw[0] & 0x7f);
    out->minute = bcd2dec(raw[1] & 0x7f);
    out->hour   = bcd2dec(raw[2] & 0x3f);  // 24h-Register, siehe RX8130-Datenblatt
    // raw[3] (WDAY) wird bewusst nicht ausgewertet: die M5Stack-Referenz
    // wendet BCD-Dekodierung auf ein One-Hot-Bitmask-Register an, was fuer
    // Werte ab Donnerstag falsche Ergebnisse liefert - lieber weglassen als
    // einen falschen Wochentag anzeigen.
    out->day    = bcd2dec(raw[4] & 0x3f);
    out->month  = bcd2dec(raw[5] & 0x1f);
    out->year   = 2000 + bcd2dec(raw[6]);

    // Auf echter Hardware beobachtet: der I2C-Read gelingt (Chip antwortet),
    // aber liefert einen ungueltigen Tag (0) - die Uhr wurde offenbar noch
    // nie gestellt (Werksreset-/Erstinbetriebnahme-Zustand). Das ist ein
    // reales Chip-Ergebnis, kein Uebertragungsfehler - trotzdem nicht als
    // "aktuelle Zeit" ausgeben, sonst zeigt die UI ein unsinniges Datum an.
    if (out->day < 1 || out->day > 31 || out->month < 1 || out->month > 12) {
        out->ok = false;
        return ESP_OK;
    }

    out->ok = true;
    return ESP_OK;
}
