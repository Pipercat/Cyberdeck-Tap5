# Remote Flashing (Phase 5)

End-to-end flow: PC/Mac build -> WLAN -> CyberDeck -> USB -> angeschlossener
ESP-Chip. Implementiert in `components/modules/flash/`.

## Architektur

```
PC / Mac
   |  WLAN (HTTP REST, siehe docs/remote_protocol.md)
   v
remote_server.c        - nimmt Manifest + Firmware-Chunks entgegen
   |
   v
flash_manager.c         - Zustandsautomat, EIN statischer 4-KB-Chunk-Puffer
   |                        (FLASH_CHUNK_MAX_LEN), Rueckdruck: der naechste
   |                        Chunk wird erst angenommen, wenn der vorherige
   |                        tatsaechlich geschrieben wurde. NIE eine
   |                        vollstaendige Firmware im RAM.
   v
flash_target.c           - Espressifs esp-serial-flasher (esp_loader.h)
   |                        als Bootloader-Protokoll-Implementierung
   v
usb_serial.c (USB Host)  - Transport zum Zielchip (siehe docs/usb_host.md)
```

**Kein Dateisystem in diesem Pfad** - siehe "VFS/SPIFFS-Status" unten fuer
die Begruendung. Firmware wird ausschliesslich gestreamt: PC sendet feste
4-KB-Bloecke per HTTP-POST, jeder Block wird sofort auf den Zielchip
geschrieben und danach verworfen.

## esp-serial-flasher-Integration

`flash_target.c` implementiert die von esp-serial-flasher geforderten
`loader_port_*()`-Transport-Callbacks (siehe die Bibliothek selbst,
"esp-serial-flasher porting guide") ueber `usb_serial.c` statt eines UART-
Treibers - das ist der vorgesehene Erweiterungspunkt der Bibliothek fuer
neue Transporte, keine Umgehung.

**Chip-Support** (esp-serial-flasher-Zielliste): ESP32, ESP32-S2, ESP32-S3,
ESP32-C2, ESP32-C3, ESP32-C5, ESP32-C6, ESP32-H2, ESP32-P4. `flash_target.c`
mappt esp-serial-flashers `target_chip_t`-Enum 1:1 auf ein eigenes
`flash_target_chip_t` - nicht unterstuetzte/unbekannte Chips melden sauber
`FLASH_TARGET_CHIP_UNKNOWN` statt zu raten.

Die **einzige autoritative Chip-Erkennung** im System ist
`esp_loader_get_target()` nach erfolgreichem Bootloader-Sync (liest den
echten Chip-Magic-Wert vom ROM-Bootloader) - siehe `docs/usb_host.md` fuer
die Begruendung, warum der USB-Deskriptor dafuer NICHT reicht.

## Bootloader-Einstieg / Reset (DTR/RTS)

`flash_target.c` implementiert die klassische esptool.py-Auto-Reset-
Sequenz (DTR steuert GPIO0, RTS steuert EN/Reset, beide ueber die auf
praktisch allen ESP32-Dev-Boards verbaute NPN-Inverter-Schaltung
aktiv-low) - eine breit etablierte, herstelleruebergreifende Konvention
(Espressif/Adafruit/SparkFun/M5Stack-Boards nutzen sie alle gleich), **kein
geraetespezifisch geratener Wert**.

Funktioniert die automatische Sequenz auf einem konkreten Board nicht (z.B.
kein Auto-Program-Schaltkreis verbaut), meldet `flash_target_connect()`
einen Sync-Timeout - `POST /api/v1/flash/start` liefert dann einen Fehler,
und die UI/Remote-Clients zeigen "Sync timeout" statt eine falsche
Erfolgsmeldung. `POST /api/v1/device/bootloader` fuehrt nur die DTR/RTS-
Sequenz aus, OHNE zu behaupten, dass der Chip danach tatsaechlich im
Bootloader ist (kein Sync-Versuch) - bei fehlender automatischer
Unterstuetzung zeigt die Antwort "Manual bootloader entry required. Hold
BOOT and press RESET." (Nutzervorgabe Abschnitt 15).

## Flash-Manifest

```json
{
  "chip": "esp32s3",
  "files": [
    { "address": "0x0000",  "name": "bootloader.bin",       "size": 22144 },
    { "address": "0x8000",  "name": "partition-table.bin",  "size": 3072 },
    { "address": "0x10000", "name": "firmware.bin",         "size": 985232 }
  ]
}
```

`address` akzeptiert sowohl Hex-Strings ("0x10000") als auch Zahlen.
`chip` ist ein reiner Hinweis vom Client (siehe oben, warum das nicht
autoritativ ist). `tools/cyberdeck-cli` und `tools/cyberdeck-web` bauen
dieses Manifest automatisch aus dem ESP-IDF-Build-Artefakt
`build/flasher_args.json` - der Nutzer muss keine Adressen von Hand
eingeben (Nutzeranforderung 22).

## Zustandsautomat (`flash_state_t`)

```
IDLE -> PREPARING -> ENTERING_BOOTLOADER -> SYNCING
      -> [ERASING -> FLASHING -> VERIFYING] je Datei im Manifest
      -> RESETTING -> SUCCESS
Jeder Schritt kann nach ERROR abbrechen (siehe flash_error_t fuer die
genauen Fehlerarten - No target/Unsupported device/Sync timeout/Erase
failed/Write failed/Verify failed/Upload interrupted/...).
CANCELLED ist jederzeit ueber POST /api/v1/flash/cancel erreichbar.
```

Zustand + Fehler sind sowohl auf dem Tab5 (Flash-Screen, `flash_ui.c`) als
auch remote (`GET /api/v1/flash/status`, `flash.progress`/`flash.completed`-
WebSocket-Events) identisch sichtbar - eine einzige Quelle der Wahrheit
(`flash_manager.c`s statisches `s_status`).

## Fail-Safe-Verhalten

- Jeder Fehlerpfad in `flash_manager.c` ruft `flash_target_disconnect()`
  (gibt die USB-Serial-Sitzung frei) und setzt den Zustand auf `ERROR` -
  ein neuer Versuch (`flash_manager_start()`) ist danach immer wieder
  moeglich.
- Wird das Ziel waehrend eines aktiven Flash-Vorgangs vom USB abgezogen,
  meldet ein Disconnect-Callback (`usb_host_manager`) dies an
  `flash_manager.c`, das den laufenden Vorgang beim naechsten sicheren
  Zeitpunkt mit `FLASH_ERR_TARGET_DISCONNECTED` abbricht statt unbegrenzt
  auf Antworten vom nicht mehr vorhandenen Chip zu warten.
- Der Flash Worker Task ist von LVGL und vom `esp_http_server`-Task
  getrennt (eigener FreeRTOS-Task, Kommunikation ueber Queues/Semaphore) -
  ein blockierender/fehlschlagender Flash-Vorgang kann weder die UI noch
  den Remote-Server einfrieren.

## VFS/SPIFFS-Status (wichtig fuer diese Aenderung)

`docs/hardware_reference.md` dokumentiert einen frueher reproduzierbaren
Boot-Crash beim Hinzufuegen von SD-Karte/SPIFFS/VFS neben esp_hosted. Diese
Session hat den vollstaendigen Verlauf gelesen, bevor irgendein Code
geschrieben wurde:

1. Erste Vermutung: SDMMC-spezifischer Konflikt mit esp_hosteds SDIO-
   Transport - **widerlegt** (derselbe Crash trat auch mit reinem SPIFFS
   ohne SDMMC auf).
2. Zweite Vermutung: die `vfs`-Komponente allgemein - **widerlegt** (ein
   rein kosmetischer Theme-Diff ganz ohne `vfs` loeste denselben Crash aus).
3. **Root Cause gefunden und behoben** (`main/main.c`,
   `reserve_tcm_to_avoid_esp_hosted_crash()`, Commit `36557a6`): ein
   ESP32-P4-spezifischer Espressif-seitiger Inkonsistenz zwischen
   `memory_layout.c`s TCM-Capability-Tagging und `esp_ptr_internal()`s
   Adressbereichspruefung liess FreeRTOS' fruehe Timer-Task-Allokation
   gelegentlich in TCM landen, was einen Static-Memory-Gueltigkeitscheck
   fehlschlagen liess - unabhaengig von SD/SPIFFS/vfs, ausgeloest durch
   praktisch jede Aenderung am statischen Speicherlayout des Binaries. Der
   Fix reserviert TCM dauerhaft VOR jeder anderen Allokation, sodass der
   Allocator nie mehr dorthin ausweichen kann - 10/10 saubere Boots plus
   ein echter Power-Cycle auf echter Hardware verifiziert (siehe
   `docs/hardware_reference.md` fuer die vollstaendige Beweisfuehrung).

**Konsequenz fuer diese Aenderung**: der eigentliche Bug war nie
SPIFFS/VFS-spezifisch und ist bereits strukturell behoben. Trotzdem bleibt
diese Implementierung bewusst bei der vom Nutzer vorgegebenen
Architektur (Streaming statt Dateisystem) - nicht weil das
SPIFFS/VFS-Risiko noch bestehen wuerde, sondern weil:

- Streaming ohnehin die bessere Architektur fuer Firmware-Transfer ist
  (kein unnoetiges vollstaendiges Zwischenspeichern, siehe oben),
- diese Session **keine Moeglichkeit hatte, den TCM-Fix auf echter
  Hardware gegen die NEUEN Komponenten dieser Aenderung
  (usb_host/cdc_acm_host/esp-serial-flasher/esp_http_server, alle
  zusammen ein nicht kleiner Speicherlayout-Sprung) zu verifizieren** -
  siehe Abschluss-Report. "Mehrfach real testen, bevor 'fertig'" (Regel
  aus `docs/hardware_reference.md`) gilt hier umso mehr.

Das on-device Web-UI-Hosting (Nutzeranforderung 21, "direkt vom CyberDeck
ausgeliefert") wurde aus demselben Vorsichtsgrund vertagt: ESP-IDFs
`EMBED_TXTFILES`/`EMBED_FILES` braucht kein VFS-Mount (linker-eingebettete
Bytes, kein Dateisystemtreiber), waere also vermutlich unproblematisch -
aber "vermutlich" reicht hier nicht ohne Hardware-Verifikation. Der
separate PC-Client (`tools/cyberdeck-web/`) ist die vom Nutzer explizit
sanktionierte Alternative fuer genau diesen Fall (Nutzeranforderung 21:
"Wenn das nicht stabil moeglich ist: Erstelle stattdessen einen kleinen
separaten PC-Client").

## Update (2026-08-21): esp_loader-Portierung korrigiert, Boot verifiziert

Der erste echte `idf.py build` dieses Branches (Commit `c74bbb1`) zeigte,
dass die urspruengliche `flash_target.c` gegen ein aelteres
esp-serial-flasher-Porting-Modell geschrieben war (globale
`loader_port_*()`-Funktionen). Die tatsaechlich aufgeloeste Version
(`espressif/esp-serial-flasher` **2.0.0**, siehe `dependencies.lock`)
nutzt stattdessen ein vtable-basiertes `esp_loader_port_ops_t` mit
`esp_loader_t*`/`esp_loader_port_t`-Handles - `flash_target.c` wurde
entsprechend portiert. Boot/Wi-Fi-Reconnect/Remote-Server-Autostart sind
seitdem auf echter Tab5-Hardware verifiziert (siehe README). Ein
vollstaendiger Flash-Zyklus gegen ein angeschlossenes Ziel-ESP-Board steht
weiterhin aus - das ist der naechste Verifikationsschritt.

## Nicht verifizierte/offene Punkte

- Ein vollstaendiger Flash-Zyklus (USB-Ziel erkennen, Bootloader-Sync,
  Schreiben/Verify, Reset in neue Firmware) ist noch nicht auf echter
  Hardware durchlaufen worden.
- Baudratenwechsel nach Sync (`FLASH_BAUD_RATE = 460800` in
  `flash_manager.c`) ist ein ueblicher Wert, nicht auf diesem Board/dieser
  USB-Bruecken-Kombination verifiziert - bei Problemen zunaechst auf
  115200 zurueckfallen.
- Kein Multi-Target-Szenario getestet (nur ein USB-Host-Port vorhanden).
