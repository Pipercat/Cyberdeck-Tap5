#include "hal_gpio.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "hal_gpio";

typedef struct {
    int gpio_num;
    const char *owner_tag;
} hal_gpio_reservation_t;

// Reservierungstabelle: Groesse = Anzahl freier Pins (grosszuegig dimensioniert).
static hal_gpio_reservation_t s_reservations[32];

static hal_gpio_reservation_t *find_reservation(int gpio_num)
{
    for (size_t i = 0; i < sizeof(s_reservations) / sizeof(s_reservations[0]); i++) {
        if (s_reservations[i].gpio_num == gpio_num) {
            return &s_reservations[i];
        }
    }
    return NULL;
}

static hal_gpio_reservation_t *find_free_slot(void)
{
    return find_reservation(-1);
}

esp_err_t hal_gpio_request(int gpio_num, hal_gpio_mode_t mode, const char *owner_tag)
{
    const pin_descriptor_t *pin = pin_table_find(gpio_num);
    if (pin == NULL) {
        ESP_LOGE(TAG, "GPIO %d ist nicht in der verifizierten Pin-Tabelle - Zugriff verweigert", gpio_num);
        return ESP_ERR_NOT_FOUND;
    }

    if (pin->role == PIN_ROLE_INTERNAL) {
        ESP_LOGE(TAG, "GPIO %d ('%s') ist intern belegt (%s) - Zugriff verweigert",
                 gpio_num, pin->label, pin->internal_function ? pin->internal_function : "?");
        return ESP_ERR_NOT_ALLOWED;
    }
    if (pin->role == PIN_ROLE_UNVERIFIED) {
        ESP_LOGE(TAG, "GPIO %d ist nicht verifiziert - bis zur Bestaetigung gegen die "
                       "Primaerquelle gesperrt", gpio_num);
        return ESP_ERR_INVALID_STATE;
    }
    if (pin->role == PIN_ROLE_SHARED_BUS && mode != HAL_GPIO_MODE_INPUT) {
        ESP_LOGE(TAG, "GPIO %d ('%s') ist ein interner Systembus - nur lesend/diagnostisch",
                 gpio_num, pin->label);
        return ESP_ERR_NOT_ALLOWED;
    }

    bool capable = false;
    switch (mode) {
        case HAL_GPIO_MODE_INPUT:
        case HAL_GPIO_MODE_INPUT_PULLUP:
        case HAL_GPIO_MODE_INPUT_PULLDOWN:
            capable = pin->supports_input; break;
        case HAL_GPIO_MODE_OUTPUT:
            capable = pin->supports_output; break;
        case HAL_GPIO_MODE_PWM:
            capable = pin->supports_pwm; break;
        case HAL_GPIO_MODE_ADC:
            capable = pin->supports_adc; break;
        case HAL_GPIO_MODE_INTERRUPT:
            capable = pin->supports_interrupt; break;
    }
    if (!capable) {
        ESP_LOGE(TAG, "GPIO %d unterstuetzt den angeforderten Modus (%d) nicht", gpio_num, mode);
        return ESP_ERR_NOT_SUPPORTED;
    }

    hal_gpio_reservation_t *existing = find_reservation(gpio_num);
    if (existing != NULL && existing->owner_tag != NULL &&
        (owner_tag == NULL || strcmp(existing->owner_tag, owner_tag) != 0)) {
        ESP_LOGE(TAG, "GPIO %d ist bereits durch '%s' reserviert", gpio_num, existing->owner_tag);
        return ESP_ERR_INVALID_ARG;
    }

    hal_gpio_reservation_t *slot = existing ? existing : find_free_slot();
    if (slot == NULL) {
        ESP_LOGE(TAG, "Reservierungstabelle voll - hal_gpio.c s_reservations vergroessern");
        return ESP_ERR_NO_MEM;
    }
    slot->gpio_num = gpio_num;
    slot->owner_tag = owner_tag;

    ESP_LOGI(TAG, "GPIO %d fuer '%s' reserviert (Modus %d)", gpio_num,
             owner_tag ? owner_tag : "?", mode);
    return ESP_OK;
}

esp_err_t hal_gpio_release(int gpio_num)
{
    hal_gpio_reservation_t *slot = find_reservation(gpio_num);
    if (slot == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "GPIO %d freigegeben (war '%s')", gpio_num, slot->owner_tag ? slot->owner_tag : "?");
    slot->gpio_num = -1;
    slot->owner_tag = NULL;
    return ESP_OK;
}

const char *hal_gpio_owner(int gpio_num)
{
    hal_gpio_reservation_t *slot = find_reservation(gpio_num);
    return slot ? slot->owner_tag : NULL;
}

__attribute__((constructor))
static void hal_gpio_init_reservations(void)
{
    for (size_t i = 0; i < sizeof(s_reservations) / sizeof(s_reservations[0]); i++) {
        s_reservations[i].gpio_num = -1;
        s_reservations[i].owner_tag = NULL;
    }
}
