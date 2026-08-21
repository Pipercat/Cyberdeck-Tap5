/**
 * flash_ui.h - Tab5-Bildschirm fuer den Flash-Bereich (Nutzeranforderung 18).
 *
 * Da das CyberDeck bewusst keine lokale Firmware-Ablage besitzt (kein
 * SPIFFS/SD, siehe docs/hardware_reference.md/remote_flashing.md), zeigt
 * dieser Screen Zielgeraet + Live-Fortschritt eines von einem Remote-Client
 * gestarteten Flash-Vorgangs (Architektur Abschnitt 2/21) - keine lokale
 * "Datei auswaehlen"-Funktion, die es mangels Dateisystem nicht geben kann.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void flash_module_ui_register(void);

#ifdef __cplusplus
}
#endif
