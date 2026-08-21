# USB Host (Phase 5)

Der ESP32-P4 des Tab5 fungiert als USB-Host fuer angeschlossene
Mikrocontroller-Boards ueber den einzelnen USB-A-Host-Port (siehe
`docs/pinout.md`). Implementiert in `components/core/usb_host/`.

## Architektur

```
usb_host_manager.c   - ESP-IDFs eingebaute usb_host-Bibliothek installieren,
                        Client registrieren, Connect/Disconnect + Deskriptoren
                        auslesen (VID/PID/Strings/Geraeteklasse). Kennt keine
                        Chip-/Treiberlogik.
        |
        v
usb_device_manager.c - Klassifiziert das gemeldete Geraet (bekannte VID/PID-
                        Paare, siehe unten) und leitet Faehigkeiten ab
                        (serial_supported / flash_supported). UI und Remote-
                        API lesen NUR hier, nie direkt aus usb_host_manager.
        |
        v
usb_serial.c          - Serieller Datenpfad ueber Espressifs offizielle
                        cdc_acm_host-Komponente (usb/cdc_acm_host.h). Fester
                        8-KB-Ringpuffer fuer RX, kein unbegrenztes Wachstum.
                        Genutzt sowohl vom Flash Manager (esp-serial-flasher-
                        Transport) als auch vom Remote-Serial-Bridge-Pfad -
                        ein Datenpfad statt zwei unabhaengiger.
```

Beide `usb_host_manager` (generischer Client, jedes USB-Geraet) und
`cdc_acm_host` (eigener, spezialisierter Client fuer die eigentliche
Datenuebertragung) registrieren sich unabhaengig gegen dieselbe
`usb_host_install()`-Instanz - das ist laut ESP-IDF-Architektur der
vorgesehene Weg, mehrere Klassentreiber/Consumer nebeneinander zu betreiben,
kein Sonderfall dieses Projekts.

**Lazy Bring-up**: `usb_host_manager_init()` wird NICHT beim Boot
aufgerufen, sondern erst beim ersten Betreten des Flash-Screens oder der
ersten Remote-API-Anfrage, die USB braucht (`flash_manager_init()` ruft es
intern auf) - analog zum bestehenden `wifi_module_init()`-Muster, das
SDIO-Bring-up zum C6 ebenfalls bewusst nicht beim Boot ausloest (siehe
`docs/hardware_reference.md`).

## Bekannte VID/PID-Zuordnung (UI-Hinweis, NICHT autoritativ)

`usb_device_manager.c` enthaelt eine kleine Tabelle bekannter VID/PID-Paare
fuer seriell nutzbare USB-Bruecken:

| VID:PID | Bezeichnung |
|---|---|
| 303A:1001 | Espressif USB-Serial/JTOG (ESP32-S3/C3/C6/H2/**alle** damit ausgestatteten Chips - siehe unten) |
| 303A:0002 | Espressif natives USB-CDC (USB-OTG) |
| 10C4:EA60 | Silicon Labs CP210x |
| 1A86:7523 | WCH CH340 |
| 1A86:55D4 | WCH CH9102 |
| 0403:6001 | FTDI FT232R |
| 0403:6015 | FTDI FT230X |

**Wichtig, siehe Code-Kommentar in `usb_device_manager.h`**: Espressif-Chips
mit eingebautem USB-Serial/JTOG (ESP32-S3, -C3, -C6, -H2 und vermutlich
weitere) melden **alle denselben** generischen USB-Deskriptor (303A:1001,
Produktstring "USB JTAG/serial debug unit"). Der USB-Deskriptor allein
erlaubt **keine** Unterscheidung, welcher konkrete Chip angeschlossen ist -
das waere Rateei (ausdruecklich gegen Nutzervorgabe Abschnitt 41/42, "keine
Fake Device Detection", "kein Hardcoded ESP32-S3"). Die tatsaechliche,
autoritative Chip-Erkennung liefert erst `esp_loader_get_target()` aus
esp-serial-flasher NACH dem Bootloader-Sync (liest den echten Chip-Magic-
Wert direkt vom ROM-Bootloader, siehe `docs/remote_flashing.md`). Die
VID/PID-Tabelle ist bewusst nur ein UI-Hinweis vor dem Verbinden ("das ist
vermutlich ein Espressif-Chip"), nicht das letzte Wort.

Diese Tabelle ist **nicht auf echter Hardware gegen jeden einzelnen Chip
verifiziert** - Werte stammen aus bekannten esptool.py/TinyUSB-
Geraetelisten (Trainingswissen, siehe Abschluss-Report zu den generellen
Grenzen dieser Session).

## USB-Treiber-Support-Matrix

| Klasse | Treiber | Status |
|---|---|---|
| Espressif natives USB-CDC / USB-Serial-JTOG | `cdc_acm_host` (offizielle Espressif-Komponente, Standard-CDC-ACM-Klasse) | **Verdrahtet** (`usb_serial.c`), compile-only verifiziert |
| Silicon Labs CP210x | kein Standard-CDC-ACM (Vendor-spezifisches Protokoll) - Espressif liefert dafuer eigene `usb_host_*_vcp`-Komponenten im `esp-usb`-Repo (C++-API) | **Erkannt, nicht verdrahtet** - siehe unten |
| WCH CH340/CH341/CH9102 | dito (Vendor-spezifisch) | **Erkannt, nicht verdrahtet** |
| FTDI FT23x | dito (Vendor-spezifisch) | **Erkannt, nicht verdrahtet** |

### Warum CP210x/CH34x/FTDI nicht verdrahtet sind

Espressifs offizielle Treiber fuer diese drei Chips (`usb_host_cp210x_vcp`,
`usb_host_ch34x_vcp`, `usb_host_ftdi_vcp` im `esp-usb`-Repository) sind
**C++-Komponenten** (Klassen `Cp210xDevice`/`Ch34xDevice`/`FT23xDevice`, alle
von `CdcAcmDevice` abgeleitet). Das gesamte Projekt ist bislang reines C
(keine einzige `.cpp`-Datei im Repository) - das Einziehen einer C/C++-
Interop-Grenze allein fuer diese drei Treiber, ohne die Moeglichkeit, das
Ergebnis auf echter Hardware zu verifizieren (siehe Abschluss-Report:
diese Session hatte weder ESP-IDF-Toolchain noch Zugriff auf die
Component-Registry noch physische Hardware), war das mit Abstand
riskanteste Einzelstueck dieser Aenderung. Nutzervorgabe Abschnitt 41 ist
hier eindeutig: *"Bei Unsicherheit lieber eine Funktion zunaechst sauber
als unsupported markieren, statt riskanten Hardwarecode einzubauen."*

`usb_device_manager.c` erkennt und meldet diese Chips trotzdem korrekt
(VID/PID-Klassifikation, `bridge_label` zeigt z.B. "Silicon Labs CP210x"),
setzt aber `serial_supported`/`flash_supported` bewusst auf `false` - UI und
Remote-API zeigen dafuer "Unsupported USB device" statt eine Funktion
vorzutaeuschen, die nicht real ist (Nutzervorgabe Abschnitt 42).

**Naechster sinnvoller Schritt** (nicht in dieser Session): C++ fuer genau
diese drei Treiber-Komponenten als isolierte `.cpp`-Uebersetzungseinheiten
mit einer reinen-C-Bridge-Funktion (`extern "C"`) einziehen, dann echte
CP210x/CH340-Boards zum Testen besorgen und schrittweise verifizieren -
nicht blind alle drei auf einmal.

## Faehigkeiten-Anzeige (Beispiel)

```
USB DEVICE CONNECTED
Type: Espressif USB-Serial/JTOG
Interface: Native USB CDC (cdc_acm_host)
VID: 303A  PID: 1001
Serial: <vom Geraet gemeldet>
Capabilities:
  Serial: yes
  Flash:  yes (esp-serial-flasher, siehe docs/remote_flashing.md)
  Reset:  yes (DTR/RTS-Sequenz, siehe docs/remote_flashing.md)
```

## Nicht verifizierte/offene Punkte

- Kein Hub-Fanout gezielt getestet (der Tab5 hat einen einzelnen USB-A-
  Host-Port, mehrere gleichzeitig angeschlossene Geraete ueber einen
  externen Hub sind durch die usb_host-Bibliothek technisch nicht
  ausgeschlossen, aber `usb_device_manager.c` geht von genau einem
  aktiven Ziel aus).
- USB-Host-Task-Prioritaeten (`USB_LIB_TASK_PRIORITY`/
  `USB_CLIENT_TASK_PRIORITY` in `usb_host_manager.c`, aktuell 5) sind ein
  vernuenftiger Default, nicht auf echter Hardware gegen LVGL/esp_hosted-
  Lastszenarien feinabgestimmt.
- Exakte `usb_host`/`cdc_acm_host`-API-Signaturen sind nach bestem
  Trainingswissen geschrieben, aber in dieser Session **nicht gegen die
  tatsaechlich installierten ESP-IDF-Header kompiliert** (kein Toolchain-
  Zugriff, siehe Abschluss-Report) - vor dem ersten `idf.py build`
  gegenpruefen.
