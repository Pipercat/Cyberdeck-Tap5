# Remote Access (Phase 5)

Wie man den CyberDeck-Remote-Zugriff aktiviert, pairt und absichert.
Implementiert in `components/core/remote/`.

## Aktivieren

1. Tab5: WLAN verbinden (Network-Screen - unveraendertes bestehendes
   Verhalten, siehe README).
2. Tab5: Settings > **Remote Access** > Toggle auf ON. Der Server startet
   sofort (`remote_server_start()`); zusaetzlich startet er automatisch
   erneut, sobald das Geraet eine IP-Adresse bekommt (z.B. nach einem
   WLAN-Reconnect) - solange die Einstellung aktiviert bleibt.
3. Die aktuelle IP wird direkt darunter angezeigt (auch im Network-Screen
   und in der Statusleiste oben, "Server"/"USB"-Felder).

Der Server ist **standardmaessig deaktiviert** (Nutzervorgabe Abschnitt
37) - ein frisch geflashtes/zurueckgesetztes Geraet ist nicht remote
erreichbar, bis der Nutzer das explizit einschaltet.

## Pairing

1. Tab5: Settings > Remote Access > "SHOW PAIR CODE" antippen. Ein
   6-stelliger Code erscheint mit Countdown (5 Minuten gueltig, danach
   verfaellt er automatisch und muss neu erzeugt werden).
2. PC/Mac: `cyberdeck.py --host <ip> pair --code <code>` (oder die
   entsprechende Eingabe in `tools/cyberdeck-web`).
3. Bei Erfolg erhaelt der Client einen langlebigen Bearer-Token, den er ab
   dann bei jeder Anfrage mitschickt (`Authorization: Bearer <token>`).
   Der Code selbst ist Einmalgebrauch (nach erfolgreichem Pairing sofort
   ungueltig).

**Rate-Limiting**: maximal 5 Pairing-Versuche pro 5-Minuten-Fenster
(`remote_auth.c`, `REMOTE_AUTH_MAX_ATTEMPTS_PER_WINDOW`) - danach wird
jeder weitere Versuch unabhaengig vom Code sofort mit 429 abgelehnt, bis
das Fenster ablaeuft.

Settings > "REVOKE ALL" verwirft alle gepairten Clients sofort (z.B. bei
Verdacht auf ein kompromittiertes Geraet).

## Sicherheitsmodell

| Endpunkt-Gruppe | Auth |
|---|---|
| `GET /system`, `GET /network` | offen (rein lesend, keine sensiblen Daten) |
| `POST /pair/confirm` | offen (das IST der Auth-Bootstrap, rate-limited) |
| Alles Weitere (`/devices*`, `/flash/*`, `/device/*`, `/logs`, `/ws`, `/serial`) | **immer** ein gueltiger Bearer-Token |

**Wichtig**: die Settings-Option "Require Pairing" schaltet **nicht**
irgendeine Authentifizierung fuer Flash-/Geraete-Endpunkte ab - diese
verlangen immer einen gueltigen Token, ohne Ausnahme (Nutzervorgabe
Abschnitt 37: "niemals unauthenticated /flash im normalen WLAN erlauben").
Falls diese Option in einer spaeteren Phase erweitert wird (z.B. um
komplett offene Nur-Lese-Endpunkte im Heimnetz zu erlauben), muss diese
Garantie fuer alle mutierenden Endpunkte erhalten bleiben.

### Tokens sind fluechtig (RAM-only)

Gepairte Clients (`client_id`, Token, Name) werden **ausschliesslich im
RAM** gehalten - ein Geraete-Neustart vergisst alle Pairings, jeder Client
muss sich danach neu pairen. Grund: `CONFIG_NVS_ENCRYPTION` ist im Projekt
bewusst noch nicht aktiviert (siehe `components/core/settings/settings.h`)
- ein Klartext-Bearer-Token in unverschluesseltem NVS waere ein
Sicherheitsrisiko. **"Pairing tokens volatile until encrypted NVS is
implemented"** - explizite Nutzervorgabe (Abschnitt 29), hier fortgefuehrt.

Die Remote-Access-*Einstellungen* selbst (An/Aus, Geraetename, Require-
Pairing-Flag) sind normale, nicht-sensible Settings und werden wie gehabt
im Settings-Blob persistiert (Schema v2, siehe `settings.h`).

### Keine Klartext-Logs

`remote_auth.c` loggt niemals den vollen Token- oder Pairing-Code-Wert
(nur "neuer Client gepairt", "Pairing gesperrt" etc.) - siehe
Nutzervorgabe Abschnitt 16 ("keine Passwoerter im Klartext loggen").

## mDNS: bewusst nicht implementiert

`cyberdeck.local` wurde in dieser Session **nicht** umgesetzt. Die
zugrundeliegende ESP-IDF-`mdns`-Komponente haengt technisch nicht am
dokumentierten VFS/SPIFFS-Boot-Crash (sie nutzt reines UDP-Multicast, kein
Dateisystem) - das Risiko waere vermutlich gering, aber "vermutlich"
reicht ohne Hardware-Zugriff nicht (siehe Abschluss-Report). Der Nutzer
sieht die IP-Adresse stattdessen direkt in Settings/Network/Statusleiste;
`tools/cyberdeck-cli`/`-web` erwarten aktuell eine manuell eingegebene
IP. **Stabilitaet vor Komfort** (Nutzervorgabe Abschnitt 17) - mDNS ist ein
guter naechster Schritt, sobald echte Hardware-Tests moeglich sind.

## Debugging

- Log-Tags: `REMOTE_SERVER`, `REMOTE_AUTH`, `REMOTE_EVENTS`, `USB_HOST`,
  `USB_DEVICE`, `USB_SERIAL`, `FLASH_MGR`, `FLASH_TARGET`.
- `GET /api/v1/logs` liefert die juengsten Log-Zeilen (nutzt den
  bestehenden `log_sink`-Ringpuffer, kein zweites Log-System).
- System-Monitor auf dem Tab5 zeigt Remote-Server-Status/Client-Anzahl,
  USB-Host-Status/erkanntes Ziel, Flash-Manager-Zustand.
