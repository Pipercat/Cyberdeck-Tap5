#include "self_test.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "self_test";

#define MAX_TESTS 32

typedef struct {
    const char *name;
    self_test_fn_t fn;
} registered_test_t;

static registered_test_t s_tests[MAX_TESTS];
static size_t s_test_count = 0;

esp_err_t self_test_register(const char *name, self_test_fn_t fn)
{
    if (name == NULL || fn == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_test_count >= MAX_TESTS) {
        ESP_LOGE(TAG, "MAX_TESTS erreicht - self_test.c vergroessern");
        return ESP_ERR_NO_MEM;
    }
    s_tests[s_test_count].name = name;
    s_tests[s_test_count].fn = fn;
    s_test_count++;
    return ESP_OK;
}

esp_err_t self_test_run_all(self_test_result_t *results, size_t max_results, size_t *out_count)
{
    if (results == NULL || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t n = s_test_count < max_results ? s_test_count : max_results;
    for (size_t i = 0; i < n; i++) {
        const char *detail = NULL;
        self_test_status_t status = s_tests[i].fn(&detail);
        results[i].name = s_tests[i].name;
        results[i].status = status;
        results[i].detail = detail;
        ESP_LOGI(TAG, "%s: %d %s", s_tests[i].name, status, detail ? detail : "");
    }
    *out_count = n;
    return ESP_OK;
}

size_t self_test_count(void)
{
    return s_test_count;
}
