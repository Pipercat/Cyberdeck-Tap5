# CyberDeck Tab5

Professionelles Embedded-Engineering-Werkzeug fuer den M5Stack Tab5 (ESP32-P4).
Siehe `docs/hardware_reference.md` und `docs/pinout.md` fuer den
Hardware-Verifikationsstand.

## Status

**Phase 1 (Foundation) und Phase 2 (Engineering Basics) abgeschlossen, Phase 3
(Media) teilweise abgeschlossen, Phase 4 (Netzwerk) Wi-Fi-Grundlagen
abgeschlossen** - alles auf echter Hardware geflasht und gebootet verifiziert
(2026-08-19). Ausserdem: Settings-Screen (Backlight, Werksreset) und mehrere
UI-Layout-Bugs behoben (Statusleiste verdeckte Titel/Zurueck-Button auf den
Unterseiten - siehe Git-Historie fuer Details zur eigentlichen Ursache).

Phase 1: Projektgeruest, verifizierte Pin-HAL, Display/Touch/LVGL-Bring-up,
Dashboard mit dynamischem Kachel-Grid, Navigation, Dark-Theme, Settings (NVS),
Logging, Self-Test-Grundgeruest.

Phase 2: **GPIO** (Input/Input-PU/Input-PD/Output/PWM/ADC je Pin, Detail-Modal),
**PWM/Servo** (Pulsbreiten-Steuerung, Sweep, generischer LED/PWM-Modus),
**ADC/Mini-Oszilloskop** (Live-Chart, Min/Max/Avg, Sample-Rate), **I2C-Scanner**
(Port A, Adress-Heuristik, Register-Read/Write), **Serial-Terminal** (UART auf
freien Pins, Baudrate, ASCII/HEX, Send/Clear, On-Screen-Tastatur),
**System-Monitor** (RAM/PSRAM/Flash/Chip-Temperatur/Akku-Spannung/Laufzeit,
Logs-Ansicht, Self-Test-Ausfuehrung, Neustart mit Bestaetigung). Alle Module
greifen ausschliesslich ueber die HAL (`hal_gpio`/`hal_pwm`/`hal_adc`) auf
Hardware zu.

Phase 3: **Audio** (Mikrofon-Pegelmesser mit relativem dB-Balken, Lautsprecher-
Testton mit Frequenz-/Lautstaerkeregler, Sweep 100Hz-10kHz). Init-Sequenz
(I2S STD/TDM, ES8388/ES7210-Codec-Setup) 1:1 aus der offiziellen
M5Stack-Werksfirmware uebernommen, siehe `docs/hardware_reference.md`.

**Kamera bewusst zurueckgestellt**: es gibt keine verifizierte
Referenzimplementierung fuer Live-View auf diesem Board (M5Stacks eigene
Firmware bindet die esp_video/esp_cam_sensor-Komponenten zwar ein, nutzt sie
aber nirgends). Live-View braucht zusaetzlich die ISP-Pipeline
(RAW-Bayer-Sensor SC2336 -> RGB) zur Farbkonvertierung, die ohne verifizierte
Referenz nur geraten werden koennte - der Nutzer hat sich explizit dagegen
entschieden, das zu riskieren, und die Kamera fuer eine spaetere, gezielte
Session mit mehr Recherchezeit zurueckgestellt.

Phase 4 (bisher): **Wi-Fi** (STA-Scan/Connect/Disconnect, Status inkl. RSSI/
IP in der Statusleiste). Das Funkmodul sitzt auf einem separaten ESP32-C6-
Co-Prozessor (nicht dem Haupt-SoC), angebunden per SDIO2 - Architektur
(esp_hosted + esp_wifi_remote) und Pin-/Kconfig-Werte 1:1 aus der offiziellen
Werksfirmware uebernommen und auf echter Hardware verifiziert (SDIO-Handshake
mit dem C6 funktioniert, ~2.3s, kein Hang). Details und Boot-Log-Auszug in
`docs/hardware_reference.md`. Passwoerter werden bewusst nicht persistiert
(NVS-Verschluesselung ist noch nicht aktiviert).

Phase 6 (bisher): **Quick Actions** (Sprungleiste auf dem Dashboard zu
Serial/GPIO/I2C/Audio/Network), **Self-Test** um einen Wi-Fi/C6-Link-Check
erweitert, **Sensors** (RTC RX8130CE - echtes Register-Readout; IMU BMI270
bewusst nicht implementiert, siehe `docs/hardware_reference.md` fuer die
Begruendung - braucht einen ~30k-Zeilen-Firmware-Blob-Upload).

Auf dem angeschlossenen Geraet erfolgreich geflasht und gebootet: Panel-Typ
ST7121 automatisch erkannt, Touch aktiv, Dashboard und alle Phase-2/3-Module
laufen ohne Absturz. Display laeuft in physischer Portrait-Ausrichtung
(720x1280) - eine Landscape-Rotation wurde mehrfach versucht und wieder
verworfen (siehe `docs/hardware_reference.md` fuer Details und warum). Details
zu allen gefundenen/behobenen Hardware-Ueberraschungen (IO-Expander-Reset,
Backlight-GPIO, PSRAM-Takt, Panel-Erkennung, echter Sensorname SC2336 statt
SC2356) ebenfalls dort.

## Build

ESP-IDF v5.4.1 ist bereits unter `~/esp/esp-idf` installiert (Ziel: `esp32p4`).

```bash
source ~/esp/esp-idf/export.sh
cd ~/Cyberdeck_Tap5
idf.py build
```

## Flashen auf echte Hardware

1. Tab5 per USB-C mit dem Rechner verbinden (Boot-/Download-Modus: Tab5
   ausgeschaltet, dann Power-Taste ~2s gedrueckt halten bis der Rechner das
   Geraet als seriellen Port erkennt - genaues Vorgehen kann je nach
   Firmware-Stand variieren, siehe M5Stack-Doku falls `idf.py flash` den Port
   nicht automatisch findet).
2. Port ermitteln (macOS: `ls /dev/cu.usb*`).
3. Flashen:

```bash
idf.py -p /dev/cu.usbXXXXX flash monitor
```

`monitor` oeffnet direkt die serielle Konsole (Beenden mit `Ctrl+]`).

## Erwartetes Ergebnis auf dem Geraet

- Display zeigt das Dashboard (Portrait, 720x1280) mit Statusleiste oben
  (Platzhalterwerte) und einem dynamischen Kachel-Grid, Backlight an (80%).
- Touch auf GPIO/PWM/ADC/I2C Scanner/Serial/System/Audio/Network/Settings/
  Sensors oeffnet das jeweils funktionsfaehige Modul; die restlichen Kacheln
  (Flash, SPI, Camera, Files, Projects) zeigen weiterhin "Coming Soon".

**Bekannte offene Punkte:**
- Geraete mit dem aelteren ILI9881C+GT911-Panel ("Gen1", vor ca. Oktober 2025)
  werden zur Laufzeit erkannt, aber deren Treiberpfad ist noch nicht
  implementiert - `board_init()` bricht dann mit einer klaren Log-Meldung ab.
- Landscape-Darstellung ist nicht implementiert (Display laeuft nativ im
  Portrait-Modus) - siehe `docs/hardware_reference.md`.
- SD-Karte ist noch nicht eingebunden; Akku-Anzeige im System-Monitor zeigt
  nur die reale Spannung (INA226-Busspannung), kein kalibrierter Strom/Prozentwert.

## Naechste Phasen

Siehe Implementierungsplan (`/Users/marvin/.claude/plans/proud-hatching-spring.md`):
Kamera (zurueckgestellt, siehe oben), Rest von Phase 4 (Server-API/File-Browser),
Phase 5 (Flashing externer ESP32-Boards), Phase 6 (Projects/Testsequenzen/Diagnostics).

## Push-Test

✅ GitHub-Push von ChatGPT erfolgreich getestet am **19.08.2026**.
