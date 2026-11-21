/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <time.h>
#include <stdlib.h>
#include <sys/time.h>
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

#include "ui.h"   /* generado por EEZ Studio en main/ui */
#include "sys_info.h"
#include "panel_model.h"
#include "panel_ui.h"
#include "panel_keypad.h"
#include "screens.h"

/* ---- Configuración ---- */
#define INACTIVITY_TIMEOUT_MS DISPLAY_DEFAULT_INACTIVITY_TIMEOUT_MS
#define LOG_MAX_LINES         100        /* líneas máx. en la lista de logs */
#define NAV_DEPTH             8          /* pantallas recordadas para "Volver" */

/* UART con la central */
#define UART_PORT     UART_NUM_2
#define UART_BUF_SIZE 512
/* UART2 header pins on ESP32-S3-Touch-LCD-7 (adjust if your wiring differs) */
#define UART_RX_GPIO  44
#define UART_TX_GPIO  43

/* Trama: [magic u16][len u16][crc16 u16][payload JSON] */
#define FRAME_MAGIC         0x55AA
#define FRAME_MAX_PAYLOAD   2048   /* el emisor usa char json_data[2048] */
#define FRAME_RX_TIMEOUT_MS 300    /* trama a medias sin bytes nuevos -> descartar */

/* Falla de hora: se compara TIMESTAMP de la central con la hora del display */
#define TIME_DIFF_MAX_S        5            /* diferencia máx. tolerada (s) */
#define TIME_VALID_MIN_EPOCH   1704067200LL /* 2024-01-01: antes de esto se asume hora no seteada */
#define TIME_NVS_NAMESPACE     "disp_time"  /* NVS: última hora conocida (sobrevive a reinicios) */
#define TIME_NVS_KEY           "last_ts"
#define TIME_SAVE_PERIOD_S     3600         /* cada cuánto se guarda la hora en NVS */
#define TIME_RESYNC_PERIOD_S   86400        /* cada cuánto se vuelve a poner en hora con la central (24 h) */

/* Zona horaria (formato POSIX TZ): -03:00 sin horario de verano */
#define TIMEZONE_POSIX "<-03>3"

/* Keepalive: cada KEEPALIVE_MS se pregunta la hora a la central */
#define KEEPALIVE_MS  1000
#define KEEPALIVE_CMD "{\"cmdGetData\":\"SSI_TIEMPO\"}"

/* ui_tick() sincroniza variables/flows de EEZ. Corre como timer de LVGL,
 * así se ejecuta dentro de la tarea de LVGL con el lock ya tomado. */
static void eez_ui_tick_cb(lv_timer_t *t)
{
    (void)t;
    ui_tick();
}

static esp_lcd_panel_handle_t g_panel;

static uint32_t last_activity = 0;
static lv_obj_t *cont_scroll = NULL;

static void reset_inactivity_timer(void)
{
    last_activity = lv_tick_get();
    waveshare_rgb_lcd_backlight_on();
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
 * Recepción de tramas de la otra central:
 *   [magic u16 = 0x55AA][len u16][crc16 u16][payload JSON (len bytes)]
 * Todo little-endian (en el cable: AA 55 LL LL CC CC ...).
 * CRC = esp_crc16_le(UINT16_MAX, payload, len), igual que el emisor.
 * ------------------------------------------------------------------------- */

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

/* ---- Hora persistente (NVS) ---------------------------------------------
 * Sin RTC, el reloj arranca en 1970. Se guarda la última hora conocida en NVS
 * y se restaura al arrancar (aproximada: no cuenta el tiempo apagado). La hora
 * se verifica con la central: la primera respuesta válida tras el arranque
 * pone el reloj en hora; de ahí en más solo se vigila la diferencia. */
static bool    s_time_synced;      /* ya se puso en hora con la central desde el arranque */
static int64_t s_time_saved_s;     /* esp_timer (s) del último guardado en NVS */
static int64_t s_time_synced_s;    /* esp_timer (s) de la última puesta en hora con la central */

static void time_save_to_nvs(void)
{
    time_t now = time(NULL);
    if (now < TIME_VALID_MIN_EPOCH) return;
    nvs_handle_t h;
    if (nvs_open(TIME_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_i64(h, TIME_NVS_KEY, (int64_t)now) == ESP_OK) nvs_commit(h);
    nvs_close(h);
    s_time_saved_s = esp_timer_get_time() / 1000000;
}

/* Llamar una vez al arrancar, después de nvs_flash_init() y tzset(). */
static void time_restore_from_nvs(void)
{
    nvs_handle_t h;
    int64_t ts = 0;
    if (nvs_open(TIME_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    esp_err_t err = nvs_get_i64(h, TIME_NVS_KEY, &ts);
    nvs_close(h);
    if (err != ESP_OK || ts < TIME_VALID_MIN_EPOCH) return;
    if (time(NULL) >= TIME_VALID_MIN_EPOCH) return;   /* ya hay hora válida */

    struct timeval tv = { .tv_sec = (time_t)ts, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    ESP_LOGI("TIME", "hora restaurada desde NVS (aproximada)");
}

/* Controla "TIMESTAMP" (epoch, segundos) de la central contra la hora del display.
 * - Primera respuesta tras el arranque, y luego cada TIME_RESYNC_PERIOD_S: pone el
 *   reloj en hora con la central (se limpia la falla) y la guarda en NVS.
 * - Entre resincronizaciones: si difieren más de TIME_DIFF_MAX_S marca la falla
 *   de hora (barra en Main) sin tocar el reloj. Con hora correcta, la guarda en
 *   NVS cada TIME_SAVE_PERIOD_S. */
static void check_time_diff(const char *json)
{
    const char *p = strstr(json, "\"TIMESTAMP\"");
    if (!p || !(p = strchr(p, ':'))) return;
    long long ts = strtoll(p + 1, NULL, 10);

    if (ts < TIME_VALID_MIN_EPOCH) {            /* la central no tiene hora válida */
        panel_model_set_time_diff(0, false, false);
        return;
    }

    int64_t up_s = esp_timer_get_time() / 1000000;
    if (!s_time_synced || up_s - s_time_synced_s >= TIME_RESYNC_PERIOD_S) {
        long long adj = ts - (long long)time(NULL);
        struct timeval tv = { .tv_sec = (time_t)ts, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        if (s_time_synced) ESP_LOGI("TIME", "hora resincronizada con la central (ajuste %+lld s)", adj);
        else               ESP_LOGI("TIME", "hora puesta con la central");
        s_time_synced   = true;
        s_time_synced_s = up_s;
        time_save_to_nvs();
        panel_model_set_time_diff(0, true, false);
        return;
    }

    long long diff = ts - (long long)time(NULL);
    bool fault = diff > TIME_DIFF_MAX_S || diff < -TIME_DIFF_MAX_S;
    if (fault != panel_model_get()->time_fault) {
        /* El formato de ESP_LOGx debe ser literal: no se puede elegir con "?:" */
        if (fault) ESP_LOGW("TIME", "falla de hora: central %+lld s respecto del display", diff);
        else       ESP_LOGI("TIME", "hora de la central y del display coinciden (%+lld s)", diff);
    }
    panel_model_set_time_diff((int32_t)diff, true, fault);

    /* Solo se guarda una hora confiable (sin falla) */
    if (!fault && up_s - s_time_saved_s >= TIME_SAVE_PERIOD_S)
        time_save_to_nvs();
}

/* Se llama con una trama válida. Corre dentro de un timer de LVGL,
 * así que se puede tocar la UI directamente. */
static void on_frame_payload(const char *json, size_t len)
{
    /* Respuesta al keepalive ({"TIEMPO":...,"MAC":...,"TIMESTAMP":...}), llega
     * cada 1 s: renueva la conexión y controla la diferencia de hora; no se loguea. */
    if (strstr(json, "\"TIEMPO\"")) {
        panel_model_feed(json, len, lv_tick_get());
        check_time_diff(json);
        return;
    }
    ESP_LOGI("RX", "JSON(%u): %.*s", (unsigned)len, (int)len, json);
    log_line_to_display(json);
    int upd = panel_model_feed(json, len, lv_tick_get());
    char summary[256];
    panel_model_summary(upd, summary, sizeof(summary));
    ESP_LOGI("RX", "%s", summary);
    log_line_to_display(summary);
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
        panel_model_init();
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

/* Envía un JSON por UART con el formato de trama de frame_rx_feed:
 * [hdr: magic 0x55AA | len | crc16][JSON]. Devuelve true si salió completo.
 * Sin log en pantalla (la usa también el keepalive, cada 1 s). */
static bool uart_send_json(const char *msg)
{
    serial_frame_hdr_t hdr;
    hdr.magic = FRAME_MAGIC;
    hdr.len   = (uint16_t)strlen(msg);
    hdr.crc16 = esp_crc16_le(UINT16_MAX, (const uint8_t *)msg, hdr.len);

    int n = uart_write_bytes(UART_PORT, (const char *)&hdr, sizeof(hdr));
    n += uart_write_bytes(UART_PORT, msg, hdr.len);
    bool ok = n == (int)(sizeof(hdr) + hdr.len);

    if (ok) ESP_LOGD("UART", "Enviado %s", msg);
    else    ESP_LOGW("UART", "Error enviando %s", msg);
    return ok;
}

/* Keepalive con la central: cada 1 s pregunta la hora. La respuesta
 * ({"TIEMPO":...}) pasa por on_frame_payload y renueva last_rx_ms; si la
 * central deja de contestar, panel_ui muestra "Sin conexion con la central". */

static void keepalive_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    uart_send_json(KEEPALIVE_CMD);
}

/* ---- Acciones del encabezado (spec 001, etapas 7-8). Textos de dictionary.js ---- */
static void enviar_ack_cb(lv_event_t *e)
{
    (void)e;
    if (uart_send_json("{\"cmdACK\":{}}"))
        panel_ui_notice("Enviado", "Aceptacion enviado correctamente");
    else
        panel_ui_notice("Error", "Error enviando aceptacion");
}

static void do_reset(void *arg)
{
    (void)arg;
    if (uart_send_json("{\"cmdReset\":{}}")) {
        panel_ui_set_unknown();          /* gris hasta el próximo estado (como el widget) */
        panel_ui_notice("Enviado", "Reset enviado correctamente");
    }
    else
        panel_ui_notice("Error", "Error enviando comando");
}

static void do_test(void *arg)
{
    (void)arg;
    if (uart_send_json("{\"cmdTest\":{}}"))
        panel_ui_notice("Enviado", "Test enviado correctamente");
    else
        panel_ui_notice("Error", "Error enviando test");
}

static void go_screen(enum ScreensEnum id)
{
    eez_flow_set_screen(id, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0);
}

/* Historial de pantallas para "Volver". Se registra con SCREEN_LOADED, así
 * cuenta también los cambios hechos por flows de EEZ (botones de Main). */
static enum ScreensEnum nav_stack[NAV_DEPTH];
static int nav_n;
static enum ScreensEnum nav_cur = SCREEN_ID_MAIN;
static bool nav_going_back;

static void screen_loaded_cb(lv_event_t *e)
{
    enum ScreensEnum id = (enum ScreensEnum)(intptr_t)lv_event_get_user_data(e);
    if (id == nav_cur) return;
    if (nav_going_back) {
        nav_going_back = false;
    } else if (id == SCREEN_ID_MAIN) {
        nav_n = 0;                       /* Main es la raíz: historial vacío */
    } else {
        if (nav_n == NAV_DEPTH) {        /* lleno: descarta el más viejo */
            memmove(nav_stack, nav_stack + 1, sizeof(nav_stack[0]) * (NAV_DEPTH - 1));
            nav_n--;
        }
        nav_stack[nav_n++] = nav_cur;
    }
    nav_cur = id;
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    enum ScreensEnum to = nav_n > 0 ? nav_stack[--nav_n] : SCREEN_ID_MAIN;
    nav_going_back = true;
    go_screen(to);
}

static void nav_init(void)
{
    struct { lv_obj_t *scr; enum ScreensEnum id; } s[] = {
        { objects.main, SCREEN_ID_MAIN }, { objects.logs, SCREEN_ID_LOGS },
        { objects.sys_info, SCREEN_ID_SYS_INFO }, { objects.wifi, SCREEN_ID_WIFI },
        { objects.config, SCREEN_ID_CONFIG },
    };
    for (size_t i = 0; i < sizeof(s) / sizeof(s[0]); i++)
        if (s[i].scr)
            lv_obj_add_event_cb(s[i].scr, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, (void *)(intptr_t)s[i].id);

    lv_obj_t *backs[] = { objects.logs_back, objects.sys_back, objects.wifi_back, objects.cfg_back };
    for (size_t i = 0; i < sizeof(backs) / sizeof(backs[0]); i++)
        if (backs[i]) lv_obj_add_event_cb(backs[i], back_cb, LV_EVENT_CLICKED, NULL);
}

static void do_config(void *arg) { (void)arg; go_screen(SCREEN_ID_CONFIG); }
static void btn_reset_cb(lv_event_t *e)  { (void)e; panel_keypad_open(1, do_reset, NULL); }
static void btn_test_cb(lv_event_t *e)   { (void)e; panel_keypad_open(1, do_test, NULL); }
static void btn_config_cb(lv_event_t *e) { (void)e; panel_keypad_open(2, do_config, NULL); }

/* Pantalla Config: el destino va en user_data */
static void cfg_nav_cb(lv_event_t *e)
{
    go_screen((enum ScreensEnum)(intptr_t)lv_event_get_user_data(e));
}

void app_main(void)
{
    const esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
    const esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB;

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;

    /* Zona horaria: localtime() muestra hora local en vez de UTC */
    setenv("TZ", TIMEZONE_POSIX, 1);
    tzset();

    /* Wi-Fi primero: necesita RAM interna contigua que el LCD/LVGL consumen después */
    ESP_ERROR_CHECK(nvs_flash_init());
    time_restore_from_nvs();
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
        lv_timer_create(keepalive_timer_cb, KEEPALIVE_MS, NULL);

        /* Encabezado Main (spec 001, etapa 2). ACK directo; Reset/Test/Config
         * pasarán por el teclado PIN en la etapa 7. */
        lv_obj_add_event_cb(objects.btn_ack, enviar_ack_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(objects.btn_reset, btn_reset_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(objects.btn_test, btn_test_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(objects.btn_config, btn_config_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(objects.cfg_wifi, cfg_nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)SCREEN_ID_WIFI);
        lv_obj_add_event_cb(objects.cfg_sysinfo, cfg_nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)SCREEN_ID_SYS_INFO);
        lv_obj_add_event_cb(objects.cfg_logs, cfg_nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)SCREEN_ID_LOGS);
        nav_init();
        panel_ui_start();

        last_activity = lv_tick_get();
        lv_timer_create(inactivity_timer_cb, 1000, NULL);

        esp_lv_adapter_unlock();
    }
}
