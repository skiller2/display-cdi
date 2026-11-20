/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <time.h>
#include <string.h>

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
#include "esp_crc.h"

#ifdef HAVE_EEZ_UI
#include "ui.h"   /* generado por EEZ Studio en main/ui */
#include "sys_info.h"
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

/* ---------------------------------------------------------------------------
 * Recepción de tramas del otro micro:
 *   [magic u16 = 0x55AA][len u16][crc16 u16][payload JSON (len bytes)]
 * Todo little-endian (en el cable: AA 55 LL LL CC CC ...).
 * CRC = esp_crc16_le(UINT16_MAX, payload, len), igual que el emisor.
 * ------------------------------------------------------------------------- */
#define FRAME_MAGIC        0x55AA
#define FRAME_MAX_PAYLOAD  2048   /* el emisor usa char json_data[2048] */
#define FRAME_RX_TIMEOUT_MS 300   /* trama a medias sin bytes nuevos -> descartar */

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint16_t len;
    uint16_t crc16;
} serial_frame_hdr_t;

typedef enum {
    RX_WAIT_MAGIC_LO,   /* esperando 0xAA */
    RX_WAIT_MAGIC_HI,   /* esperando 0x55 */
    RX_HDR,             /* len + crc */
    RX_PAYLOAD,         /* acumulando JSON */
} rx_state_t;

typedef struct {
    rx_state_t         state;
    uint8_t            hdr_buf[sizeof(serial_frame_hdr_t)];
    size_t             hdr_idx;
    serial_frame_hdr_t hdr;
    char               payload[FRAME_MAX_PAYLOAD + 1]; /* +1 para '\0' */
    size_t             pay_idx;
    uint32_t           last_byte_tick;
    uint32_t           frames_ok, crc_errors, len_errors, timeouts;
} frame_rx_t;

/* static: 2 KB fuera del stack de la tarea de LVGL */
static frame_rx_t s_rx;

/* Se llama con una trama válida. Corre dentro de un timer de LVGL,
 * así que se puede tocar la UI directamente. */
static void on_frame_payload(const char *json, size_t len)
{
    ESP_LOGI("RX", "JSON(%u): %.*s", (unsigned)len, (int)len, json);
    log_line_to_display(json);
    // TODO: parsear el JSON (cJSON) y actualizar los objetos de la UI
}

static void frame_rx_feed(frame_rx_t *d, uint8_t b)
{
    switch (d->state) {
    case RX_WAIT_MAGIC_LO:
        if (b == (FRAME_MAGIC & 0xFF)) {
            d->hdr_buf[0] = b;
            d->hdr_idx = 1;
            d->state = RX_WAIT_MAGIC_HI;
        }
        break;

    case RX_WAIT_MAGIC_HI:
        if (b == (FRAME_MAGIC >> 8)) {
            d->hdr_buf[1] = b;
            d->hdr_idx = 2;
            d->state = RX_HDR;
        } else if (b != (FRAME_MAGIC & 0xFF)) {
            d->state = RX_WAIT_MAGIC_LO;   /* si es otro 0xAA, seguimos esperando 0x55 */
        }
        break;

    case RX_HDR:
        d->hdr_buf[d->hdr_idx++] = b;
        if (d->hdr_idx == sizeof(serial_frame_hdr_t)) {
            memcpy(&d->hdr, d->hdr_buf, sizeof(d->hdr));
            if (d->hdr.len == 0 || d->hdr.len > FRAME_MAX_PAYLOAD) {
                d->len_errors++;
                ESP_LOGW("RX", "len inválido: %u", d->hdr.len);
                d->state = RX_WAIT_MAGIC_LO;
                break;
            }
            d->pay_idx = 0;
            d->state = RX_PAYLOAD;
        }
        break;

    case RX_PAYLOAD:
        d->payload[d->pay_idx++] = (char)b;
        if (d->pay_idx >= d->hdr.len) {
            d->payload[d->pay_idx] = '\0';
            uint16_t crc = esp_crc16_le(UINT16_MAX, (const uint8_t *)d->payload, d->hdr.len);
            if (crc == d->hdr.crc16) {
                d->frames_ok++;
                on_frame_payload(d->payload, d->hdr.len);
            } else {
                d->crc_errors++;
                ESP_LOGW("RX", "CRC malo: calc=%04X hdr=%04X", crc, d->hdr.crc16);
            }
            d->state = RX_WAIT_MAGIC_LO;
        }
        break;
    }
}

/* Timer de LVGL (cada 50 ms): lee lo que haya SIN bloquear y alimenta el decodificador. */
static void uart_poll_timer_cb(lv_timer_t *timer)
{
    static uint8_t data[UART_BUF_SIZE];
    static bool inited = false;
    (void)timer;

    if (!inited) {
        uart_init();
        memset(&s_rx, 0, sizeof(s_rx));
        s_rx.state = RX_WAIT_MAGIC_LO;
        inited = true;
        ESP_LOGI("UART", "UART initialized on port %d, RX=%d, TX=%d", (int)UART_PORT, UART_RX_GPIO, UART_TX_GPIO);
        log_line_to_display("UART: esperando tramas");
    }

    /* Trama cortada (se perdieron bytes): volver a buscar el inicio */
    if (s_rx.state != RX_WAIT_MAGIC_LO &&
        lv_tick_elaps(s_rx.last_byte_tick) > FRAME_RX_TIMEOUT_MS) {
        s_rx.timeouts++;
        ESP_LOGW("RX", "timeout en trama (estado %d, %u/%u bytes)",
                 s_rx.state, (unsigned)s_rx.pay_idx, s_rx.hdr.len);
        s_rx.state = RX_WAIT_MAGIC_LO;
    }

    int len;
    /* timeout 0: nunca bloquea la tarea de LVGL */
    while ((len = uart_read_bytes(UART_PORT, data, sizeof(data), 0)) > 0) {
        s_rx.last_byte_tick = lv_tick_get();
        for (int i = 0; i < len; i++) {
            frame_rx_feed(&s_rx, data[i]);
        }
    }
}

static void enviar_ack_cb(lv_event_t *e)
{
    (void)e;
    /* Trama: [hdr: magic 0x55AA | len | crc16][JSON] — mismo formato que recibe frame_rx_feed */
    static const char msg[] = "{\"cmdACK\":{}}";

    serial_frame_hdr_t hdr;
    hdr.magic = FRAME_MAGIC;
    hdr.len   = (uint16_t)(sizeof(msg) - 1);   /* sin el '\0' */
    hdr.crc16 = esp_crc16_le(UINT16_MAX, (const uint8_t *)msg, hdr.len);

    int n = uart_write_bytes(UART_PORT, (const char *)&hdr, sizeof(hdr));
    n += uart_write_bytes(UART_PORT, msg, hdr.len);

    ESP_LOGI("UART", "Enviado %s (%d bytes, crc=%04X)", msg, n, hdr.crc16);
    log_line_to_display(n == (int)(sizeof(hdr) + hdr.len) ? ">> cmdACK enviado" : ">> Error enviando cmdACK");
}

static void enviar_reset_cb(lv_event_t *e)
{
    (void)e;
    /* Trama: [hdr: magic 0x55AA | len | crc16][JSON] — mismo formato que recibe frame_rx_feed */
    static const char msg[] = "{\"cmdReset\":{}}";

    serial_frame_hdr_t hdr;
    hdr.magic = FRAME_MAGIC;
    hdr.len   = (uint16_t)(sizeof(msg) - 1);   /* sin el '\0' */
    hdr.crc16 = esp_crc16_le(UINT16_MAX, (const uint8_t *)msg, hdr.len);

    int n = uart_write_bytes(UART_PORT, (const char *)&hdr, sizeof(hdr));
    n += uart_write_bytes(UART_PORT, msg, hdr.len);

    ESP_LOGI("UART", "Enviado %s (%d bytes, crc=%04X)", msg, n, hdr.crc16);
    log_line_to_display(n == (int)(sizeof(hdr) + hdr.len) ? ">> cmdACK enviado" : ">> Error enviando cmdACK");
}

static void enviar_test_cb(lv_event_t *e)
{
    (void)e;
    /* Trama: [hdr: magic 0x55AA | len | crc16][JSON] — mismo formato que recibe frame_rx_feed */
    static const char msg[] = "{\"cmdTest\":{}}";

    serial_frame_hdr_t hdr;
    hdr.magic = FRAME_MAGIC;
    hdr.len   = (uint16_t)(sizeof(msg) - 1);   /* sin el '\0' */
    hdr.crc16 = esp_crc16_le(UINT16_MAX, (const uint8_t *)msg, hdr.len);

    int n = uart_write_bytes(UART_PORT, (const char *)&hdr, sizeof(hdr));
    n += uart_write_bytes(UART_PORT, msg, hdr.len);

    ESP_LOGI("UART", "Enviado %s (%d bytes, crc=%04X)", msg, n, hdr.crc16);
    log_line_to_display(n == (int)(sizeof(hdr) + hdr.len) ? ">> cmdACK enviado" : ">> Error enviando cmdACK");
}


void app_main(void)
{
    const esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
    const esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB;

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;

    /* Wi-Fi primero: necesita RAM interna contigua que el LCD/LVGL consumen después */
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init();

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

    display_config_load();

#ifdef HAVE_EEZ_UI
    /* Interfaz de EEZ Studio */
    if (esp_lv_adapter_lock(-1) == ESP_OK)
    {
        ui_init();
        lv_timer_create(eez_ui_tick_cb, 33, NULL);
        sys_info_start();
        wifi_config_bind_eez();

        /* Logs por UART en el contenedor "lista_logs" diseñado en EEZ */
        cont_scroll = objects.lista_logs;
        lv_obj_set_flex_flow(cont_scroll, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scrollbar_mode(cont_scroll, LV_SCROLLBAR_MODE_AUTO);
        lv_timer_create(uart_poll_timer_cb, 50, NULL);

        /* Botón "enviar_uart" de EEZ: manda TEST por la UART */
        lv_obj_add_event_cb(objects.enviar_ack, enviar_ack_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(objects.enviar_reset, enviar_reset_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(objects.enviar_test, enviar_test_cb, LV_EVENT_CLICKED, NULL);

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
