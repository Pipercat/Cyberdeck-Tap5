# M5Stack Tab5 - Pin-Tabelle (kompakt)

Generiert aus `components/core/hardware/pin_table.c` (Stand Phase 1). Bei
Aenderungen an der Pin-Tabelle diese Datei von Hand nachziehen (Phase 2+:
kleines Build-Skript, das diese Tabelle direkt aus pin_table.c erzeugt).

## Intern belegt (gesperrt, PIN_ROLE_INTERNAL)

| GPIO | Funktion |
|---|---|
| 8-13, 15 | SDIO2 - Wi-Fi-Modul (ESP32-C6) |
| 20, 21, 34 | RS485 (SIT3088): TX, RX, DIR |
| 26 | I2S DOUT (Audio-Codec ES8388) |
| 27 | I2S SCLK (Audio/Mic, geteilt) |
| 28 | I2S DIN (Mic-Frontend ES7210) |
| 29 | I2S LRCK (Audio/Mic, geteilt) |
| 30 | I2S MCLK (Audio/Mic, geteilt) |
| 36 | CAM_MCLK (Kamera SC2356) |
| 39-42 | microSD SPI-Datenleitungen |
| 43, 44 | microSD SDIO CLK/CMD |
| 22 | LCD-Backlight (LEDC-PWM), verifiziert aus M5Tab5-UserDemo BSP |

## Interner Systembus (PIN_ROLE_SHARED_BUS, nur lesend)

| GPIO | Funktion |
|---|---|
| 31 | System I2C SDA (Touch 0x55, ES8388 0x10, ES7210 0x40, BMI270 0x68, RX8130CE 0x32, INA226 0x41, IO-Expander 0x43/0x44) + Kamera SDA |
| 32 | System I2C SCL (siehe SDA) + Kamera SCL |

## Frei nutzbar (PIN_ROLE_FREE)

| GPIO | Bezeichnung |
|---|---|
| 53 | Port A SDA (HY2.0-4P, externer I2C) |
| 54 | Port A SCL (HY2.0-4P, externer I2C) |
| 17 | Port B Pin1 |
| 52 | Port B Pin2 |
| 7  | Port C Pin1 |
| 6  | Port C Pin2 |
| 2, 3, 4, 5, 16, 18, 19, 35, 37, 38, 45, 47, 48, 51 | M-Bus GPIO (Quelle: M5Unified-Quellcode, vor kritischem Einsatz gegen Schematic pruefen) |

## Nicht verifiziert (PIN_ROLE_UNVERIFIED, gesperrt bis Bestaetigung)

- GPIO_EXT-Header (Pinbelegung unbekannt)
- Alle GPIO-Nummern, die nicht in einer der obigen Tabellen stehen

Siehe `docs/hardware_reference.md` fuer Quellenangaben und offene Punkte.
