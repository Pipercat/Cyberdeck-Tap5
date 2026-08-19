# M5Stack Tab5 - Hardware-Referenz (Phase 1-3)

Diese Datei fasst den Verifikationsstand der Tab5-Hardware zusammen, wie er
in `components/core/hardware/pin_table.c` (die tatsaechliche Quelle der
Wahrheit fuer Software) umgesetzt ist. Siehe auch `docs/pinout.md` fuer die
kompakte Pin-Tabelle.

## Quellenlage

| Bereich | Quelle | Vertrauensstufe |
|---|---|---|
| SoC, Speicher, Display, Kamera, Audio, Sensoren, Power | docs.m5stack.com/en/core/Tab5 (offizielle Doku) | Hoch |
| SDIO2/Audio/I2C/SD/Kamera-Pinnummern | wie oben, per WebFetch extrahiert | Hoch |
| Port A/B/C + M-Bus-GPIOs | m5stack/M5Unified Quellcode (offene HAL von M5Stack), `src/M5Unified.cpp` | Mittel-Hoch (Quellcode-basiert, aber nicht Zeile-fuer-Zeile im Original gegengelesen) |
| ESP32-P4 SoC-Fakten (SRAM-Groesse, Temp-Sensor, USB-Host-Limitation, GPIO-Treiberstaerke) | Espressif ESP32-P4 Datasheet / ESP-IDF-Doku | Hoch |
| GPIO_EXT-Header, exakte PI4IOE5V6408-Bitbelegung | Keine Primaerquelle gefunden | **Nicht verifiziert - gesperrt** |
| Display-/Touch-Controller-Bezeichnung | uneinheitlich "ST7121" vs. "ST7123" in Sekundaerquellen | Siehe Hinweis unten |

## Display-/Touch-Controller: geklärt (Stand 2026-08-19, auf echter Hardware verifiziert)

Es gibt tatsaechlich **mehrere Panel-Varianten**, keine Namensverwechslung in
Sekundaerquellen: Tab5-Geraete tragen entweder den ST7121- oder den
ST7123-Controller (oder, bei sehr alten Einheiten, ILI9881C+GT911 getrennt).
`board_init.c` erkennt die Variante zur Laufzeit per I2C-Sondierung (Adresse
0x55, Firmware-Versions-Register 0x0000: Wert 1 = ST7121, Wert 3 = ST7123)
und initialisiert den passenden Treiber (`esp_lcd_st7121` bzw.
`esp_lcd_st7123`, beide offizielle Espressif-Pakete). Quelle der exakten
Init-Sequenz, DSI-Timing-Werte und Erkennungslogik: das offizielle
M5Stack-Werksfirmware-Repository `github.com/m5stack/M5Tab5-UserDemo`.

Das getestete Geraet hat einen **ST7121**-Controller; Touch lief ueber
`esp_lcd_touch_st7123` (dieselbe I2C-Touch-Komponente wird laut Referenz fuer
beide Panel-Varianten verwendet).

## Wichtige, auf echter Hardware gefundene Korrekturen gegenueber der ersten Annahme

1. **Display-/Touch-Reset laeuft ueber die IO-Expander, nicht ueber ein
   Host-GPIO.** Ohne Freigabe blieb `esp_lcd_panel_init()` unendlich haengen
   (Task-Watchdog-Reboot). Fix: `io_expander.c` initialisiert beide
   PI4IOE5V6408 (0x43/0x44) mit der exakten Register-/Bitfolge aus der
   M5Stack-Werksfirmware und setzt P4 (LCD_RST), P5 (TP_RST), P6 (CAM_RST),
   P1 (SPK_EN), P2 (EXT5V_EN) auf 0x43 sowie P0 (WLAN_PWR_EN), P3
   (USB5V_EN) auf 0x44 auf High. Nach der Freigabe ist eine Wartezeit
   (300 ms) noetig, bis der Touch-Controller am I2C-Bus antwortet.
2. **LCD-Backlight ist ein echtes ESP32-P4-GPIO, nicht IO-Expander-basiert:**
   GPIO 22, angesteuert per LEDC-PWM (verifiziert aus `BSP_LCD_BACKLIGHT` in
   der M5Stack-Werksfirmware). `pin_table.c` wurde entsprechend ergaenzt
   (`PIN_ROLE_INTERNAL`); `board_init.c` konfiguriert es direkt (kein
   `hal_gpio_request()`, da board_init der legitime Systembesitzer ist -
   dieser Gate ist fuer Fach-Module gedacht, nicht fuer das Board selbst).
3. **PSRAM muss mit 200 MHz laufen** (`CONFIG_SPIRAM_SPEED_200M`,
   erfordert `CONFIG_IDF_EXPERIMENTAL_FEATURES=y`), sonst kommt das
   MIPI-DSI-Display beim Auslesen des Framebuffers aus dem PSRAM nicht
   hinterher ("can't fetch data from external memory fast enough, underrun
   happens"). Die anfaengliche Entscheidung, das Experimental-Flag aus
   Stabilitaetsgruenden wegzulassen, war falsch - bei diesem Gerät ist
   200 MHz PSRAM keine Performance-Kür, sondern Voraussetzung fuer ein
   funktionierendes Display. Bestaetigt durch die Konfiguration der
   offiziellen M5Stack-Werksfirmware.
4. **Panel-Raster ist physisch Portrait (720×1280)**, nicht Landscape
   (1280×720) wie die Produktangabe suggeriert - das Tab5-Gehaeuse montiert
   das Panel gedreht. `board_init.c` betreibt das Display bewusst in dieser
   nativen Portrait-Ausrichtung; die UI (Dashboard/Nav/Statusbar) nutzt
   LV_PCT()-basiertes Layout und passt sich automatisch an.

   **Landscape-Rotation wurde auf echter Hardware mehrfach versucht und
   wieder verworfen** (nicht nur theoretisch verworfen - tatsaechlich
   geflasht und am Geraet beobachtet):
   - `lv_disp_set_rotation(..., LV_DISPLAY_ROTATION_90)` mit `sw_rotate=true`
     im PARTIAL-Refresh-Modus: Inhalt erschien nur in einem Teilbereich des
     Screens, falsch orientiert (grosse Freiflaeche blieb ungezeichnet).
   - Wechsel auf `ROTATION_270`: identisches Fehlbild - kein Hinweis auf ein
     reines Richtungsproblem.
   - `full_refresh=true` (um das PARTIAL-Problem zu umgehen): Absturz
     `xQueueSemaphoreTake ... pxQueue` - esp_lvgl_port wartet in diesem Modus
     unconditional auf ein `trans_sem`, das nur bei `avoid_tearing=true`
     angelegt wird (Code-Fund in `managed_components/espressif__esp_lvgl_port`).
   - `avoid_tearing=true` dazu aktiviert: neuer Fehler
     `esp_lcd_dpi_panel_get_frame_buffer(390): invalid frame buffer number`,
     weil dieser Modus 2 Panel-Framebuffer braucht (`num_fbs=2`), aber
     `num_fbs=1` konfiguriert war.
   - `num_fbs=2` gesetzt: Watchdog-Timeout/Haenger - der erwartete
     `on_refresh_done`-Callback, der `trans_sem` freigibt, feuert nicht wie
     im Code vorgesehen.

   Nach vier gescheiterten Iterationen auf echter Hardware wurde die
   Rotation komplett entfernt statt weiter zu raten. Fuer eine spaetere
   Wiederaufnahme: vermutlich ist PPA-basierte Hardware-Rotation
   (`CONFIG_LVGL_PORT_ENABLE_PPA`, aktuell deaktiviert) der robustere Weg
   als die reine Software-Rotation von esp_lvgl_port.

## Audio-Bring-up (Phase 3, auf echter Hardware geflasht - Speaker/Mic-
Funktionstest durch den Nutzer noch ausstehend)

I2S-Konfiguration und Codec-Init 1:1 aus der M5Stack-Werksfirmware uebernommen
(`m5stack_tab5.c`, `bsp_audio_init()`/`bsp_audio_codec_speaker_init()`/
`bsp_audio_codec_microphone_init()`), umgesetzt in `audio_module.c`:
- TX (Lautsprecher, ES8388): I2S STD-Modus, Mono, 16 Bit, 48 kHz.
- RX (Mikrofon, ES7210): I2S TDM-Modus, 4 Slots (ES7210 ist ein 4-Kanal-ADC-
  Frontend, auch wenn nur 2 Mikrofone bestueckt sind).
- I2C-Adressen `ES8388_CODEC_DEFAULT_ADDR` (0x20) und `ES7210_CODEC_DEFAULT_ADDR`
  (0x80) sind 8-Bit-Formate (bereits um 1 Bit verschoben) - NICHT identisch mit
  den 7-Bit-Werten 0x10/0x40 aus der allgemeinen Hardwaredoku weiter oben.
  Beide bezeichnen dieselben physischen Chips, nur unterschiedliche
  Adressnotation - `esp_codec_dev`'s eigene Header-Makros wurden 1:1
  uebernommen statt eigene Werte einzusetzen.
- Lautsprecher-Enable (SPK_EN) laeuft ueber PI4IOE1 P1 (`pa_pin = -1` im
  Codec-Config, da bereits in `io_expander.c` seit Phase 1 auf High gesetzt).

## Kamera: bewusst zurueckgestellt (Nutzerentscheidung 2026-08-19)

- **Sensor-Bezeichnung korrigiert**: Die allgemeine Hardwaredoku (Abschnitt
  oben) nennt "SC2356" - das ist vermutlich ein Tippfehler in M5Stacks eigener
  Produktdokumentation. Die offizielle Werksfirmware (`CONFIG_CAMERA_SC2336`
  in den mitgelieferten esp_video-Beispiel-Konfigurationen) verwendet
  durchgaengig **SC2336**. Kein SC2356-Treiber existiert im esp_cam_sensor-
  Repository, ein SC2336-Treiber schon.
- **Kein verifizierbarer Live-View-Pfad gefunden**: `esp_video`/
  `esp_cam_sensor` sind in der Werksfirmware als Abhaengigkeiten vorhanden,
  werden aber im Board- oder App-Code nirgends tatsaechlich initialisiert
  oder aufgerufen - es gibt keine lauffaehige Referenz fuer dieses Board.
  Live-View braeuchte zusaetzlich die ISP-Pipeline (RAW-Bayer -> RGB), deren
  Konfiguration ohne Referenz nur geraten werden koennte.
- **Was verifiziert ist**: SCCB (I2C) laeuft ueber den internen System-I2C-
  Bus (`esp_video_init_sccb_config_t.init_sccb = false` +
  `i2c_handle = board_get_system_i2c_bus()`, statt eine zweite I2C-Instanz auf
  denselben Pins zu erzeugen). Kamera-Reset laeuft wie Display/Touch ueber
  PI4IOE1 P6 (bereits in `io_expander.c` freigegeben) - `reset_pin`/`pwdn_pin`
  in der esp_video-Konfiguration koennen auf -1 (kein Host-GPIO) stehen.
- **Empfehlung fuer eine spaetere Session**: mit `idf.py create-project` einen
  Minimal-Testaufbau aus `esp_video/examples/capture_stream` isoliert auf
  echter Hardware zum Laufen bringen (V4L2 QUERYCAP/REQBUFS/QBUF/STREAMON/
  DQBUF-Zyklus, siehe dortige `capture_stream_main.c`), BEVOR das in dieses
  Projekt integriert wird - deutlich schneller iterierbar als im vollen App-
  Kontext.

## Nicht verifizierte/gesperrte Bereiche (bewusst, siehe pin_table.h)

- **GPIO_EXT-Header**: keine Pinbelegung in verfuegbaren Quellen gefunden.
- **Interrupt-Pin fuer Touch**: nicht als separates Host-GPIO identifiziert -
  Touch laeuft aktuell im Polling-Modus statt interruptgetrieben (siehe
  `board_init.c`-Kommentare).

## Naechste Schritte zur vollstaendigen Verifikation

1. Offizielles Schematic-PDF laden: docs.m5stack.com/en/products/sku/K145
2. M-Bus- und GPIO_EXT-Pinliste gegen das Schematic gegenpruefen
3. ESP32-P4-Datenblatt Kapitel 5.1/5.4 (Absolute Maximum Ratings, DC
   Characteristics) fuer die exakten Strom-/Spannungsgrenzwerte pruefen
4. Gen1-Panel-Pfad (ILI9881C+GT911) implementieren, falls ein Nutzer ein
   aelteres Geraet hat (`board_init.c` erkennt die Variante bereits und
   bricht dort aktuell kontrolliert mit einer klaren Fehlermeldung ab)

Erledigt (2026-08-19, auf echter Hardware verifiziert): Display-/Touch-
Bring-up (ST7121+ST7123-Erkennung), IO-Expander-Reset-Sequenz, Backlight-GPIO,
PSRAM-Taktrate. Details siehe Abschnitt oben.

Bis Punkt 1-2 erledigt sind, bleiben die betroffenen M-Bus/GPIO_EXT-Pins in
`pin_table.c` auf `PIN_ROLE_UNVERIFIED` und sind damit ueber `hal_gpio.c`
automatisch fuer alle Module gesperrt.
