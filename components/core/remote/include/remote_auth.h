/**
 * remote_auth.h - Pairing/Session-Authentifizierung fuer den Remote-Zugriff
 * (Nutzeranforderung 16/37). Kein Passwort im klassischen Sinn: ein
 * kurzlebiger, auf dem Tab5-Display angezeigter Pairing-Code tauscht sich
 * gegen einen langlebigen Client-Token, den der PC/Mac-Client fortan als
 * Bearer-Token mitschickt.
 *
 * SICHERHEITSHINWEIS (siehe Nutzeranforderung 29 / docs/remote_access.md):
 * Client-Tokens werden ausschliesslich im RAM gehalten (kein NVS/Flash-
 * Persistenz) - ein Geraete-Neustart vergisst alle gepairten Clients und
 * erfordert erneutes Pairing. Grund: CONFIG_NVS_ENCRYPTION ist im Projekt
 * bewusst noch nicht aktiviert (siehe settings.h), ein Klartext-Token in
 * unverschluesseltem NVS waere ein Sicherheitsrisiko. "Sicher eingeschraenkt
 * statt unsicher persistent" (Nutzervorgabe).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REMOTE_AUTH_CODE_LEN     6    // 6-stelliger Pairing-Code, z.B. "482917"
#define REMOTE_AUTH_TOKEN_HEXLEN 64   // 32 Byte Zufall als Hex-String
#define REMOTE_AUTH_MAX_CLIENTS  4
#define REMOTE_AUTH_CODE_VALIDITY_S 300
#define REMOTE_AUTH_MAX_ATTEMPTS_PER_WINDOW 5
#define REMOTE_AUTH_LOCKOUT_WINDOW_S 300

typedef struct {
    bool     in_use;
    char     client_id[17];    // 8 Byte Zufall als Hex-String
    char     name[32];
    int64_t  paired_at_us;
    int64_t  last_seen_us;
} remote_auth_client_t;

esp_err_t remote_auth_init(void);

// Erzeugt einen neuen 6-stelligen Pairing-Code (ersetzt einen evtl. noch
// aktiven). Vom Settings-Screen ueber "SHOW PAIR CODE" ausgeloest.
esp_err_t remote_auth_generate_code(char out_code[REMOTE_AUTH_CODE_LEN + 1], int *out_valid_s);

// Liefert true + Restlaufzeit, wenn aktuell ein Code aktiv ist (fuer die
// Settings-UI-Anzeige "Valid for: 04:43").
bool remote_auth_get_active_code(char out_code[REMOTE_AUTH_CODE_LEN + 1], int *out_remaining_s);

// Prueft den Code gegen den aktuell aktiven (rate-limited, siehe
// REMOTE_AUTH_MAX_ATTEMPTS_PER_WINDOW). Bei Erfolg wird ein neuer Client
// angelegt und *out_token_hex (Puffer >= REMOTE_AUTH_TOKEN_HEXLEN+1) gefuellt.
// ESP_ERR_INVALID_STATE = zu viele Fehlversuche (temporaer gesperrt).
esp_err_t remote_auth_confirm(const char *code, const char *client_name,
                               char out_token_hex[REMOTE_AUTH_TOKEN_HEXLEN + 1],
                               remote_auth_client_t *out_client);

// Validiert einen Bearer-Token aus einem eingehenden Request. Aktualisiert
// last_seen_us bei Erfolg.
bool remote_auth_validate_token(const char *token_hex);

size_t remote_auth_get_clients(remote_auth_client_t *out, size_t max);

// Verwirft alle Tokens (Settings "REVOKE ALL").
void remote_auth_revoke_all(void);

#ifdef __cplusplus
}
#endif
