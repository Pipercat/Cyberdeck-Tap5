/**
 * remote_events.h - Event-Bus zwischen USB/Flash-Schicht und den WebSocket-
 * Endpunkten des Remote-Servers (Architektur Abschnitt 10/12).
 *
 * Abonniert usb_host_manager/flash_manager/usb_serial-Callbacks EINMALIG
 * (remote_events_init()) und uebersetzt sie in das versionierte JSON-Event-
 * Protokoll (siehe docs/remote_protocol.md), das per httpd_queue_work() +
 * httpd_ws_send_frame_async() sicher aus fremden Task-Kontexten (Flash
 * Worker, USB-Client-Task) an alle verbundenen WebSocket-Clients verteilt
 * wird - das ist der von esp_http_server dafuer vorgesehene Mechanismus,
 * kein eigenes Nebenlaeufigkeits-Konstrukt.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REMOTE_EVENTS_MAX_WS_CLIENTS     4
#define REMOTE_EVENTS_MAX_SERIAL_CLIENTS 1   // usb_serial.h erlaubt ohnehin nur eine aktive Sitzung

esp_err_t remote_events_init(httpd_handle_t server);

void remote_events_register_ws_client(int fd);
void remote_events_unregister_ws_client(int fd);

void remote_events_register_serial_client(int fd);
void remote_events_unregister_serial_client(int fd);

// Fuer Endpunkte ausserhalb dieser Datei, die ein zusaetzliches Event
// broadcasten wollen (z.B. remote_server.c bei client.paired).
void remote_events_broadcast_json_owned(char *json_str);  // uebernimmt Ownership, ruft free()

#ifdef __cplusplus
}
#endif
