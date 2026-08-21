# Remote Protocol (Phase 5)

REST + WebSocket API, Version 1 (`REMOTE_PROTOCOL_VERSION`,
`components/core/remote/include/remote_protocol.h`). Base-URL:
`http://<device-ip>/api/v1/`.

Jede JSON-Antwort traegt ein Envelope-Feld `"protocol": 1` (sowie meist
`"device"`/`"firmware"`) - so koennen kuenftige Clients Protokoll-Versionen
unterscheiden (Nutzeranforderung 38).

## Auth

`Authorization: Bearer <token>` Header. Siehe `docs/remote_access.md` fuer
das Pairing, das diesen Token liefert. WebSocket-Endpunkte koennen keine
Custom-Header beim Handshake setzen (Browser-Einschraenkung) - dort wird
der Token als Query-Parameter `?token=...` uebergeben.

## REST-Endpunkte

| Methode | Pfad | Auth | Beschreibung |
|---|---|---|---|
| GET | `/api/v1/system` | offen | Geraetename/Firmware, Heap/PSRAM/Flash, Uptime |
| GET | `/api/v1/network` | offen | Wi-Fi-Status, Remote-Server-Status/Client-Anzahl |
| POST | `/api/v1/pair/confirm` | offen | `{code, client_name}` -> `{client_token, client_id}` |
| GET | `/api/v1/devices` | Token | `{devices: [...]}` (0 oder 1 Eintrag) |
| GET | `/api/v1/devices/current` | Token | Details des aktuell verbundenen USB-Ziels |
| GET | `/api/v1/flash/status` | Token | Kompletter `flash_status_t` als JSON |
| POST | `/api/v1/flash/start` | Token | Body: Flash-Manifest (siehe `docs/remote_flashing.md`) |
| POST | `/api/v1/flash/chunk` | Token | Query `?file=<idx>&offset=<bytes>`, Body: rohe Bytes (`application/octet-stream`, max. 4096 B) |
| POST | `/api/v1/flash/finish` | Token | Signalisiert "alle Dateien vollstaendig" -> Reset -> SUCCESS |
| POST | `/api/v1/flash/cancel` | Token | Bricht einen laufenden Flash-Vorgang ab |
| POST | `/api/v1/device/reset` | Token | Normale Reset-Sequenz (kein Bootloader) |
| POST | `/api/v1/device/bootloader` | Token | Nur die DTR/RTS-Bootloader-Sequenz, kein Sync |
| GET | `/api/v1/logs` | Token | Text/plain, juengste Log-Zeilen |
| WS | `/api/v1/ws?token=` | Token | Event-Stream (siehe unten) |
| WS | `/api/v1/serial?token=&baud=` | Token | Roher serieller RX/TX-Kanal |

Alle Endpunkte unterstuetzen `OPTIONS` (CORS-Preflight) und antworten mit
`Access-Control-Allow-Origin: *` - bewusst offen fuer ein LAN-Werkzeug
(`tools/cyberdeck-web` laeuft von `file://`/beliebiger Origin), nicht fuer
einen oeffentlichen Deployment-Kontext gedacht.

### Chunk-Integritaet (optional)

`POST /flash/chunk` akzeptiert einen optionalen Header `X-Chunk-CRC32`
(Hex-String) - wird er gesetzt, prueft das Geraet ihn gegen
`esp_rom_crc32_le(0, chunk, len)` und lehnt bei Mismatch mit 400 ab.

**Achtung**: `tools/cyberdeck-cli` und `tools/cyberdeck-web` senden diesen
Header aktuell **nicht** - Pythons `zlib.crc32`/JavaScripts uebliche
CRC32-Implementierungen verwenden die Standard-Initialisierung/
Schluss-XOR-Konvention (Init `0xFFFFFFFF`, Schluss-XOR `0xFFFFFFFF`),
waehrend `esp_rom_crc32_le(0, ...)` mit Startwert 0 aufgerufen wird - ob
beide Seiten ohne zusaetzliche Anpassung dasselbe Ergebnis liefern, wurde
in dieser Session **nicht verifiziert** (kein Hardware-/Toolchain-Zugriff).
Vor dem Aktivieren dieses Headers auf Client-Seite: einmal einen bekannten
Chunk auf beiden Seiten hashen und vergleichen.

## WebSocket-Events (`/api/v1/ws`)

Reine Server->Client-Broadcasts, ein Event-Typ pro Nachricht:

```json
{"protocol":1,"type":"device.connected","device":{"vid":"303a","pid":"1001","name":"...","bridge":"...","serial_supported":true,"flash_supported":true}}
{"protocol":1,"type":"device.disconnected"}
{"protocol":1,"type":"flash.progress","state":"FLASHING","file":"firmware.bin","address":"0x00010000","progress":74,"written":934000,"total":1260000}
{"protocol":1,"type":"flash.completed","success":true}
{"protocol":1,"type":"flash.completed","success":false,"error":"Sync timeout"}
```

Vom Client eingehende Frames auf `/ws` werden aktuell nur korrekt
konsumiert (kein Protokoll-Hang), aber nicht interpretiert - alle Aktionen
laufen ueber REST, nicht ueber ein WS-Kommandoprotokoll (bewusst einfach
gehalten fuer die erste Version).

## Serieller Kanal (`/api/v1/serial`)

Roh-Binaerframes in beide Richtungen (kein JSON-Envelope, das waere fuer
Terminal-Bytes unnoetiger Overhead): Server->Client = empfangene Bytes vom
Zielchip, Client->Server = zu sendende Bytes. Query-Parameter `baud`
(Default 115200) setzt die Baudrate beim Oeffnen. Nur eine aktive Sitzung
gleichzeitig (siehe `usb_serial.h` - ein USB-Host-Port, ein Ziel).

## Fehlerformat

```json
{"error": "invalid_manifest", "message": "Datei-Eintrag unvollstaendig/ungueltig"}
```

`error` ist ein stabiler Kurzcode (fuer programmatische Client-Logik),
`message` ein menschenlesbarer Text (Deutsch/Englisch gemischt, je nachdem
welche Fehlerquelle - siehe `flash_error_str()` fuer die auf Englisch
gehaltenen Flash-Fehlermeldungen, passend zu den in Abschnitt 24 der
Aufgabenstellung vorgegebenen Beispieltexten).

## Nicht verifizierte/offene Punkte

- `httpd_ws_recv_frame`/`httpd_ws_send_frame_async`-Nutzung in
  `remote_server.c`/`remote_events.c` folgt dem in ESP-IDF-Beispielen
  ueblichen Muster, wurde aber nicht gegen die installierte
  `esp_http_server`-Version kompiliert (siehe Abschluss-Report).
- Ob der WebSocket-Handshake bei ungueltigem Token sauber mit einem HTTP-
  Fehlerstatus abgelehnt wird oder die Verbindung erst nach einem
  akzeptierten Handshake wieder geschlossen wird, haengt von der genauen
  `esp_http_server`-Version ab (siehe Code-Kommentar in `remote_server.c`,
  `ws_handler()`) - sicherheitsrelevant ist in beiden Faellen dasselbe:
  kein Nutzdaten-Byte erreicht einen nicht authentifizierten Client.
