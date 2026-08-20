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

## Wi-Fi-Bring-up (Phase 4, auf echter Hardware geflasht und verifiziert 2026-08-19)

- Das Wi-Fi-Radio sitzt NICHT auf dem ESP32-P4, sondern auf dem separaten
  ESP32-C6-MINI-1U-Co-Prozessor, angebunden per SDIO2. Architektur (esp_hosted
  + esp_wifi_remote, transparenter `esp_wifi.h`-Ersatz) und exakte Pin-/
  Kconfig-Werte stammen aus der generierten `sdkconfig` der offiziellen
  M5Tab5-UserDemo-Werksfirmware (`platforms/tab5/`), nicht geraten - siehe
  `components/core/network/idf_component.yml` (Versionen exakt gepinnt:
  `esp_hosted 1.4.0`, `esp_wifi_remote 0.8.5`) und `sdkconfig.defaults`.
- **Auf echter Hardware verifiziert**: `wifi_module_init()` (STA-Modus) laeuft
  sauber durch - SDIO-Handshake mit dem C6 (GPIO12 CLK/13 CMD/11 D0/10 D1/9
  D2/8 D3, GPIO15 Reset, 4-Bit-Bus, 40MHz) dauert ca. 2.3s, danach
  `wifi_module_init() -> ESP_OK`, kein Hang, Boot laeuft normal bis zum
  Dashboard weiter. Der C6 hatte bereits werkseitig eine ESP-Hosted-
  Slave-Firmware geladen (Boot-Log: "Received INIT event from ESP32
  peripheral", Capabilities inkl. WLAN + BLE via HCI-over-SDIO) - auf diesem
  konkreten Geraet musste also KEIN separates C6-Firmware-Image geflasht
  werden (anders als in der M5Tab5-UserDemo-Doku als moeglicher Schritt
  beschrieben, siehe `platforms/tab5/wifi_c6_fw/flash.sh` dort).
- **Bewusst lazy statt beim Boot**: `wifi_module_init()` wird erst beim ersten
  Oeffnen des Network-Screens aufgerufen (`network_on_show()`), nicht in
  `app_main()` - SDIO-Bring-up zu einem zweiten Chip ist eine nicht-triviale
  Hardware-Operation, die nicht bei jedem Boot noetig ist, wenn der Nutzer
  Wi-Fi gar nicht braucht. Kurzzeitig testweise eager (in `app_main()`)
  aufgerufen, um genau dieses Boot-Log auf echter Hardware zu erfassen, dann
  wieder auf lazy zurueckgesetzt.
- **Nicht implementiert**: Passwort-Persistierung. `settings_t.wifi_ssid` wird
  nach erfolgreicher Verbindung gespeichert (Komfort: Feld vorausgefuellt),
  das Passwort bewusst nicht - NVS-Verschluesselung ist noch nicht aktiviert
  (siehe `settings.h`-Kommentar), ein Klartext-Passwort in der Settings-Blob
  waere unsicher. Muss nach jedem Boot neu eingegeben werden.

## Sensoren-Bring-up (Phase 6, auf echter Hardware geflasht und verifiziert 2026-08-19)

- **RTC (RX8130CE, I2C 0x32)**: Register-Layout (SEC=0x10...YEAR=0x16, BCD-
  kodiert, 24h-Stundenregister) 1:1 aus der offiziellen M5Tab5-UserDemo-
  Firmware uebernommen (`hal/utils/rx8130/rx8130.cpp`), nicht aus dem
  allgemeinen Datenblatt geraten. Auf echter Hardware verifiziert: Chip
  antwortet auf I2C (`i2c_master_transmit_receive` liefert `ESP_OK`), aber
  liefert ein **ungueltiges Datum** (Tag=0, Jahr=2002) - die Uhr wurde auf
  diesem konkreten Geraet offenbar noch nie gestellt. Das ist ein echtes
  Chip-Ergebnis, kein Uebertragungsfehler; die Software erkennt das (Tag/
  Monat-Plausibilitaetspruefung) und zeigt "RTC nicht gestellt" statt eines
  unsinnigen Datums - keine erfundene "aktuelle Zeit" wird angezeigt. Eine
  Set-Time-Funktion (z.B. nach NTP-Sync ueber Wi-Fi) ist nicht implementiert,
  waere aber ein naheliegender naechster Schritt.
- **IMU (BMI270)**: bewusst NICHT implementiert. Der Chip braucht zwingend
  einen ueber I2C hochgeladenen Firmware-Konfigurations-Blob (Bosch BMI2
  SensorAPI, `platforms/tab5/components/sensor_bmi270/` in der Referenz-
  Firmware, ~30.000 Zeilen Vendor-Code) fuer jegliche Datenausgabe - anders
  als bei RX8130/INA226 funktioniert hier kein minimaler Register-Read ohne
  dieses Init. Das Vendoring dieses Umfangs wuerde eine eigene, sorgfaeltig
  gepruefte Session brauchen statt einer unvollstaendigen/riskanten
  Teilintegration - siehe `components/modules/sensors/sensors_module.h` fuer
  die Begruendung im Code.

## microSD-Karte: bekannter Boot-Crash, zurueckgestellt (2026-08-19)

Ein erster Versuch, microSD-Unterstuetzung hinzuzufuegen (SDMMC 4-Bit-Modus,
Pins/LDO-Kanal 1:1 aus der M5Tab5-UserDemo-Firmware uebernommen, siehe
`components/m5stack_tab5/m5stack_tab5.c` dort: GPIO39-44, LDO-Kanal 4 fuer
die SDMMC-IO-Spannung) fuehrte auf echter Hardware zu einem **reproduzierbaren
Boot-Crash-Loop**:

```
assert failed: xTaskCreateStaticPinnedToCore
freertos_tasks_c_additions.h:300 (xPortCheckValidTCBMem(pxTaskBuffer))
```

Wichtig: der Crash passiert INNERHALB von esp_hosteds eigener frueher
Task-Erzeugung (waehrend `H_API: ESP-Hosted starting`), **bevor** `app_main()`
ueberhaupt laeuft - nicht in eigenem Code. Nach vollstaendigem
`idf.py fullclean` weiterhin reproduzierbar (kein Stale-Build-Artefakt).
Vermutung: Konflikt zwischen dem SDMMC/FATFS-Stack (`fatfs`,
`esp_driver_sdmmc`, `sdmmc`, `vfs`) und esp_hosteds eigenem SDIO-basiertem
Wi-Fi-Transport (beide nutzen SDMMC/SDIO-nahe Peripherie auf dem ESP32-P4) -
nicht abschliessend verifiziert.

Der betroffene Code (`core/storage/sd_module.c`, `modules/files/`) wurde
**bewusst nicht auf `main` gemerged** (main muss immer sauber booten) und
liegt stattdessen auf dem Branch `wip/sd-card-crash-investigation` fuer eine
spaetere, gezielte Debugging-Session (Bisektion: nur fatfs+esp_driver_sdmmc
linken ohne eigenen Code, um zu pruefen ob es ein reiner Linking-Konflikt
ist; ESP-IDF/esp_hosted-Issue-Tracker nach bekannten Interaktionsproblemen
durchsuchen).

### Update (2026-08-20): derselbe Crash auch mit SPIFFS statt SD - Vermutung praezisiert

Ein Versuch, Rest-Phase-4 (Server-API/File-Browser) **ohne SD-Karte** zu
implementieren - SPIFFS auf der internen `storage`-Flash-Partition
(`esp_vfs_spiffs_register`) statt SDMMC, um die Sensor-Analogie oben zu
vermeiden - loeste auf echter Hardware **denselben Crash** aus:

```
assert failed: xTaskCreateStaticPinnedToCore
freertos_tasks_c_additions.h:299 (xPortcheckValidStackMem(puxStackBuffer))
```

Ebenfalls reproduzierbar bei jedem Boot, ebenfalls INNERHALB von esp_hosteds
frueher Task-Erzeugung, bevor `app_main()` laeuft (Crash direkt nach
`transport: Add ESP-Hosted channel IF[2]` im Log). Der einzige linkende
Unterschied zu einem sauber bootenden Build: das `spiffs`-Komponenten-
REQUIRES zieht `vfs` mit rein (fuer `esp_vfs_spiffs_register`/POSIX-
Dateizugriff) - **kein** `fatfs`/`esp_driver_sdmmc`/`sdmmc` im Spiel.

Das praezisiert die obige Vermutung: der Konflikt haengt vermutlich nicht am
SDMMC-Peripherietreiber selbst, sondern generell daran, dass **irgendeine
Komponente `vfs` aktiviert**, waehrend esp_hosted laeuft - ob ueber SD/FATFS
oder SPIFFS gebraucht. Naechster Bisektionsschritt fuer die spaetere Session:
ein Minimal-Build, der nur `REQUIRES vfs` zu einer sonst unveraenderten
main-Konfiguration hinzufuegt (ganz ohne SPIFFS/SD-Code), um zu pruefen, ob
allein das Linken von `vfs` reicht.

Der Versuch (SPIFFS-Storage + minimaler HTTP-Server ueber `esp_http_server`
fuer eine Datei-Browser-Seite) wurde **wieder aus `main` entfernt** (gleicher
Grund: main muss immer sauber booten). Der Code liegt unveraendert, aber
nicht in `EXTRA_COMPONENT_DIRS` eingebunden, in `components/core/storage/`,
`components/core/network/http_server.c(.h)` und `components/modules/files/`
fuer die gleiche spaetere Debugging-Session wie die SD-Karte.

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
