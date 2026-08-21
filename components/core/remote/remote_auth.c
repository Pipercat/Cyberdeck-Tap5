#include "remote_auth.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "REMOTE_AUTH";

static remote_auth_client_t s_clients[REMOTE_AUTH_MAX_CLIENTS];
static char s_client_tokens[REMOTE_AUTH_MAX_CLIENTS][REMOTE_AUTH_TOKEN_HEXLEN + 1];

static char s_active_code[REMOTE_AUTH_CODE_LEN + 1] = {0};
static int64_t s_code_generated_at_us = 0;
static bool s_code_active = false;

static int s_attempt_count = 0;
static int64_t s_attempt_window_start_us = 0;

static bool s_initialized = false;

static void bytes_to_hex(const uint8_t *bytes, size_t n, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = hex[bytes[i] >> 4];
        out[i * 2 + 1] = hex[bytes[i] & 0x0F];
    }
    out[n * 2] = '\0';
}

esp_err_t remote_auth_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    memset(s_clients, 0, sizeof(s_clients));
    memset(s_client_tokens, 0, sizeof(s_client_tokens));
    s_initialized = true;
    return ESP_OK;
}

esp_err_t remote_auth_generate_code(char out_code[REMOTE_AUTH_CODE_LEN + 1], int *out_valid_s)
{
    uint32_t r = esp_random() % 1000000;  // 000000-999999, gleichverteilt genug fuer einen UI-Pairing-Code
    snprintf(s_active_code, sizeof(s_active_code), "%06" PRIu32, r);
    s_code_generated_at_us = esp_timer_get_time();
    s_code_active = true;
    strncpy(out_code, s_active_code, REMOTE_AUTH_CODE_LEN + 1);
    if (out_valid_s != NULL) {
        *out_valid_s = REMOTE_AUTH_CODE_VALIDITY_S;
    }
    ESP_LOGI(TAG, "Neuer Pairing-Code generiert (gueltig %ds)", REMOTE_AUTH_CODE_VALIDITY_S);
    return ESP_OK;
}

static bool code_still_valid(void)
{
    if (!s_code_active) {
        return false;
    }
    int64_t age_s = (esp_timer_get_time() - s_code_generated_at_us) / 1000000;
    if (age_s >= REMOTE_AUTH_CODE_VALIDITY_S) {
        s_code_active = false;
        return false;
    }
    return true;
}

bool remote_auth_get_active_code(char out_code[REMOTE_AUTH_CODE_LEN + 1], int *out_remaining_s)
{
    if (!code_still_valid()) {
        return false;
    }
    strncpy(out_code, s_active_code, REMOTE_AUTH_CODE_LEN + 1);
    if (out_remaining_s != NULL) {
        int64_t age_s = (esp_timer_get_time() - s_code_generated_at_us) / 1000000;
        *out_remaining_s = (int)(REMOTE_AUTH_CODE_VALIDITY_S - age_s);
    }
    return true;
}

static bool rate_limited(void)
{
    int64_t now_us = esp_timer_get_time();
    if (s_attempt_window_start_us == 0 ||
        (now_us - s_attempt_window_start_us) / 1000000 >= REMOTE_AUTH_LOCKOUT_WINDOW_S) {
        s_attempt_window_start_us = now_us;
        s_attempt_count = 0;
    }
    return s_attempt_count >= REMOTE_AUTH_MAX_ATTEMPTS_PER_WINDOW;
}

esp_err_t remote_auth_confirm(const char *code, const char *client_name,
                               char out_token_hex[REMOTE_AUTH_TOKEN_HEXLEN + 1],
                               remote_auth_client_t *out_client)
{
    if (code == NULL || out_token_hex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (rate_limited()) {
        ESP_LOGW(TAG, "Pairing gesperrt (zu viele Fehlversuche im Zeitfenster)");
        return ESP_ERR_INVALID_STATE;
    }
    s_attempt_count++;

    if (!code_still_valid() || strncmp(code, s_active_code, REMOTE_AUTH_CODE_LEN) != 0) {
        ESP_LOGW(TAG, "Pairing-Code ungueltig/abgelaufen");
        return ESP_ERR_INVALID_ARG;
    }

    int free_slot = -1;
    for (int i = 0; i < REMOTE_AUTH_MAX_CLIENTS; i++) {
        if (!s_clients[i].in_use) {
            free_slot = i;
            break;
        }
    }
    if (free_slot < 0) {
        ESP_LOGW(TAG, "Maximale Anzahl gepairter Clients erreicht");
        return ESP_ERR_NO_MEM;
    }

    uint8_t id_bytes[8];
    uint8_t token_bytes[REMOTE_AUTH_TOKEN_HEXLEN / 2];
    esp_fill_random(id_bytes, sizeof(id_bytes));
    esp_fill_random(token_bytes, sizeof(token_bytes));

    remote_auth_client_t *c = &s_clients[free_slot];
    memset(c, 0, sizeof(*c));
    c->in_use = true;
    bytes_to_hex(id_bytes, sizeof(id_bytes), c->client_id);
    strncpy(c->name, (client_name != NULL && client_name[0] != '\0') ? client_name : "Unnamed client",
            sizeof(c->name) - 1);
    c->paired_at_us = esp_timer_get_time();
    c->last_seen_us = c->paired_at_us;

    bytes_to_hex(token_bytes, sizeof(token_bytes), s_client_tokens[free_slot]);
    strncpy(out_token_hex, s_client_tokens[free_slot], REMOTE_AUTH_TOKEN_HEXLEN + 1);
    if (out_client != NULL) {
        *out_client = *c;
    }

    s_code_active = false;   // Code ist Einmalgebrauch
    s_attempt_count = 0;     // erfolgreiches Pairing setzt das Fehlversuchsfenster zurueck
    ESP_LOGI(TAG, "Neuer Client gepairt: '%s' (id=%s)", c->name, c->client_id);
    return ESP_OK;
}

bool remote_auth_validate_token(const char *token_hex)
{
    if (token_hex == NULL) {
        return false;
    }
    for (int i = 0; i < REMOTE_AUTH_MAX_CLIENTS; i++) {
        if (s_clients[i].in_use && strcmp(s_client_tokens[i], token_hex) == 0) {
            s_clients[i].last_seen_us = esp_timer_get_time();
            return true;
        }
    }
    return false;
}

size_t remote_auth_get_clients(remote_auth_client_t *out, size_t max)
{
    size_t n = 0;
    for (int i = 0; i < REMOTE_AUTH_MAX_CLIENTS && n < max; i++) {
        if (s_clients[i].in_use) {
            out[n++] = s_clients[i];
        }
    }
    return n;
}

void remote_auth_revoke_all(void)
{
    memset(s_clients, 0, sizeof(s_clients));
    memset(s_client_tokens, 0, sizeof(s_client_tokens));
    s_code_active = false;
    ESP_LOGW(TAG, "Alle gepairten Clients widerrufen");
}
