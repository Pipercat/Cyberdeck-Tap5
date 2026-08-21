#include "usb_device_manager.h"
#include <string.h>
#include "usb_host_manager.h"
#include "esp_log.h"

static const char *TAG = "USB_DEVICE";

// Bekannte VID/PID-Paare fuer serielle USB-Bruecken. Espressif-Chips mit
// eingebautem USB-Serial/JTAG teilen sich alle denselben Eintrag (siehe
// Header-Kommentar) - Werte aus esptool.py/tinyusb bekannten Geraetelisten,
// nicht auf echter Hardware gegen jeden einzelnen Chip verifiziert (siehe
// docs/usb_host.md "Bekannte Einschraenkungen").
typedef struct {
    uint16_t vid, pid;
    usb_bridge_kind_t kind;
    const char *label;
} known_bridge_t;

static const known_bridge_t k_known_bridges[] = {
    { 0x303A, 0x1001, USB_BRIDGE_KIND_NATIVE_CDC, "Espressif USB-Serial/JTOG" },
    { 0x303A, 0x0002, USB_BRIDGE_KIND_NATIVE_CDC, "Espressif USB-CDC (natives USB-OTG)" },
    { 0x10C4, 0xEA60, USB_BRIDGE_KIND_CP210X,     "Silicon Labs CP210x" },
    { 0x1A86, 0x7523, USB_BRIDGE_KIND_CH34X,      "WCH CH340" },
    { 0x1A86, 0x55D4, USB_BRIDGE_KIND_CH34X,      "WCH CH9102" },
    { 0x0403, 0x6001, USB_BRIDGE_KIND_FTDI,       "FTDI FT232R" },
    { 0x0403, 0x6015, USB_BRIDGE_KIND_FTDI,       "FTDI FT230X" },
};
#define KNOWN_BRIDGE_COUNT (sizeof(k_known_bridges) / sizeof(k_known_bridges[0]))

static usb_device_manager_target_t s_target;
static bool s_initialized = false;

static const known_bridge_t *lookup_bridge(uint16_t vid, uint16_t pid)
{
    for (size_t i = 0; i < KNOWN_BRIDGE_COUNT; i++) {
        if (k_known_bridges[i].vid == vid && k_known_bridges[i].pid == pid) {
            return &k_known_bridges[i];
        }
    }
    return NULL;
}

static void classify(const usb_host_device_info_t *info, usb_device_manager_target_t *out)
{
    memset(out, 0, sizeof(*out));
    out->connected = true;
    out->vid = info->vid;
    out->pid = info->pid;
    strncpy(out->manufacturer, info->manufacturer, sizeof(out->manufacturer) - 1);
    strncpy(out->product, info->product, sizeof(out->product) - 1);
    strncpy(out->serial, info->serial, sizeof(out->serial) - 1);

    const known_bridge_t *match = lookup_bridge(info->vid, info->pid);
    if (match != NULL) {
        out->bridge_kind = match->kind;
        out->bridge_label = match->label;
    } else if (info->has_cdc_interface) {
        out->bridge_kind = USB_BRIDGE_KIND_UNKNOWN;
        out->bridge_label = "Unbekannte CDC-ACM-Bruecke";
    } else {
        out->bridge_kind = USB_BRIDGE_KIND_NONE;
        out->bridge_label = "Kein serieller Pfad erkannt";
    }

    // Nur der native Espressif-Pfad (cdc_acm_host, Klasse USB_CLASS_COMM)
    // ist in dieser Version tatsaechlich verdrahtet - siehe usb_serial.c und
    // docs/usb_host.md. Vendor-Bruecken (CP210x/CH34x/FTDI) werden zwar
    // erkannt/angezeigt, aber bewusst als nicht unterstuetzt gemeldet statt
    // eine Funktion vorzutaeuschen, die nicht getestet ist (Abschnitt 42).
    out->serial_supported = (out->bridge_kind == USB_BRIDGE_KIND_NATIVE_CDC) && info->has_cdc_interface;
    out->flash_supported = out->serial_supported;
}

static void on_usb_connect(const usb_host_device_info_t *info, void *ctx)
{
    (void)ctx;
    classify(info, &s_target);
    ESP_LOGI(TAG, "Target klassifiziert: %s (%s) supported=%d",
             s_target.product, s_target.bridge_label, s_target.serial_supported);
}

static void on_usb_disconnect(uint8_t dev_addr, void *ctx)
{
    (void)ctx;
    (void)dev_addr;
    memset(&s_target, 0, sizeof(s_target));
    ESP_LOGI(TAG, "Target getrennt");
}

esp_err_t usb_device_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    esp_err_t err = usb_host_manager_init();
    if (err != ESP_OK) {
        return err;
    }
    err = usb_host_manager_register_callbacks(on_usb_connect, on_usb_disconnect, NULL);
    if (err != ESP_OK) {
        return err;
    }
    s_initialized = true;
    return ESP_OK;
}

void usb_device_manager_get_target(usb_device_manager_target_t *out)
{
    if (out == NULL) {
        return;
    }
    *out = s_target;
}
