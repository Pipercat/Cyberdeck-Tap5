/**
 * serial_module.h - UART-Terminal-Logik (Nutzeranforderung 11) auf frei
 * waehlbaren GPIOs (siehe pin_table.c). Nutzt UART_NUM_1 - UART_NUM_0 bleibt
 * für die System-Konsole reserviert und wird hier nie angefasst.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Startet den UART-Treiber auf den angegebenen Pins. Ein vorheriger Start
// wird automatisch gestoppt.
esp_err_t serial_module_start(int tx_gpio, int rx_gpio, int baud_rate);

esp_err_t serial_module_stop(void);

bool serial_module_is_running(void);

esp_err_t serial_module_write(const uint8_t *data, size_t len);

// Nicht blockierend: liest bis zu max_len Bytes, die seit dem letzten Aufruf
// empfangen wurden. Rueckgabe: Anzahl gelesener Bytes (kann 0 sein).
size_t serial_module_read(uint8_t *buf, size_t max_len);

#ifdef __cplusplus
}
#endif
