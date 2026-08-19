/**
 * board_init.c - Display (MIPI-DSI), Touch (I2C), IO-Expander und LVGL-Bring-up.
 *
 * Alle Konfigurationswerte (DSI-Lane-Bitrate, DPI-Timing, IO-Expander-
 * Register/Bitmuster, Panel-Typ-Erkennung) sind 1:1 aus dem offiziellen
 * M5Stack-Werksfirmware-Repository uebernommen und NICHT erfunden:
 *   github.com/m5stack/M5Tab5-UserDemo
 *   platforms/tab5/components/m5stack_tab5/m5stack_tab5.c
 *
 * Auf echter Hardware verifiziert (Phase 1, 2026-08-19): Die Tab5-Displays
 * gibt es in mehreren Varianten (ILI9881C+GT911 "Gen1", ST7121, ST7123),
 * die zur Laufzeit per I2C-Sondierung erkannt werden - die urspruengliche
 * Annahme "immer ST7121" war falsch und fuehrte zu einem haengenden
 * esp_lcd_panel_init() (Panel blieb im Hardware-Reset, weil der Reset ueber
 * den PI4IOE5V6408-IO-Expander laeuft, nicht ueber ein Host-GPIO).
 */
#include "board_init.h"
#include "hal_gpio.h"
#include "pin_table.h"
#include "io_expander.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_st7121.h"
#include "esp_lcd_st7123.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_st7123.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board_init";

// Physische Panel-Aufloesung: der DSI-Raster ist PORTRAIT (720x1280) - das
// Tab5-Geraet ist als Landschaftstablet montiert, die Software rotiert per
// LVGL-Software-Rotation um 90 Grad (siehe init_lvgl()). Quelle: BSP_LCD_H_RES/
// V_RES aus M5Tab5-UserDemo, display.h.
#define TAB5_PANEL_H_RES  720
#define TAB5_PANEL_V_RES  1280
#define TAB5_MIPI_DSI_PHY_LDO_CHAN  3
#define TAB5_MIPI_DSI_PHY_LDO_MV    2500
#define TAB5_DSI_LANE_BITRATE_MBPS  965
#define TAB5_DPI_CLOCK_MHZ          70

#define TAB5_SYS_I2C_SDA  GPIO_NUM_31
#define TAB5_SYS_I2C_SCL  GPIO_NUM_32
#define TAB5_SYS_I2C_PORT I2C_NUM_0

#define TAB5_LCD_BACKLIGHT_GPIO   GPIO_NUM_22
#define TAB5_BACKLIGHT_LEDC_TIMER LEDC_TIMER_0
#define TAB5_BACKLIGHT_LEDC_CH    LEDC_CHANNEL_0
#define TAB5_BACKLIGHT_LEDC_RES   LEDC_TIMER_10_BIT

#define TAB5_TOUCH_GT911_BACKUP_ADDR 0x14  // sekundaere GT911-Adresse (Gen1-Geraete)

typedef enum {
    TAB5_DISPLAY_UNKNOWN = 0,
    TAB5_DISPLAY_GEN1_ILI9881C_GT911,
    TAB5_DISPLAY_ST7121,
    TAB5_DISPLAY_ST7123,
} tab5_display_type_t;

static esp_ldo_channel_handle_t s_ldo_mipi_phy = NULL;
static esp_lcd_dsi_bus_handle_t s_mipi_dsi_bus = NULL;
static esp_lcd_panel_io_handle_t s_mipi_dbi_io = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static i2c_master_bus_handle_t s_sys_i2c_bus = NULL;
static esp_lcd_panel_io_handle_t s_touch_io = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static lv_display_t *s_lvgl_disp = NULL;
static lv_indev_t *s_lvgl_touch = NULL;
static tab5_display_type_t s_display_type = TAB5_DISPLAY_UNKNOWN;

// Vollstaendige ST7123-Init-Sequenz, 1:1 aus M5Tab5-UserDemo uebernommen
// (m5stack_tab5.c, st7123_vendor_specific_init_default). ST7121 braucht laut
// derselben Referenz KEINE Zusatzkommandos (init_cmds = NULL).
static const st7123_lcd_init_cmd_t k_st7123_init_cmds[] = {
    {0x60, (uint8_t[]){0x71, 0x23, 0xa2}, 3, 0},
    {0x60, (uint8_t[]){0x71, 0x23, 0xa3}, 3, 0},
    {0x60, (uint8_t[]){0x71, 0x23, 0xa4}, 3, 0},
    {0xA4, (uint8_t[]){0x31}, 1, 0},
    {0xD7, (uint8_t[]){0x10, 0x0A, 0x10, 0x2A, 0x80, 0x80}, 6, 0},
    {0x90, (uint8_t[]){0x71, 0x23, 0x5A, 0x20, 0x24, 0x09, 0x09}, 7, 0},
    {0xA3, (uint8_t[]){0x80, 0x01, 0x88, 0x30, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x00, 0x00,
                       0x1E, 0x5C, 0x1E, 0x80, 0x00, 0x4F, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46,
                       0x00, 0x00, 0x1E, 0x5C, 0x1E, 0x80, 0x00, 0x6F, 0x58, 0x00, 0x00, 0x00, 0xFF},
     40, 0},
    {0xA6, (uint8_t[]){0x03, 0x00, 0x24, 0x55, 0x36, 0x00, 0x39, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0x24,
                       0x55, 0x38, 0x00, 0x37, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0x24, 0x11, 0x00, 0x00,
                       0x00, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0xEC, 0x11, 0x00, 0x03, 0x00, 0x03, 0x6E,
                       0x6E, 0xFF, 0xFF, 0x00, 0x08, 0x80, 0x08, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00},
     55, 0},
    {0xA7, (uint8_t[]){0x19, 0x19, 0x80, 0x64, 0x40, 0x07, 0x16, 0x40, 0x00, 0x44, 0x03, 0x6E, 0x6E, 0x91, 0xFF,
                       0x08, 0x80, 0x64, 0x40, 0x25, 0x34, 0x40, 0x00, 0x02, 0x01, 0x6E, 0x6E, 0x91, 0xFF, 0x08,
                       0x80, 0x64, 0x40, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x08, 0x80,
                       0x64, 0x40, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x6E, 0x6E, 0x84, 0xFF, 0x08, 0x80, 0x44},
     60, 0},
    {0xAC, (uint8_t[]){0x03, 0x19, 0x19, 0x18, 0x18, 0x06, 0x13, 0x13, 0x11, 0x11, 0x08, 0x08, 0x0A, 0x0A, 0x1C,
                       0x1C, 0x07, 0x07, 0x00, 0x00, 0x02, 0x02, 0x01, 0x19, 0x19, 0x18, 0x18, 0x06, 0x12, 0x12,
                       0x10, 0x10, 0x09, 0x09, 0x0B, 0x0B, 0x1C, 0x1C, 0x07, 0x07, 0x03, 0x03, 0x01, 0x01},
     44, 0},
    {0xAD, (uint8_t[]){0xF0, 0x00, 0x46, 0x00, 0x03, 0x50, 0x50, 0xFF, 0xFF, 0xF0, 0x40, 0x06, 0x01,
                       0x07, 0x42, 0x42, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF},
     25, 0},
    {0xAE, (uint8_t[]){0xFE, 0x3F, 0x3F, 0xFE, 0x3F, 0x3F, 0x00}, 7, 0},
    {0xB2,
     (uint8_t[]){0x15, 0x19, 0x05, 0x23, 0x49, 0xAF, 0x03, 0x2E, 0x5C, 0xD2, 0xFF, 0x10, 0x20, 0xFD, 0x20, 0xC0, 0x00},
     17, 0},
    {0xE8, (uint8_t[]){0x20, 0x6F, 0x04, 0x97, 0x97, 0x3E, 0x04, 0xDC, 0xDC, 0x3E, 0x06, 0xFA, 0x26, 0x3E}, 15, 0},
    {0x75, (uint8_t[]){0x03, 0x04}, 2, 0},
    {0xE7, (uint8_t[]){0x3B, 0x00, 0x00, 0x7C, 0xA1, 0x8C, 0x20, 0x1A, 0xF0, 0xB1, 0x50, 0x00,
                       0x50, 0xB1, 0x50, 0xB1, 0x50, 0xD8, 0x00, 0x55, 0x00, 0xB1, 0x00, 0x45,
                       0xC9, 0x6A, 0xFF, 0x5A, 0xD8, 0x18, 0x88, 0x15, 0xB1, 0x01, 0x01, 0x77},
     36, 0},
    {0xEA, (uint8_t[]){0x13, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x2C}, 8, 0},
    {0xB0, (uint8_t[]){0x22, 0x43, 0x11, 0x61, 0x25, 0x43, 0x43}, 7, 0},
    {0xb7, (uint8_t[]){0x00, 0x00, 0x73, 0x73}, 0x04, 0},
    {0xBF, (uint8_t[]){0xA6, 0XAA}, 2, 0},
    {0xA9, (uint8_t[]){0x00, 0x00, 0x73, 0xFF, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03}, 10, 0},
    {0xC8, (uint8_t[]){0x00, 0x00, 0x10, 0x1F, 0x36, 0x00, 0x5D, 0x04, 0x9D, 0x05, 0x10, 0xF2, 0x06,
                       0x60, 0x03, 0x11, 0xAD, 0x00, 0xEF, 0x01, 0x22, 0x2E, 0x0E, 0x74, 0x08, 0x32,
                       0xDC, 0x09, 0x33, 0x0F, 0xF3, 0x77, 0x0D, 0xB0, 0xDC, 0x03, 0xFF},
     37, 0},
    {0xC9, (uint8_t[]){0x00, 0x00, 0x10, 0x1F, 0x36, 0x00, 0x5D, 0x04, 0x9D, 0x05, 0x10, 0xF2, 0x06,
                       0x60, 0x03, 0x11, 0xAD, 0x00, 0xEF, 0x01, 0x22, 0x2E, 0x0E, 0x74, 0x08, 0x32,
                       0xDC, 0x09, 0x33, 0x0F, 0xF3, 0x77, 0x0D, 0xB0, 0xDC, 0x03, 0xFF},
     37, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 100},
    {0x29, (uint8_t[]){0x00}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 100},
};
#define ST7123_INIT_CMD_COUNT (sizeof(k_st7123_init_cmds) / sizeof(k_st7123_init_cmds[0]))

static esp_err_t init_system_i2c(void)
{
    ESP_RETURN_ON_ERROR(hal_gpio_request((int)TAB5_SYS_I2C_SDA, HAL_GPIO_MODE_INPUT, "system_i2c"),
                         TAG, "SDA-Pin nicht freigegeben");
    ESP_RETURN_ON_ERROR(hal_gpio_request((int)TAB5_SYS_I2C_SCL, HAL_GPIO_MODE_INPUT, "system_i2c"),
                         TAG, "SCL-Pin nicht freigegeben");

    const i2c_master_bus_config_t i2c_config = {
        .i2c_port = TAB5_SYS_I2C_PORT,
        .sda_io_num = TAB5_SYS_I2C_SDA,
        .scl_io_num = TAB5_SYS_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&i2c_config, &s_sys_i2c_bus);
}

static tab5_display_type_t detect_display_type(void)
{
    if (i2c_master_probe(s_sys_i2c_bus, TAB5_TOUCH_GT911_BACKUP_ADDR, 50) == ESP_OK) {
        ESP_LOGI(TAG, "GT911-Touch erkannt -> Gen1-Panel (ILI9881C+GT911)");
        return TAB5_DISPLAY_GEN1_ILI9881C_GT911;
    }

    if (i2c_master_probe(s_sys_i2c_bus, ESP_LCD_TOUCH_IO_I2C_ST7123_ADDRESS, 50) != ESP_OK) {
        ESP_LOGE(TAG, "Kein bekannter Touch-Controller auf dem System-I2C-Bus gefunden");
        return TAB5_DISPLAY_UNKNOWN;
    }

    esp_lcd_panel_io_handle_t probe_io = NULL;
    esp_lcd_panel_io_i2c_config_t probe_cfg = ESP_LCD_TOUCH_IO_I2C_ST7123_CONFIG();
    probe_cfg.scl_speed_hz = 100000;
    if (esp_lcd_new_panel_io_i2c(s_sys_i2c_bus, &probe_cfg, &probe_io) != ESP_OK) {
        ESP_LOGE(TAG, "Panel-Typ-Sondierung: panel_io_i2c fehlgeschlagen");
        return TAB5_DISPLAY_UNKNOWN;
    }

    uint8_t fw_version = 0;
    esp_err_t err = esp_lcd_panel_io_rx_param(probe_io, 0x0000, &fw_version, 1);
    esp_lcd_panel_io_del(probe_io);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Panel-Typ-Sondierung: FW-Versions-Register nicht lesbar");
        return TAB5_DISPLAY_UNKNOWN;
    }

    if (fw_version == 1) {
        ESP_LOGI(TAG, "ST7121-Touch-Controller erkannt (FW-Version %u)", fw_version);
        return TAB5_DISPLAY_ST7121;
    }
    if (fw_version == 3) {
        ESP_LOGI(TAG, "ST7123-Touch-Controller erkannt (FW-Version %u)", fw_version);
        return TAB5_DISPLAY_ST7123;
    }
    ESP_LOGE(TAG, "Unbekannte Touch-Controller-FW-Version: %u", fw_version);
    return TAB5_DISPLAY_UNKNOWN;
}

static esp_err_t init_display_st712x(bool is_st7121)
{
    ESP_LOGI(TAG, "MIPI-DSI-PHY-Spannungsversorgung einschalten");
    const esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = TAB5_MIPI_DSI_PHY_LDO_CHAN,
        .voltage_mv = TAB5_MIPI_DSI_PHY_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &s_ldo_mipi_phy), TAG, "LDO-Kanal fehlgeschlagen");

    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = TAB5_DSI_LANE_BITRATE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &s_mipi_dsi_bus), TAG, "DSI-Bus fehlgeschlagen");

    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_mipi_dsi_bus, &dbi_config, &s_mipi_dbi_io),
                         TAG, "Panel-IO fehlgeschlagen");

    // Timing 1:1 aus M5Tab5-UserDemo (bsp_display_new_with_handles_to_st7123).
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = TAB5_DPI_CLOCK_MHZ,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = TAB5_PANEL_H_RES,
            .v_size = TAB5_PANEL_V_RES,
            .hsync_pulse_width = 2,
            .hsync_back_porch = 40,
            .hsync_front_porch = 40,
            .vsync_pulse_width = is_st7121 ? 20 : 2,
            .vsync_back_porch = is_st7121 ? 24 : 8,
            .vsync_front_porch = is_st7121 ? 200 : 220,
        },
        .flags = { .use_dma2d = true },
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 24,
        .vendor_config = NULL, // unten je Variante gesetzt
    };

    if (is_st7121) {
        st7121_vendor_config_t vendor_config = {
            .mipi_config = { .dsi_bus = s_mipi_dsi_bus, .dpi_config = &dpi_config },
        };
        esp_lcd_panel_dev_config_t cfg = panel_config;
        cfg.vendor_config = &vendor_config;
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7121(s_mipi_dbi_io, &cfg, &s_panel_handle),
                             TAG, "ST7121-Treiber fehlgeschlagen");
    } else {
        st7123_vendor_config_t vendor_config = {
            .init_cmds = k_st7123_init_cmds,
            .init_cmds_size = ST7123_INIT_CMD_COUNT,
            .mipi_config = { .dsi_bus = s_mipi_dsi_bus, .dpi_config = &dpi_config },
        };
        esp_lcd_panel_dev_config_t cfg = panel_config;
        cfg.vendor_config = &vendor_config;
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7123(s_mipi_dbi_io, &cfg, &s_panel_handle),
                             TAG, "ST7123-Treiber fehlgeschlagen");
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel_handle), TAG, "Panel-Reset fehlgeschlagen");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel_handle), TAG, "Panel-Init fehlgeschlagen");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel_handle, true), TAG, "Panel disp_on fehlgeschlagen");

    return ESP_OK;
}

static esp_err_t init_touch(void)
{
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_ST7123_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_sys_i2c_bus, &tp_io_config, &s_touch_io),
                         TAG, "Touch-Panel-IO fehlgeschlagen");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = TAB5_PANEL_H_RES,
        .y_max = TAB5_PANEL_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    return esp_lcd_touch_new_i2c_st7123(s_touch_io, &tp_cfg, &s_touch_handle);
}

static esp_err_t init_backlight(void)
{
    // Kein hal_gpio_request() hier: dieser Pin ist PIN_ROLE_INTERNAL, damit
    // GPIO/PWM-Fachmodule ihn nicht versehentlich belegen koennen - board_init
    // ist aber der legitime Systembesitzer und konfiguriert ihn direkt,
    // genau wie bei den Audio-/Kamera-/SD-Pins.
    const ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = TAB5_BACKLIGHT_LEDC_RES,
        .timer_num = TAB5_BACKLIGHT_LEDC_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "LEDC-Timer fehlgeschlagen");

    const ledc_channel_config_t channel_cfg = {
        .gpio_num = TAB5_LCD_BACKLIGHT_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = TAB5_BACKLIGHT_LEDC_CH,
        .timer_sel = TAB5_BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&channel_cfg);
}

static esp_err_t init_lvgl(void)
{
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 8192,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL-Port-Init fehlgeschlagen");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_mipi_dbi_io,
        .panel_handle = s_panel_handle,
        .buffer_size = TAB5_PANEL_H_RES * TAB5_PANEL_V_RES,
        .double_buffer = true,
        .hres = TAB5_PANEL_H_RES,
        .vres = TAB5_PANEL_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = {
            .buff_dma = true,
            .buff_spiram = true, // Puffer (~1.8MB pro Buffer bei 720x1280 RGB565) passt nicht in internes RAM
            .swap_bytes = false,
            .sw_rotate = false,
            // 90/270-Grad-Software-Rotation ueber esp_lvgl_port auf echter Hardware
            // ausprobiert und wieder verworfen: fuehrte je nach Konfiguration entweder
            // zu einem nur teilweise gefuellten/falsch orientierten Bild (PARTIAL-Modus)
            // oder zu einem Haenger (full_refresh+avoid_tearing, on_refresh_done-Callback
            // feuert nicht wie erwartet). Das Panel wird deshalb bewusst in seiner
            // physischen Ausrichtung (Portrait, 720x1280) betrieben - siehe
            // TAB5_PANEL_H_RES/V_RES oben. Die UI-Screens (dashboard/nav/statusbar)
            // nutzen LV_PCT()-basiertes Layout und passen sich automatisch an; ein
            // Wechsel auf Landscape-Darstellung ist ein spaeterer, separat zu
            // untersuchender Schritt (vermutlich PPA-basierte Rotation noetig).
        },
    };
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = { .avoid_tearing = false },
    };
    s_lvgl_disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (s_lvgl_disp == NULL) {
        return ESP_FAIL;
    }

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = s_lvgl_disp,
        .handle = s_touch_handle,
    };
    s_lvgl_touch = lvgl_port_add_touch(&touch_cfg);
    if (s_lvgl_touch == NULL) {
        ESP_LOGW(TAG, "Touch konnte nicht an LVGL angebunden werden - UI laeuft ohne Touch weiter");
    }

    return ESP_OK;
}

esp_err_t board_init(void)
{
    ESP_RETURN_ON_ERROR(init_system_i2c(), TAG, "System-I2C-Init fehlgeschlagen");
    ESP_RETURN_ON_ERROR(io_expander_init(s_sys_i2c_bus), TAG, "IO-Expander-Init fehlgeschlagen");
    // Panel/Touch brauchen nach Reset-Freigabe etwas Zeit, bis das I2C-
    // Interface antwortet (nicht in der Referenz explizit dokumentiert, aber
    // ueblich fuer Display-Controller nach Hardware-Reset).
    vTaskDelay(pdMS_TO_TICKS(300));

    s_display_type = detect_display_type();
    if (s_display_type == TAB5_DISPLAY_UNKNOWN) {
        ESP_LOGE(TAG, "Panel-Typ nicht erkannt - Display-Init abgebrochen");
        return ESP_ERR_NOT_FOUND;
    }
    if (s_display_type == TAB5_DISPLAY_GEN1_ILI9881C_GT911) {
        ESP_LOGE(TAG, "Gen1-Panel (ILI9881C+GT911) erkannt - Treiberpfad noch nicht "
                       "implementiert (Phase 1 deckt nur ST7121/ST7123 ab)");
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_RETURN_ON_ERROR(init_display_st712x(s_display_type == TAB5_DISPLAY_ST7121),
                         TAG, "Display-Init fehlgeschlagen");
    ESP_RETURN_ON_ERROR(init_touch(), TAG, "Touch-Init fehlgeschlagen");
    ESP_RETURN_ON_ERROR(init_backlight(), TAG, "Backlight-Init fehlgeschlagen");
    ESP_RETURN_ON_ERROR(init_lvgl(), TAG, "LVGL-Init fehlgeschlagen");

    ESP_ERROR_CHECK(board_set_backlight(80));

    ESP_LOGI(TAG, "Board-Init abgeschlossen: Panel=%s, Touch %s",
             s_display_type == TAB5_DISPLAY_ST7121 ? "ST7121" : "ST7123",
             s_lvgl_touch ? "aktiv" : "inaktiv");
    return ESP_OK;
}

lv_display_t *board_get_display(void)
{
    return s_lvgl_disp;
}

i2c_master_bus_handle_t board_get_system_i2c_bus(void)
{
    return s_sys_i2c_bus;
}

esp_err_t board_set_backlight(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    uint32_t max_duty = (1 << TAB5_BACKLIGHT_LEDC_RES) - 1;
    uint32_t duty = (max_duty * percent) / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, TAB5_BACKLIGHT_LEDC_CH, duty), TAG, "set_duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, TAB5_BACKLIGHT_LEDC_CH);
}
