/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "waveshare_rgb_lcd_port.h"
#include "wifi_config.h"
#include "display_config.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_sntp.h"

#ifdef HAVE_EEZ_UI
#include "ui.h"   /* generado por EEZ Studio en main/ui */
#include "screens.h"

/* ui_tick() sincroniza variables/flows de EEZ. Corre como timer de LVGL,
 * así se ejecuta dentro de la tarea de LVGL con el lock ya tomado. */
static void eez_ui_tick_cb(lv_timer_t *t)
{
    (void)t;
    ui_tick();
}
#endif

static esp_lcd_panel_handle_t g_panel;


#define INACTIVITY_TIMEOUT_MS DISPLAY_DEFAULT_INACTIVITY_TIMEOUT_MS

#define UART_PORT     UART_NUM_2
#define UART_BUF_SIZE 512
/* UART2 header pins on ESP32-S3-Touch-LCD-7 (adjust if your wiring differs) */
#define UART_RX_GPIO  44
#define UART_TX_GPIO  43
static uint32_t last_activity = 0;
static lv_obj_t *cont_scroll = NULL;
#define LOG_MAX_LINES 100

static void reset_inactivity_timer(void)
{
    last_activity = lv_tick_get();
    waveshare_rgb_lcd_backlight_on();
}

static void clock_cb(lv_timer_t *t)
{
    lv_obj_t *lbl = (lv_obj_t *)lv_timer_get_user_data(t);
    time_t now = time(NULL);
    struct tm *ti = localtime(&now);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
    lv_label_set_text(lbl, buf);
}

static void touch_activity_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED ||
        code == LV_EVENT_PRESSING ||
        code == LV_EVENT_RELEASED)
    {

        ESP_LOGI("TOUCH", "User activity");

        reset_inactivity_timer();
    }
}

static void inactivity_timer_cb(lv_timer_t *timer)
{
    uint32_t timeout_ms = display_config_get_inactivity_timeout_ms();
    if (timeout_ms == 0) timeout_ms = INACTIVITY_TIMEOUT_MS;
    if (last_activity && lv_tick_elaps(last_activity) >= timeout_ms) {
        ESP_LOGI("TOUCH", "Go to sleep due to inactivity");
        waveshare_rgb_lcd_backlight_off();
        last_activity=0;
    }
}

static void register_touch_activity(void)
{
    lv_indev_t *indev = lv_indev_get_next(NULL);

    while (indev != NULL)
    {

        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER)
        {

            lv_indev_add_event_cb(
                indev,
                touch_activity_cb,
                LV_EVENT_ALL,
                NULL);

            ESP_LOGI("TOUCH", "Touch activity callback registered");
            return;
        }

        indev = lv_indev_get_next(indev);
    }

    ESP_LOGW("TOUCH", "No pointer input device found");
}

static void log_line_to_display(const char *text)
{
    if (cont_scroll == NULL) {
        ESP_LOGW("LOGVIEW", "cont_scroll not ready: %s", text);
        return;
    }

    lv_obj_t *band = lv_obj_create(cont_scroll);
    lv_obj_set_width(band, LV_PCT(100));
    lv_obj_set_height(band, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(band, 4, 0);
    lv_obj_set_style_pad_all(band, 4, 0);

    uint32_t r = (esp_random() % 156) + 50;
    uint32_t g = (esp_random() % 156) + 50;
    uint32_t bcol = (esp_random() % 156) + 50;
    lv_obj_set_style_bg_color(band, lv_color_make(r, g, bcol), 0);

    lv_obj_t *lbl = lv_label_create(band);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_label_set_text(lbl, text);

    /* Límite de líneas: sin esto la memoria crece sin fin */
    while (lv_obj_get_child_count(cont_scroll) > LOG_MAX_LINES) {
        lv_obj_delete(lv_obj_get_child(cont_scroll, 0));
    }

    lv_obj_scroll_to_view(band, LV_ANIM_ON);
}

void uart_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_GPIO, UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void uart_poll_timer_cb(lv_timer_t *timer)
{
    static uint8_t data[UART_BUF_SIZE];
    static char line[UART_BUF_SIZE];
    static size_t line_len = 0;
    static bool inited = false;
    (void)timer;

    if (!inited) {
        uart_init();
        inited = true;

        ESP_LOGI("UART", "UART initialized on port %d, RX=%d, TX=%d", (int)UART_PORT, UART_RX_GPIO, UART_TX_GPIO);
        log_line_to_display("UART log viewer started ");
    }

    int len = uart_read_bytes(UART_PORT, data, UART_BUF_SIZE - 1, 0);
    if (len <= 0) {
        return;
    }

    ESP_LOGI("UART", "Read %d bytes from UART", len);
    for (int i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\r' || c == '\n') {
            if (line_len > 0) {
                line[line_len] = '\0';
                log_line_to_display(line);
                line_len = 0;
            }
        } else if (line_len < (UART_BUF_SIZE - 1)) {
            line[line_len++] = c;
        }
    }
}

static void enviar_uart_cb(lv_event_t *e)
{
    (void)e;
    static const char msg[] = "TEST\r\n";
    int n = uart_write_bytes(UART_PORT, msg, sizeof(msg) - 1);
    ESP_LOGI("UART", "Enviado TEST (%d bytes)", n);
    log_line_to_display(n > 0 ? ">> TEST enviado" : ">> Error enviando TEST");
}


void app_main(void)
{
    const esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
    const esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB;

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;

    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(tear_mode, rotation, &panel_handle, &touch_handle));
    g_panel = panel_handle;
    ESP_ERROR_CHECK(waveshare_rgb_lcd_backlight_on());

    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
//    adapter_config.task_stack_size = 12 * 1024;
    adapter_config.stack_in_psram = true;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_config));

    esp_lv_adapter_display_config_t disp_config = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel_handle, NULL, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, rotation);
    disp_config.profile.use_psram = true;
    lv_display_t *disp = esp_lv_adapter_register_display(&disp_config);
    assert(disp != NULL);

    if (touch_handle != NULL)
    {
        esp_lv_adapter_touch_config_t touch_config = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch_handle);
        lv_indev_t *touch = esp_lv_adapter_register_touch(&touch_config);
        assert(touch != NULL);

        /* Register global touch monitoring */
        register_touch_activity();
    }

    ESP_ERROR_CHECK(esp_lv_adapter_start());

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    display_config_load();
    wifi_init();

#ifdef HAVE_EEZ_UI
    /* Interfaz de EEZ Studio */
    if (esp_lv_adapter_lock(-1) == ESP_OK)
    {
        ui_init();
        lv_timer_create(eez_ui_tick_cb, 33, NULL);

        /* Logs por UART en el contenedor "lista_logs" diseñado en EEZ */
        cont_scroll = objects.lista_logs;
        lv_obj_set_flex_flow(cont_scroll, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scrollbar_mode(cont_scroll, LV_SCROLLBAR_MODE_AUTO);
        lv_timer_create(uart_poll_timer_cb, 50, NULL);

        /* Botón "enviar_uart" de EEZ: manda TEST por la UART */
        lv_obj_add_event_cb(objects.enviar_uart, enviar_uart_cb, LV_EVENT_CLICKED, NULL);

        last_activity = lv_tick_get();
        lv_timer_create(inactivity_timer_cb, 1000, NULL);

        esp_lv_adapter_unlock();
    }
#else
    /* Interfaz original (pestañas Dashboard / Config / Logs) */
    if (esp_lv_adapter_lock(-1) == ESP_OK)
    {
        lv_obj_t *tv = lv_tabview_create(lv_scr_act());
        lv_obj_set_size(tv, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);

        lv_obj_t *t_dash = lv_tabview_add_tab(tv, "Dashboard");
        lv_obj_t *t_cfg = lv_tabview_add_tab(tv, "Config");
        wifi_config_create(t_cfg);
        lv_obj_t *t_logs = lv_tabview_add_tab(tv, "Logs");

        lv_tabview_set_act(tv, 2, LV_ANIM_OFF);

        lv_obj_t *lbl = lv_label_create(t_dash);
        lv_label_set_text(lbl, "pepito");
        lv_obj_center(lbl);

        lbl = lv_label_create(t_logs);
        lv_label_set_text(lbl, "Logs");
        lv_obj_center(lbl);

        cont_scroll = lv_obj_create(t_logs);
        lv_obj_set_size(cont_scroll, LV_PCT(100), LV_PCT(90));
        lv_obj_align(cont_scroll, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_flex_flow(cont_scroll, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scrollbar_mode(cont_scroll, LV_SCROLLBAR_MODE_AUTO);

        last_activity = lv_tick_get();
        lv_timer_create(inactivity_timer_cb, 1000, NULL);

        lv_obj_t *clock = lv_label_create(lv_scr_act());
        lv_obj_align(clock, LV_ALIGN_TOP_RIGHT, -10, 10);
        lv_obj_set_style_text_font(clock, &lv_font_montserrat_20, 0);

        lv_obj_t *wifi_icon = lv_label_create(lv_scr_act());
        lv_obj_align(wifi_icon, LV_ALIGN_TOP_LEFT, 10, 10);
        lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_20, 0);
        wifi_config_set_icon(wifi_icon);

        lv_timer_create(clock_cb, 1000, clock);
        lv_timer_create(uart_poll_timer_cb, 50, NULL);

        esp_lv_adapter_unlock();
    }
#endif
}
