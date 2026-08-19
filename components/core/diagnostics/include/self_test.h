/**
 * self_test.h - Diagnostics-Framework-Grundgeruest (Nutzeranforderung 18).
 *
 * Phase 1: nur die Struktur, KEINE echten Pruefungen. Jeder Testfall wird
 * hier registriert und liefert in Phase 1 self_test_result_t.status =
 * SELF_TEST_NOT_IMPLEMENTED. Ab Phase 2 ersetzen die jeweiligen Module
 * (GPIO, I2C, Kamera, ...) diesen Platzhalter durch echte Hardwarepruefungen.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SELF_TEST_NOT_IMPLEMENTED = 0,
    SELF_TEST_PASS,
    SELF_TEST_FAIL,
    SELF_TEST_SKIPPED,
} self_test_status_t;

typedef struct {
    const char *name;              // z.B. "Display", "Touch", "SD Card"
    self_test_status_t status;
    const char *detail;            // Fehlerdetail, NULL bei PASS
} self_test_result_t;

typedef self_test_status_t (*self_test_fn_t)(const char **out_detail);

// Registriert einen Testfall. Muss vor self_test_run_all() erfolgen.
esp_err_t self_test_register(const char *name, self_test_fn_t fn);

// Fuehrt alle registrierten Tests aus. results muss Platz fuer mindestens
// self_test_count() Eintraege bieten.
esp_err_t self_test_run_all(self_test_result_t *results, size_t max_results, size_t *out_count);

size_t self_test_count(void);

#ifdef __cplusplus
}
#endif
