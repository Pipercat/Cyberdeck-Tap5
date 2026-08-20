#include <inttypes.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_heap_caps.h"
#include "board_init.h"
#include "settings.h"
#include "log_sink.h"
#include "nav.h"
#include "dashboard.h"
#include "gpio_module_ui.h"
#include "pwm_module_ui.h"
#include "adc_module_ui.h"
#include "i2c_module_ui.h"
#include "serial_module_ui.h"
#include "system_module.h"
#include "system_module_ui.h"
#include "audio_module_ui.h"
#include "settings_module_ui.h"
#include "network_module_ui.h"
#include "wifi_module.h"
#include "sensors_module_ui.h"
#include "statusbar.h"
#include "esp_lvgl_port.h"

static const char *TAG = "main";

#define LOG_SINK_CAPACITY_BYTES (32 * 1024)

// FIX (esp_hosted/TCM boot-crash, siehe docs/hardware_reference.md -
// Abschnitt "esp_hosted / Binary Layout Boot Crash"):
//
// FreeRTOS erzeugt seinen Timer-Service-Task lazy beim allerersten
// Software-Timer-Gebrauch. ESP-IDFs vApplicationGetTimerTaskMemory()
// alloziert TCB/Stack dafuer via pvPortMalloc() (MALLOC_CAP_INTERNAL|
// MALLOC_CAP_8BIT). Auf dem ESP32-P4 ist TCM (7 KB, siehe soc_memory_types[]
// in esp-idf/components/heap/port/esp32p4/memory_layout.c) mit genau dieser
// Capability-Kombination als Fallback-Pool eingetragen - landet die
// Allokation dort, lehnt FreeRTOS' eigener Zeiger-Gueltigkeitscheck
// (xPortCheckValidTCBMem/xPortcheckValidStackMem, siehe heap_idf.c) sie ab,
// weil esp_ptr_internal() TCM-Adressen NICHT als internes RAM erkennt -
// Boot-Crash, reproduzierbar auf echter Hardware verifiziert (siehe Doku).
//
// esp_hosteds eigener Konstruktor (unpriorisiert, siehe
// esp_hosted_host_init.c) loest ueber add_esp_wifi_remote_channels()/
// rpc_register_event_callbacks() typischerweise die ERSTE Timer-Nutzung im
// gesamten Boot aus - und damit genau diese fragile Allokation.
//
// Erster Fix-Versuch (Timer-Task per priorisiertem Konstruktor VOR
// esp_hosted erzwingen, in der Annahme "frueher = weniger fragmentiert")
// hat NICHT funktioniert: es landete weiterhin ein Puffer in TCM (beim
// ersten Versuch der TCB, danach der Stack) - TCM wird also unabhaengig vom
// Zeitpunkt fuer Allokationen dieser Groessenordnung herangezogen, nicht nur
// bei hoher Fragmentierung. Siehe docs/hardware_reference.md fuer Details.
//
// Tatsaechlicher Fix: TCM komplett und dauerhaft belegen, BEVOR irgendeine
// andere Allokation stattfinden kann (Konstruktor-Prioritaet 101, die
// niedrigste vom Nutzer erlaubte Zahl - laeuft vor esp_hosteds
// unpriorisiertem Konstruktor). Der reservierte Block wird nie freigegeben.
// Damit hat der Heap-Allocator fuer MALLOC_CAP_INTERNAL-Anfragen (die TCM
// als Fallback mit einschliessen, siehe soc_memory_types[] in
// esp-idf/components/heap/port/esp32p4/memory_layout.c) schlicht keinen
// TCM-Platz mehr zur Auswahl und muss auf den grossen Haupt-SRAM-Pool
// ausweichen - unabhaengig von Allokationsgroesse/-zeitpunkt/Fragmentierung.
// TCM wird sonst im Projekt nirgends genutzt, der Verlust der 7 KB ist ein
// bewusster, dokumentierter Kompromiss.
static void __attribute__((constructor(101))) reserve_tcm_to_avoid_esp_hosted_crash(void)
{
    size_t tcm_free = heap_caps_get_free_size(MALLOC_CAP_TCM);
    if (tcm_free > 0) {
        // Kleiner Sicherheitsabstand fuer Allocator-Overhead/Alignment -
        // ein paar Byte TCM-Rest sind unkritisch, solange keine Allokation
        // in der Groessenordnung von TCB/Timer-Stack mehr hineinpasst.
        void *reserved = heap_caps_malloc(tcm_free > 64 ? tcm_free - 64 : tcm_free, MALLOC_CAP_TCM);
        (void)reserved;  // absichtlich nie freigegeben
    }

    TimerHandle_t warmup = xTimerCreate("warmup", pdMS_TO_TICKS(1), pdFALSE, NULL, NULL);
    if (warmup != NULL) {
        xTimerDelete(warmup, 0);
    }
}

// Speist die persistente Statusleiste mit echten Werten (Akku/Wi-Fi),
// soweit bereits verfuegbar. Server/USB-Target bleiben "--", bis die
// jeweiligen Module (Phase 5) existieren - bewusst keine erfundenen
// Platzhalterwerte. Wi-Fi bleibt "--", bis der Nutzer den Network-Screen
// mindestens einmal geoeffnet hat (wifi_module_init() laeuft dort bewusst
// lazy, nicht beim Boot - SDIO-Bring-up zum C6 ist eine potenziell riskante
// Hardware-Operation, siehe docs/hardware_reference.md). RAM/CPU nicht mehr
// hier (kompakte Statusleiste, Nutzervorgabe) - weiterhin im System-Screen.
static void statusbar_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    system_module_stats_t stats;
    if (system_module_get_stats(&stats) != ESP_OK) {
        return;
    }
    statusbar_set_battery_voltage(stats.battery_voltage_v, stats.battery_voltage_v >= 0.0f);
    statusbar_set_wifi(wifi_module_get_rssi(), wifi_module_get_state() == WIFI_MODULE_STATE_CONNECTED);
}

void app_main(void)
{
    ESP_ERROR_CHECK(log_sink_init(LOG_SINK_CAPACITY_BYTES));

    ESP_LOGI(TAG, "CyberDeck Tab5 - Phase 1 Foundation Build");
    ESP_ERROR_CHECK(settings_init());
    ESP_ERROR_CHECK(board_init());
    system_module_init();

    const settings_t *cfg = settings_get();
    ESP_LOGI(TAG, "Settings geladen: schema=%" PRIu32 " backlight=%u%%",
             cfg->schema_version, cfg->backlight_percent);
    ESP_ERROR_CHECK(board_set_backlight(cfg->backlight_percent));

    // LVGL-Objekte duerfen nur unter Lock erzeugt werden (esp_lvgl_port
    // haelt einen eigenen Task fuer den Render-/Input-Loop).
    lvgl_port_lock(0);
    nav_init();
    dashboard_register();
    gpio_module_ui_register();
    pwm_module_ui_register();
    adc_module_ui_register();
    i2c_module_ui_register();
    serial_module_ui_register();
    system_module_ui_register();
    audio_module_ui_register();
    settings_module_ui_register();
    network_module_ui_register();
    sensors_module_ui_register();
    nav_show(NAV_SCREEN_DASHBOARD);
    lv_timer_create(statusbar_refresh_timer_cb, 3000, NULL);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Dashboard angezeigt - Phase 1 Foundation bereit");
}
