#include "usb_host_manager.h"
#include <string.h>
#include "usb/usb_helpers.h"
#include "usb/usb_types_ch9.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "USB_HOST";

#define USB_LIB_TASK_STACK    4096
#define USB_CLIENT_TASK_STACK 4096
#define USB_LIB_TASK_PRIORITY    5
#define USB_CLIENT_TASK_PRIORITY 5

static bool s_initialized = false;
static usb_host_client_handle_t s_client_hdl = NULL;
static TaskHandle_t s_lib_task = NULL;
static TaskHandle_t s_client_task = NULL;

static usb_host_device_info_t s_current_device;
static bool s_device_present = false;

static usb_host_connect_cb_t s_on_connect[USB_HOST_MANAGER_MAX_CALLBACKS];
static usb_host_disconnect_cb_t s_on_disconnect[USB_HOST_MANAGER_MAX_CALLBACKS];
static void *s_cb_ctx[USB_HOST_MANAGER_MAX_CALLBACKS];
static size_t s_cb_count = 0;

// USB-Stringdeskriptoren sind UTF-16LE (USB 2.0 Spec, Kap. 9.6.7). Fuer
// Hersteller-/Produkt-/Seriennummer-Strings genuegt eine ASCII-Untermenge -
// Nicht-ASCII-Zeichen werden bewusst durch '?' ersetzt statt UTF-8 zu
// dekodieren (kein bekannter Bedarf fuer non-ASCII-Geraetenamen bisher).
static void decode_usb_string(const usb_str_desc_t *desc, char *out, size_t out_len)
{
    out[0] = '\0';
    if (desc == NULL || out_len == 0 || desc->bLength < 2) {
        return;
    }
    size_t chars = (desc->bLength - 2) / 2;
    size_t n = (chars < out_len - 1) ? chars : out_len - 1;
    for (size_t i = 0; i < n; i++) {
        uint16_t c = desc->wData[i];
        out[i] = (c > 0 && c < 0x80) ? (char)c : '?';
    }
    out[n] = '\0';
}

// Sucht ueber alle Interfaces der aktiven Konfiguration nach einem CDC-
// Communication-Interface (USB_CLASS_COMM, 0x02) - Kriterium dafuer, ob ein
// USB-Serial-Pfad (siehe usb_serial.h) ueberhaupt in Frage kommt.
static bool config_has_cdc_interface(const usb_config_desc_t *cfg_desc)
{
    if (cfg_desc == NULL) {
        return false;
    }
    int offset = 0;
    for (int i = 0; i < cfg_desc->bNumInterfaces; i++) {
        const usb_intf_desc_t *intf = usb_parse_interface_descriptor(cfg_desc, i, 0, &offset);
        if (intf != NULL && intf->bInterfaceClass == USB_CLASS_COMM) {
            return true;
        }
    }
    return false;
}

static void notify_connect(const usb_host_device_info_t *info)
{
    for (size_t i = 0; i < s_cb_count; i++) {
        if (s_on_connect[i] != NULL) {
            s_on_connect[i](info, s_cb_ctx[i]);
        }
    }
}

static void notify_disconnect(uint8_t dev_addr)
{
    for (size_t i = 0; i < s_cb_count; i++) {
        if (s_on_disconnect[i] != NULL) {
            s_on_disconnect[i](dev_addr, s_cb_ctx[i]);
        }
    }
}

static void handle_new_device(uint8_t dev_addr)
{
    usb_device_handle_t dev_hdl;
    if (usb_host_device_open(s_client_hdl, dev_addr, &dev_hdl) != ESP_OK) {
        ESP_LOGW(TAG, "device_open fuer Addr %u fehlgeschlagen", dev_addr);
        return;
    }

    const usb_device_desc_t *dev_desc = NULL;
    const usb_config_desc_t *cfg_desc = NULL;
    usb_device_info_t info = {0};
    if (usb_host_get_device_descriptor(dev_hdl, &dev_desc) != ESP_OK ||
        usb_host_get_active_config_descriptor(dev_hdl, &cfg_desc) != ESP_OK ||
        usb_host_device_info(dev_hdl, &info) != ESP_OK) {
        ESP_LOGW(TAG, "Deskriptor-Auslesen fuer Addr %u fehlgeschlagen", dev_addr);
        usb_host_device_close(s_client_hdl, dev_hdl);
        return;
    }

    memset(&s_current_device, 0, sizeof(s_current_device));
    s_current_device.dev_addr = dev_addr;
    s_current_device.vid = dev_desc->idVendor;
    s_current_device.pid = dev_desc->idProduct;
    s_current_device.device_class = dev_desc->bDeviceClass;
    s_current_device.device_subclass = dev_desc->bDeviceSubClass;
    s_current_device.device_protocol = dev_desc->bDeviceProtocol;
    s_current_device.dev_hdl = dev_hdl;
    s_current_device.has_cdc_interface = config_has_cdc_interface(cfg_desc);
    decode_usb_string(info.str_desc_manufacturer, s_current_device.manufacturer, sizeof(s_current_device.manufacturer));
    decode_usb_string(info.str_desc_product, s_current_device.product, sizeof(s_current_device.product));
    decode_usb_string(info.str_desc_serial_num, s_current_device.serial, sizeof(s_current_device.serial));
    if (s_current_device.manufacturer[0] == '\0') strcpy(s_current_device.manufacturer, "Unknown");
    if (s_current_device.product[0] == '\0') strcpy(s_current_device.product, "Unknown");
    if (s_current_device.serial[0] == '\0') strcpy(s_current_device.serial, "N/A");

    s_device_present = true;
    ESP_LOGI(TAG, "connected VID=%04x PID=%04x '%s' '%s' cdc=%d",
             s_current_device.vid, s_current_device.pid,
             s_current_device.manufacturer, s_current_device.product,
             s_current_device.has_cdc_interface);
    notify_connect(&s_current_device);
}

static void handle_device_gone(usb_device_handle_t dev_hdl)
{
    if (!s_device_present || s_current_device.dev_hdl != dev_hdl) {
        return;
    }
    uint8_t addr = s_current_device.dev_addr;
    usb_host_device_close(s_client_hdl, dev_hdl);
    memset(&s_current_device, 0, sizeof(s_current_device));
    s_device_present = false;
    ESP_LOGI(TAG, "disconnected addr=%u", addr);
    notify_disconnect(addr);
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    (void)arg;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        handle_new_device(event_msg->new_dev.address);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        handle_device_gone(event_msg->dev_gone.dev_hdl);
        break;
    default:
        break;
    }
}

static void usb_lib_task(void *arg)
{
    (void)arg;
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        // NO_CLIENTS/ALL_FREE werden hier bewusst ignoriert: der Client
        // bleibt fuer die Laufzeit des Geraets registriert, ein
        // usb_host_uninstall() ist fuer diese erste Version nicht vorgesehen
        // (kein dynamisches An-/Abschalten des USB-Hosts noetig).
    }
}

static void usb_client_task(void *arg)
{
    (void)arg;
    while (1) {
        usb_host_client_handle_events(s_client_hdl, portMAX_DELAY);
    }
}

esp_err_t usb_host_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = 0,
    };
    ESP_RETURN_ON_ERROR(usb_host_install(&host_config), TAG, "usb_host_install fehlgeschlagen");

    if (xTaskCreate(usb_lib_task, "usb_lib", USB_LIB_TASK_STACK, NULL, USB_LIB_TASK_PRIORITY, &s_lib_task) != pdPASS) {
        usb_host_uninstall();
        return ESP_ERR_NO_MEM;
    }

    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };
    esp_err_t err = usb_host_client_register(&client_config, &s_client_hdl);
    if (err != ESP_OK) {
        vTaskDelete(s_lib_task);
        usb_host_uninstall();
        return err;
    }

    if (xTaskCreate(usb_client_task, "usb_client", USB_CLIENT_TASK_STACK, NULL, USB_CLIENT_TASK_PRIORITY, &s_client_task) != pdPASS) {
        usb_host_client_deregister(s_client_hdl);
        vTaskDelete(s_lib_task);
        usb_host_uninstall();
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "usb_host installiert, Client registriert");
    return ESP_OK;
}

esp_err_t usb_host_manager_register_callbacks(usb_host_connect_cb_t on_connect,
                                               usb_host_disconnect_cb_t on_disconnect,
                                               void *ctx)
{
    if (s_cb_count >= USB_HOST_MANAGER_MAX_CALLBACKS) {
        return ESP_ERR_NO_MEM;
    }
    s_on_connect[s_cb_count] = on_connect;
    s_on_disconnect[s_cb_count] = on_disconnect;
    s_cb_ctx[s_cb_count] = ctx;
    s_cb_count++;

    // Bereits verbundenes Geraet sofort nachliefern, damit spaet
    // registrierte Abonnenten (z.B. UI-Screen erst nach Boot betreten) nicht
    // auf das naechste physische Connect-Event warten muessen.
    if (s_device_present && on_connect != NULL) {
        on_connect(&s_current_device, ctx);
    }
    return ESP_OK;
}

bool usb_host_manager_get_current_device(usb_host_device_info_t *out)
{
    if (!s_device_present || out == NULL) {
        return false;
    }
    *out = s_current_device;
    return true;
}
