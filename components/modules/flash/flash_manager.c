#include "flash_manager.h"
#include <string.h>
#include <inttypes.h>
#include "usb_host_manager.h"
#include "usb_device_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "FLASH_MGR";

#define WORKER_STACK_SIZE 6144
#define WORKER_PRIORITY   4
#define FLASH_BAUD_RATE   460800

typedef enum {
    CMD_START,
    CMD_CHUNK,
    CMD_FINISH,
    CMD_CANCEL,
} flash_cmd_type_t;

typedef struct {
    flash_cmd_type_t type;
    flash_manifest_t manifest;   // nur fuer CMD_START gefuellt
    size_t   file_index;         // nur fuer CMD_CHUNK
    uint32_t offset;             // nur fuer CMD_CHUNK
    size_t   len;                // nur fuer CMD_CHUNK - Bytes in s_chunk_buf
} flash_cmd_t;

static QueueHandle_t s_cmd_queue = NULL;
static QueueHandle_t s_result_queue = NULL;
static SemaphoreHandle_t s_call_mutex = NULL;
static TaskHandle_t s_worker_task = NULL;

static uint8_t s_chunk_buf[FLASH_CHUNK_MAX_LEN];

static flash_status_t s_status = { .state = FLASH_STATE_IDLE };
static flash_manifest_t s_manifest;
static volatile bool s_target_gone = false;

static flash_manager_status_cb_t s_status_cb = NULL;
static void *s_status_cb_ctx = NULL;

static bool s_initialized = false;

const char *flash_error_str(flash_error_t err)
{
    switch (err) {
    case FLASH_ERR_NONE:                return "None";
    case FLASH_ERR_NO_TARGET:           return "No target connected";
    case FLASH_ERR_UNSUPPORTED_DEVICE:  return "Unsupported USB device";
    case FLASH_ERR_TARGET_DISCONNECTED: return "Target disconnected";
    case FLASH_ERR_BOOTLOADER_TIMEOUT:  return "Unable to enter bootloader";
    case FLASH_ERR_SYNC_TIMEOUT:        return "Sync timeout";
    case FLASH_ERR_ERASE_FAILED:        return "Flash erase failed";
    case FLASH_ERR_WRITE_FAILED:        return "Flash write failed";
    case FLASH_ERR_VERIFY_FAILED:       return "Verification failed";
    case FLASH_ERR_UPLOAD_INTERRUPTED:  return "Upload interrupted";
    case FLASH_ERR_INVALID_MANIFEST:    return "Invalid manifest";
    case FLASH_ERR_WRONG_CHIP:          return "Wrong target chip";
    case FLASH_ERR_BUFFER_OVERFLOW:     return "Buffer overflow prevented";
    case FLASH_ERR_OUT_OF_MEMORY:       return "Out of memory";
    case FLASH_ERR_CANCELLED_BY_USER:   return "Cancelled";
    default:                            return "Internal error";
    }
}

const char *flash_state_str(flash_state_t state)
{
    switch (state) {
    case FLASH_STATE_IDLE:               return "IDLE";
    case FLASH_STATE_TARGET_CONNECTED:   return "TARGET_CONNECTED";
    case FLASH_STATE_PREPARING:          return "PREPARING";
    case FLASH_STATE_ENTERING_BOOTLOADER: return "ENTERING_BOOTLOADER";
    case FLASH_STATE_SYNCING:            return "SYNCING";
    case FLASH_STATE_ERASING:            return "ERASING";
    case FLASH_STATE_FLASHING:           return "FLASHING";
    case FLASH_STATE_VERIFYING:          return "VERIFYING";
    case FLASH_STATE_RESETTING:          return "RESETTING";
    case FLASH_STATE_SUCCESS:            return "SUCCESS";
    case FLASH_STATE_ERROR:              return "ERROR";
    case FLASH_STATE_CANCELLED:          return "CANCELLED";
    default:                             return "?";
    }
}

static void publish_status(void)
{
    s_status.updated_at_us = esp_timer_get_time();
    if (s_status.bytes_total_all > 0) {
        s_status.progress_percent = (int)((s_status.bytes_written_total * 100) / s_status.bytes_total_all);
    }
    if (s_status_cb != NULL) {
        s_status_cb(&s_status, s_status_cb_ctx);
    }
}

static void set_state(flash_state_t state)
{
    s_status.state = state;
    publish_status();
}

static void fail(flash_error_t err, const char *detail)
{
    ESP_LOGE(TAG, "Flash-Fehler: %s (%s)", flash_error_str(err), detail ? detail : "");
    s_status.last_error = err;
    s_status.state = FLASH_STATE_ERROR;
    flash_target_disconnect();
    publish_status();
}

static bool manifest_is_valid(const flash_manifest_t *m)
{
    if (m == NULL || m->file_count == 0 || m->file_count > FLASH_MANIFEST_MAX_FILES) {
        return false;
    }
    for (size_t i = 0; i < m->file_count; i++) {
        if (m->files[i].size == 0 || m->files[i].name[0] == '\0') {
            return false;
        }
    }
    return true;
}

static void handle_start(const flash_manifest_t *manifest)
{
    if (!manifest_is_valid(manifest)) {
        fail(FLASH_ERR_INVALID_MANIFEST, "Manifest-Validierung fehlgeschlagen");
        return;
    }

    usb_device_manager_target_t target;
    usb_device_manager_get_target(&target);
    if (!target.connected) {
        fail(FLASH_ERR_NO_TARGET, "Kein USB-Geraet angeschlossen");
        return;
    }
    if (!target.flash_supported) {
        fail(FLASH_ERR_UNSUPPORTED_DEVICE, target.bridge_label ? target.bridge_label : "?");
        return;
    }

    memset(&s_status, 0, sizeof(s_status));
    s_manifest = *manifest;
    s_status.file_count = manifest->file_count;
    for (size_t i = 0; i < manifest->file_count; i++) {
        s_status.bytes_total_all += manifest->files[i].size;
    }
    s_status.started_at_us = esp_timer_get_time();
    s_target_gone = false;
    set_state(FLASH_STATE_PREPARING);

    set_state(FLASH_STATE_ENTERING_BOOTLOADER);
    flash_target_chip_t chip;
    esp_err_t err = flash_target_connect(FLASH_BAUD_RATE, &chip);
    if (err == ESP_ERR_TIMEOUT) {
        fail(FLASH_ERR_SYNC_TIMEOUT, "esp_loader_connect Timeout - manueller Bootloader-Einstieg noetig");
        return;
    }
    if (err != ESP_OK) {
        fail(FLASH_ERR_BOOTLOADER_TIMEOUT, esp_err_to_name(err));
        return;
    }
    s_status.detected_chip = chip;
    set_state(FLASH_STATE_SYNCING);

    // Erster Dateibereich wird sofort eroeffnet (ERASING) - der Client
    // schickt direkt danach Chunks fuer file_index 0.
    strncpy(s_status.current_file, manifest->files[0].name, sizeof(s_status.current_file) - 1);
    s_status.current_address = manifest->files[0].address;
    s_status.bytes_total_current_file = manifest->files[0].size;
    s_status.bytes_written_current_file = 0;
    set_state(FLASH_STATE_ERASING);
    err = flash_target_begin_region(manifest->files[0].address, manifest->files[0].size);
    if (err != ESP_OK) {
        fail(FLASH_ERR_ERASE_FAILED, esp_err_to_name(err));
        return;
    }
    set_state(FLASH_STATE_FLASHING);
}

static void handle_chunk(size_t file_index, uint32_t offset, const uint8_t *data, size_t len)
{
    if (s_status.state != FLASH_STATE_FLASHING) {
        fail(FLASH_ERR_UPLOAD_INTERRUPTED, "Chunk ausserhalb FLASHING-Zustand empfangen");
        return;
    }
    if (s_target_gone) {
        fail(FLASH_ERR_TARGET_DISCONNECTED, "USB-Ziel waehrend Upload getrennt");
        return;
    }
    if (file_index != s_status.file_index || file_index >= s_manifest.file_count) {
        fail(FLASH_ERR_UPLOAD_INTERRUPTED, "Chunk fuer unerwarteten Dateiindex");
        return;
    }
    if (offset != s_status.bytes_written_current_file) {
        fail(FLASH_ERR_UPLOAD_INTERRUPTED, "Chunk-Sequenzfehler (Offset stimmt nicht)");
        return;
    }
    if ((uint64_t)offset + len > s_manifest.files[file_index].size) {
        fail(FLASH_ERR_BUFFER_OVERFLOW, "Chunk wuerde Dateigroesse ueberschreiten");
        return;
    }

    esp_err_t err = flash_target_write_chunk(data, len);
    if (err != ESP_OK) {
        fail(FLASH_ERR_WRITE_FAILED, esp_err_to_name(err));
        return;
    }

    s_status.bytes_written_current_file += len;
    s_status.bytes_written_total += len;
    publish_status();

    if (s_status.bytes_written_current_file == s_manifest.files[file_index].size) {
        set_state(FLASH_STATE_VERIFYING);
        err = flash_target_end_region();
        if (err != ESP_OK) {
            fail(FLASH_ERR_VERIFY_FAILED, esp_err_to_name(err));
            return;
        }

        size_t next = file_index + 1;
        if (next < s_manifest.file_count) {
            s_status.file_index = next;
            strncpy(s_status.current_file, s_manifest.files[next].name, sizeof(s_status.current_file) - 1);
            s_status.current_address = s_manifest.files[next].address;
            s_status.bytes_total_current_file = s_manifest.files[next].size;
            s_status.bytes_written_current_file = 0;
            set_state(FLASH_STATE_ERASING);
            err = flash_target_begin_region(s_manifest.files[next].address, s_manifest.files[next].size);
            if (err != ESP_OK) {
                fail(FLASH_ERR_ERASE_FAILED, esp_err_to_name(err));
                return;
            }
            set_state(FLASH_STATE_FLASHING);
        } else {
            set_state(FLASH_STATE_TARGET_CONNECTED);  // alle Dateien geschrieben, wartet auf flash_manager_finish()
        }
    }
}

static void handle_finish(void)
{
    if (s_status.state != FLASH_STATE_TARGET_CONNECTED) {
        fail(FLASH_ERR_UPLOAD_INTERRUPTED, "finish() vor vollstaendigem Upload aufgerufen");
        return;
    }
    set_state(FLASH_STATE_RESETTING);
    esp_err_t err = flash_target_finish(true);
    if (err != ESP_OK) {
        fail(FLASH_ERR_INTERNAL, esp_err_to_name(err));
        return;
    }
    set_state(FLASH_STATE_SUCCESS);
    ESP_LOGI(TAG, "Flash-Vorgang erfolgreich abgeschlossen (%" PRIu64 " Bytes)", s_status.bytes_written_total);
}

static void handle_cancel(void)
{
    flash_target_disconnect();
    s_status.last_error = FLASH_ERR_CANCELLED_BY_USER;
    set_state(FLASH_STATE_CANCELLED);
}

static void worker_task(void *arg)
{
    (void)arg;
    flash_cmd_t cmd;
    while (1) {
        if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        esp_err_t result = ESP_OK;
        switch (cmd.type) {
        case CMD_START:
            handle_start(&cmd.manifest);
            break;
        case CMD_CHUNK:
            handle_chunk(cmd.file_index, cmd.offset, s_chunk_buf, cmd.len);
            break;
        case CMD_FINISH:
            handle_finish();
            break;
        case CMD_CANCEL:
            handle_cancel();
            break;
        }
        if (s_status.state == FLASH_STATE_ERROR) {
            result = ESP_FAIL;
        }
        xQueueSend(s_result_queue, &result, portMAX_DELAY);
    }
}

static void on_usb_disconnect(uint8_t dev_addr, void *ctx)
{
    (void)dev_addr;
    (void)ctx;
    if (s_status.state != FLASH_STATE_IDLE && s_status.state != FLASH_STATE_SUCCESS &&
        s_status.state != FLASH_STATE_ERROR && s_status.state != FLASH_STATE_CANCELLED) {
        s_target_gone = true;
    }
}

esp_err_t flash_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = usb_device_manager_init();
    if (err != ESP_OK) {
        return err;
    }
    usb_host_manager_register_callbacks(NULL, on_usb_disconnect, NULL);

    s_cmd_queue = xQueueCreate(1, sizeof(flash_cmd_t));
    s_result_queue = xQueueCreate(1, sizeof(esp_err_t));
    s_call_mutex = xSemaphoreCreateMutex();
    if (s_cmd_queue == NULL || s_result_queue == NULL || s_call_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(worker_task, "flash_worker", WORKER_STACK_SIZE, NULL, WORKER_PRIORITY, &s_worker_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t flash_manager_register_status_callback(flash_manager_status_cb_t cb, void *ctx)
{
    s_status_cb = cb;
    s_status_cb_ctx = ctx;
    return ESP_OK;
}

static esp_err_t submit_and_wait(flash_cmd_t *cmd, TickType_t timeout)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_call_mutex, timeout) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = ESP_FAIL;
    if (xQueueSend(s_cmd_queue, cmd, timeout) != pdTRUE ||
        xQueueReceive(s_result_queue, &result, portMAX_DELAY) != pdTRUE) {
        result = ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(s_call_mutex);
    return result;
}

esp_err_t flash_manager_start(const flash_manifest_t *manifest)
{
    if (manifest == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_status.state != FLASH_STATE_IDLE && s_status.state != FLASH_STATE_SUCCESS &&
        s_status.state != FLASH_STATE_ERROR && s_status.state != FLASH_STATE_CANCELLED) {
        return ESP_ERR_INVALID_STATE;
    }
    flash_cmd_t cmd = { .type = CMD_START };
    cmd.manifest = *manifest;
    return submit_and_wait(&cmd, pdMS_TO_TICKS(30000));
}

esp_err_t flash_manager_write_chunk(size_t file_index, uint32_t offset, const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0 || len > FLASH_CHUNK_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_call_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memcpy(s_chunk_buf, data, len);
    flash_cmd_t cmd = { .type = CMD_CHUNK, .file_index = file_index, .offset = offset, .len = len };
    esp_err_t result = ESP_FAIL;
    if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(5000)) != pdTRUE ||
        xQueueReceive(s_result_queue, &result, portMAX_DELAY) != pdTRUE) {
        result = ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(s_call_mutex);
    return result;
}

esp_err_t flash_manager_finish(void)
{
    flash_cmd_t cmd = { .type = CMD_FINISH };
    return submit_and_wait(&cmd, pdMS_TO_TICKS(15000));
}

esp_err_t flash_manager_cancel(void)
{
    flash_cmd_t cmd = { .type = CMD_CANCEL };
    return submit_and_wait(&cmd, pdMS_TO_TICKS(5000));
}

void flash_manager_get_status(flash_status_t *out)
{
    if (out != NULL) {
        *out = s_status;
    }
}
