/**
 * remote_protocol.h - Gemeinsame Konstanten/Helfer fuer das versionierte
 * REST+WebSocket-Protokoll (siehe docs/remote_protocol.md). Sowohl
 * remote_server.c (REST/WS-Handler) als auch remote_events.c (Broadcast-
 * Events) haengen sich an dasselbe "protocol"-Feld, damit spaetere Clients
 * (Nutzeranforderung 38) Protokoll-Versionen unterscheiden koennen.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REMOTE_PROTOCOL_VERSION 1

// Erzeugt {"protocol":1,"device":"<Name>","firmware":"<Version>"} - Aufrufer
// haengt weitere Felder an und gibt das Objekt selbst per cJSON_Delete() frei
// (oder uebergibt es an remote_events_broadcast_json_owned() via cJSON_PrintUnformatted).
cJSON *remote_protocol_new_envelope(void);

// CRC32 (esp_rom_crc32_le) fuer optionale Chunk-Integritaetspruefung
// (Nutzeranforderung 13 - "vorzugsweise CRC32/SHA256").
uint32_t remote_protocol_crc32(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
