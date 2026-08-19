/**
 * dashboard.h - Home-Screen: Quick Actions + Modul-Grid (Nutzeranforderung 3, 19).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Registriert den Dashboard-Screen bei der Navigation (NAV_SCREEN_DASHBOARD)
// und alle Quick-Action-Kacheln. Nach nav_init() aufrufen.
void dashboard_register(void);

#ifdef __cplusplus
}
#endif
