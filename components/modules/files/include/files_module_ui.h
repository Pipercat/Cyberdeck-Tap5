/**
 * files_module_ui.h - Files-Screen: Liste der Dateien auf dem SPIFFS-Storage
 * (/storage), Textvorschau, Loeschen. Hochladen erfolgt ueber die HTTP-API
 * (core/network/http_server.h) von einem PC/Handy im selben Netz - dieser
 * Screen ist bewusst nur Browser/Verwalter, keine On-Device-Texterstellung.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void files_module_ui_register(void);

#ifdef __cplusplus
}
#endif
