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

static esp_lcd_panel_handle_t g_panel;


#define INACTIVITY_TIMEOUT_MS 10000

static uint32_t last_activity = 0;

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
    if (last_activity && lv_tick_elaps(last_activity) >= INACTIVITY_TIMEOUT_MS) {
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
    adapter_config.task_stack_size = 12 * 1024;
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

    wifi_init();

    if (esp_lv_adapter_lock(-1) == ESP_OK)
    {
        lv_obj_t *tv = lv_tabview_create(lv_scr_act());
        lv_obj_set_size(tv, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);

        lv_obj_t *t_dash = lv_tabview_add_tab(tv, "Dashboard");
        lv_obj_t *t_cfg = lv_tabview_add_tab(tv, "Config");
        wifi_config_create(t_cfg);
        lv_obj_t *t_logs = lv_tabview_add_tab(tv, "Logs");

        lv_obj_t *lbl = lv_label_create(t_dash);
        lv_label_set_text(lbl, "Dashboard");
        lv_obj_center(lbl);

        lbl = lv_label_create(t_logs);
        lv_label_set_text(lbl, "Logs");
        lv_obj_center(lbl);

        last_activity = lv_tick_get();
        lv_timer_create(inactivity_timer_cb, 1000, NULL);

        lv_obj_t *clock = lv_label_create(lv_scr_act());
        lv_obj_align(clock, LV_ALIGN_TOP_RIGHT, -10, 10);
        lv_obj_set_style_text_font(clock, &lv_font_montserrat_20, 0);

        lv_timer_create(clock_cb, 1000, clock);

        esp_lv_adapter_unlock();
    }
}
