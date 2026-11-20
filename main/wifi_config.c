#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_lv_adapter.h"
#include "wifi_config.h"
#include "display_config.h"

static lv_obj_t *status_modal;
static lv_obj_t *status_label;
static lv_obj_t *wifi_icon_label;

static lv_obj_t *sys_heap_label;
static lv_obj_t *sys_mem_label;
static lv_obj_t *sys_flash_label;
static lv_obj_t *sys_cpu_label;
static lv_obj_t *sys_chip_label;

/* Motivo de desconexión (wifi_err_reason_t) -> texto. Números fijos para no depender de la versión de IDF. */
static const char *wifi_reason_str(int r)
{
    switch (r) {
    case 1:   return "No especificado";
    case 2:   return "Autenticación expirada";
    case 3:   return "Autenticación finalizada";
    case 4:   return "Asociación expirada";
    case 5:   return "Demasiadas estaciones";
    case 6:   return "No autenticado";
    case 7:   return "No asociado";
    case 8:   return "Asociación finalizada";
    case 9:   return "Asociación sin autenticar";
    case 13:  return "IE inválido";
    case 14:  return "Fallo de MIC";
    case 15:  return "Clave incorrecta (timeout 4-way)";
    case 16:  return "Timeout de clave de grupo";
    case 17:  return "IE distinto en 4-way";
    case 18:  return "Cifrado de grupo inválido";
    case 19:  return "Cifrado pairwise inválido";
    case 20:  return "AKMP inválido";
    case 21:  return "Versión RSN no soportada";
    case 22:  return "Capacidades RSN inválidas";
    case 23:  return "Falló autenticación 802.1X";
    case 24:  return "Cifrado rechazado";
    case 25:  return "Par TDLS inalcanzable";
    case 26:  return "TDLS no especificado";
    case 27:  return "Desasociación pedida por SSP";
    case 28:  return "Sin acuerdo de roaming SSP";
    case 29:  return "Cifrado o AKM incorrecto";
    case 30:  return "No autorizado en esta ubicación";
    case 31:  return "Cambio de servicio impide TS";
    case 32:  return "QoS no especificado";
    case 33:  return "Ancho de banda insuficiente";
    case 34:  return "Faltan ACK";
    case 35:  return "TXOP excedido";
    case 36:  return "Estación saliendo (el par dejó el BSS)";
    case 37:  return "Fin de BA";
    case 38:  return "BA desconocido";
    case 39:  return "Timeout";
    case 200: return "Timeout de beacon";
    case 201: return "Red no encontrada";
    case 202: return "Falló autenticación (¿clave incorrecta?)";
    case 203: return "Falló asociación";
    case 204: return "Timeout de handshake (¿clave incorrecta?)";
    case 205: return "Falló la conexión";
    case 206: return "Reinicio de TSF del AP";
    case 207: return "Roaming";
    case 208: return "Espera de reasociación muy larga";
    case 209: return "Timeout de consulta SA";
    case 210: return "Sin AP con seguridad compatible";
    case 211: return "Modo de autenticación bajo el umbral";
    case 212: return "Señal bajo el umbral";
    default:  return "Desconocido";
    }
}

static lv_obj_t *connect_btn;      /* botón único Conectar/Desconectar (EEZ: wifi_connect) */
typedef enum { WIFI_ST_DISCONNECTED, WIFI_ST_CONNECTING, WIFI_ST_CONNECTED } wifi_state_t;
static volatile wifi_state_t wifi_state = WIFI_ST_DISCONNECTED;

/* Llamar con lock LVGL tomado */
static void update_connect_btn(void)
{
    if (!connect_btn) return;
    lv_obj_t *lbl = lv_obj_get_child(connect_btn, 0);
    if (!lbl) return;
    switch (wifi_state) {
    case WIFI_ST_CONNECTING: lv_label_set_text(lbl, "Conectando..."); break;
    case WIFI_ST_CONNECTED:  lv_label_set_text(lbl, "Desconectar"); break;
    default:                 lv_label_set_text(lbl, "Conectar"); break;
    }
}

static volatile bool scan_pending;  /* scan pedido mientras conectaba: se lanza al terminar */
static void scan_start(void);       /* requiere lock LVGL */
static void scan_done(void);        /* corre en la tarea de eventos */
static void list_rebuild(void);     /* requiere lock LVGL */

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE)
    {
        scan_done();
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *d = event_data;
        ESP_LOGI("WIFI", "Disconnected reason: %d (%s)", d ? d->reason : -1, wifi_reason_str(d ? d->reason : -1));
        int reason = d ? d->reason : -1;
        /* 8 = desconexión pedida por nosotros (reintento, cancelar, olvidar): no es un error */
        if (reason == 8 && wifi_state != WIFI_ST_CONNECTED)
            return;
        wifi_state = WIFI_ST_DISCONNECTED;
        if ((status_label || wifi_icon_label || connect_btn) && esp_lv_adapter_lock(-1) == ESP_OK)
        {
            update_connect_btn();
            list_rebuild();
            if (scan_pending)
                scan_start();
            if (status_label)
                lv_label_set_text_fmt(status_label, "Desconectado: %s (%d)",
                                      wifi_reason_str(d ? d->reason : -1), d ? d->reason : -1);
            if (wifi_icon_label)
            {
                lv_label_set_text(wifi_icon_label, LV_SYMBOL_WIFI);
                lv_obj_set_style_text_decor(wifi_icon_label, LV_TEXT_DECOR_STRIKETHROUGH, 0);
                lv_obj_set_style_text_color(wifi_icon_label, lv_color_hex(0xff0000), 0);
            }
            esp_lv_adapter_unlock();
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *ev = event_data;
        char buf[64];
        snprintf(buf, sizeof(buf), "Conectado  IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI("WIFI", "Connected to WiFi, IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        wifi_state = WIFI_ST_CONNECTED;
        if ((status_label || wifi_icon_label || connect_btn) && esp_lv_adapter_lock(-1) == ESP_OK)
        {
            update_connect_btn();
            list_rebuild();
            if (scan_pending)
                scan_start();
            if (status_label)
                lv_label_set_text(status_label, buf);
            if (wifi_icon_label)
            {
                lv_label_set_text(wifi_icon_label, LV_SYMBOL_WIFI);
                lv_obj_set_style_text_decor(wifi_icon_label, LV_TEXT_DECOR_NONE, 0);
                lv_obj_set_style_text_color(wifi_icon_label, lv_color_hex(0x0080ff), 0);
            }
            esp_lv_adapter_unlock();
        }
    }
}
static lv_obj_t *ssid_ta;
static lv_obj_t *pass_ta;
static lv_obj_t *ssid_list;
static lv_obj_t *kb;
static lv_obj_t *eez_status_label;
static lv_obj_t *auth_dd;
static bool wifi_inited = false;

void wifi_init(void)
{
    if (wifi_inited)
        return;
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_LOGI("WIFI", "init: free heap %u, min ever %u, internal DRAM %u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    esp_err_t err = esp_wifi_init(&wcfg);
    if (err != ESP_OK)
    {
        ESP_LOGE("WIFI", "esp_wifi_init failed: %s; Wi-Fi unavailable",
                 esp_err_to_name(err));
        return;
    }


    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            wifi_event_handler,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            wifi_event_handler,
            NULL
        )
    );


    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_inited = true;
    wifi_config_auto_connect();
}

void wifi_config_set_icon(lv_obj_t *icon)
{
    wifi_icon_label = icon;
    if (wifi_icon_label)
    {
        lv_label_set_text(wifi_icon_label, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(wifi_icon_label, lv_color_hex(0xff0000), 0);
    }
}

static void ta_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

static void ta_defocus_cb(lv_event_t *e)
{
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

static void connect_cb(lv_event_t *e)
{
    wifi_config_t cfg = {0};
    const char *ssid = lv_textarea_get_text(ssid_ta);
    const char *pass = lv_textarea_get_text(pass_ta);
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    /* Seguridad elegida en el dropdown wifi_auth (EEZ).
     * 0 Auto: WPA2 o superior (abierta si no hay clave) | 1 Abierta | 2 WPA2 | 3 WPA2/WPA3 | 4 WPA3 */
    uint16_t sel = auth_dd ? lv_dropdown_get_selected(auth_dd) : 0;
    switch (sel) {
    case 1:  cfg.sta.threshold.authmode = WIFI_AUTH_OPEN; break;
    case 2:  cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; break;
    case 3:  cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_WPA3_PSK; break;
    case 4:  cfg.sta.threshold.authmode = WIFI_AUTH_WPA3_PSK; break;
    default: cfg.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN; break;
    }
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;

    if (eez_status_label)
    {
        status_label = eez_status_label;
        lv_label_set_text(status_label, "Connecting...");
    }
    else
    {
        status_modal = lv_msgbox_create(lv_scr_act());
        lv_msgbox_add_title(status_modal, "WiFi");
        lv_msgbox_add_text(status_modal, "Connecting...");
        lv_msgbox_add_close_button(status_modal);
        lv_obj_center(status_modal);
        status_label = lv_label_create(lv_msgbox_get_content(status_modal));
        lv_label_set_text(status_label, "Connecting...");
    }

    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) == ESP_OK)
    {
        nvs_set_str(h, "ssid", ssid);
        nvs_set_str(h, "pass", pass);
        nvs_commit(h);
        nvs_close(h);
    }

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_disconnect(); /* corta intento previo (auto-connect) para poder reconectar */
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK)
        err = esp_wifi_connect();
    ESP_LOGI("WIFI", "connect '%s': %s", ssid, esp_err_to_name(err));
    wifi_state = (err == ESP_OK) ? WIFI_ST_CONNECTING : WIFI_ST_DISCONNECTED;
    update_connect_btn();
    if (err != ESP_OK && status_label)
        lv_label_set_text_fmt(status_label, "Error: %s", esp_err_to_name(err));
}

static void forget_cb(lv_event_t *e);

/* Botón único: conectado -> desconectar y olvidar; desconectado -> conectar */
static void connect_toggle_cb(lv_event_t *e)
{
    switch (wifi_state) {
    case WIFI_ST_CONNECTED:
        forget_cb(e);
        break;
    case WIFI_ST_CONNECTING:      /* "Conectando..." -> cancelar intento */
        wifi_state = WIFI_ST_DISCONNECTED;
        esp_wifi_disconnect();
        update_connect_btn();
        if (status_label)
            lv_label_set_text(status_label, "Conexión cancelada");
        break;
    default:
        if (kb)
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);   /* que se vea el estado */
        connect_cb(e);
        break;
    }
}

static void show_pass_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (!pass_ta) return;
    if (code == LV_EVENT_PRESSED)
        lv_textarea_set_password_mode(pass_ta, false);
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
        lv_textarea_set_password_mode(pass_ta, true);
}

static void forget_cb(lv_event_t *e)
{
    (void)e;
    wifi_state = WIFI_ST_DISCONNECTED;  /* antes de desconectar: el evento (motivo 8) se ignora */
    esp_wifi_disconnect();

    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) == ESP_OK)
    {
        nvs_erase_key(h, "ssid");
        nvs_erase_key(h, "pass");
        nvs_commit(h);
        nvs_close(h);
    }

    wifi_config_t cfg = {0};
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    ESP_LOGI("WIFI", "network forgotten");
    update_connect_btn();
    list_rebuild();

    if (ssid_ta)
        lv_textarea_set_text(ssid_ta, "");
    if (pass_ta)
        lv_textarea_set_text(pass_ta, "");
    if (status_label)
        lv_label_set_text(status_label, "Red olvidada");
    if (wifi_icon_label)
    {
        lv_obj_set_style_text_decor(wifi_icon_label, LV_TEXT_DECOR_STRIKETHROUGH, 0);
        lv_obj_set_style_text_color(wifi_icon_label, lv_color_hex(0xff0000), 0);
    }
}

static void ssid_select_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *txt = lv_label_get_text(lv_obj_get_child(btn, 1));
    char ssid[33];

    strncpy(ssid, txt, sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    char *sep = strstr(ssid, "  (");
    if (sep)
    {
        *sep = '\0';
    }
    lv_textarea_set_text(ssid_ta, ssid);
}

static lv_obj_t *scan_btn;
static lv_obj_t *scan_spinner;
static lv_obj_t *scan_lbl;
static volatile bool scanning;

/* Indicador "Buscando redes...". Requiere lock LVGL. */
static void scan_ui_busy(bool on, const char *txt)
{
    if (scan_spinner) {
        if (on) lv_obj_remove_flag(scan_spinner, LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_add_flag(scan_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    /* Texto dentro del botón: estado del scan o "Escanear" en reposo */
    if (scan_lbl)
        lv_label_set_text(scan_lbl, on ? (txt ? txt : "Buscando redes...") : "Escanear");
    if (scan_btn) {
        /* Ícono refresh (hijo 0) se oculta mientras gira el spinner */
        lv_obj_t *icon = lv_obj_get_child(scan_btn, 0);
        if (icon && icon != scan_spinner && icon != scan_lbl) {
            if (on) lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
            else    lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);
        }
        if (on) lv_obj_add_state(scan_btn, LV_STATE_DISABLED);
        else    lv_obj_remove_state(scan_btn, LV_STATE_DISABLED);
    }
}

/* Lanza scan no bloqueante; el resultado llega por WIFI_EVENT_SCAN_DONE. Requiere lock LVGL. */
static void scan_start(void)
{
    if (scanning)
        return;
    scan_pending = false;
    wifi_scan_config_t sc = {0};
    esp_err_t err = esp_wifi_scan_start(&sc, false);
    if (err == ESP_ERR_WIFI_STATE)
    {
        /* Conectando: el driver no deja escanear. Se reintenta al terminar la conexión. */
        ESP_LOGI("WIFI", "scan diferido: conectando");
        scan_pending = true;
        scan_ui_busy(true, "Esperando conexión...");
        return;
    }
    if (err != ESP_OK)
    {
        ESP_LOGE("WIFI", "scan failed: %s", esp_err_to_name(err));
        scan_ui_busy(false, NULL);
        if (status_label)
            lv_label_set_text_fmt(status_label, "Error de escaneo: %s", esp_err_to_name(err));
        return;
    }
    scanning = true;
    scan_ui_busy(true, "Buscando redes...");
}

static void scan_cb(lv_event_t *e)
{
    (void)e;
    scan_start();
}

/* Tarea de eventos: lee resultados y llena la lista con lock LVGL. */
/* Último resultado de scan, para redibujar la lista sin volver a escanear */
#define SCAN_CACHE_MAX 30
typedef struct { char ssid[33]; int8_t rssi; uint8_t ch; } ap_entry_t;
static ap_entry_t scan_cache[SCAN_CACHE_MAX];
static uint16_t scan_cache_n;
static bool scan_cache_valid;

#define WIFI_BLUE lv_color_hex(0x0080ff)

/* Lista: primero la red conectada (azul), después el scan sin repetirla. Requiere lock LVGL. */
static void list_rebuild(void)
{
    if (!ssid_list)
        return;
    lv_obj_clean(ssid_list);

    char cur[33] = {0};
    wifi_ap_record_t ap;
    if (wifi_state == WIFI_ST_CONNECTED && esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
    {
        strlcpy(cur, (const char *)ap.ssid, sizeof(cur));
        char buf[64];
        snprintf(buf, sizeof(buf), "%s  (Conectado, RSSI %d)", cur, ap.rssi);
        lv_obj_t *btn = lv_list_add_button(ssid_list, LV_SYMBOL_WIFI, buf);
        lv_obj_set_style_text_color(btn, WIFI_BLUE, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xe3f0ff), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(btn, ssid_select_cb, LV_EVENT_CLICKED, NULL);
    }

    uint16_t shown = 0;
    for (uint16_t i = 0; i < scan_cache_n; i++)
    {
        if (cur[0] && strcmp(scan_cache[i].ssid, cur) == 0)
            continue;   /* ya está arriba como conectada */
        char buf[64];
        snprintf(buf, sizeof(buf), "%s  (RSSI %d, Ch %d)", scan_cache[i].ssid, scan_cache[i].rssi, scan_cache[i].ch);
        lv_obj_t *btn = lv_list_add_button(ssid_list, LV_SYMBOL_WIFI, buf);
        lv_obj_add_event_cb(btn, ssid_select_cb, LV_EVENT_CLICKED, NULL);
        shown++;
    }
    if (scan_cache_valid && shown == 0 && !cur[0])
        lv_list_add_text(ssid_list, "No se encontraron redes");
}

/* Tarea de eventos: guarda resultados y redibuja la lista con lock LVGL. */
static void scan_done(void)
{
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    wifi_ap_record_t *recs = n ? calloc(n, sizeof(wifi_ap_record_t)) : NULL;
    if (recs)
        esp_wifi_scan_get_ap_records(&n, recs);
    else
    {
        esp_wifi_clear_ap_list();   /* libera la memoria interna del scan */
        n = 0;
    }
    ESP_LOGI("WIFI", "scan: %u APs", (unsigned)n);

    if (esp_lv_adapter_lock(-1) == ESP_OK)
    {
        scan_cache_n = 0;
        for (uint16_t i = 0; i < n && scan_cache_n < SCAN_CACHE_MAX; i++)
        {
            if (!recs[i].ssid[0])
                continue;   /* redes ocultas */
            ap_entry_t *c = &scan_cache[scan_cache_n++];
            strlcpy(c->ssid, (const char *)recs[i].ssid, sizeof(c->ssid));
            c->rssi = recs[i].rssi;
            c->ch = recs[i].primary;
        }
        scan_cache_valid = true;
        scanning = false;
        scan_ui_busy(false, NULL);
        list_rebuild();
        esp_lv_adapter_unlock();
    }
    else
        scanning = false;
    free(recs);
}

#ifdef HAVE_EEZ_UI
/* Arranque de la pantalla: primero la red conectada (y el último scan), después scan nuevo */
static void wifi_screen_loaded_cb(lv_event_t *e)
{
    (void)e;
    /* Vista limpia al entrar */
    if (status_label)
    {
        lv_label_set_text(status_label, "");
        esp_netif_ip_info_t ip;
        esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (wifi_state == WIFI_ST_CONNECTED && sta &&
            esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr != 0)
        {
            char buf[48];
            snprintf(buf, sizeof(buf), "Conectado  IP: " IPSTR, IP2STR(&ip.ip));
            lv_label_set_text(status_label, buf);
        }
    }
    if (ssid_ta)
        lv_textarea_set_text(ssid_ta, "");
    if (pass_ta)
    {
        lv_textarea_set_text(pass_ta, "");
        lv_textarea_set_password_mode(pass_ta, true);
    }
    if (kb)
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    /* Vaciar lista: descartar resultados de scans previos */
    scan_cache_n = 0;
    scan_cache_valid = false;
    if (ssid_list)
        lv_obj_clean(ssid_list);
    list_rebuild();
    scan_start();
}
#endif

static void format_bytes(char *buf, size_t len, uint32_t bytes)
{
    if (bytes >= 1024u * 1024u) {
        snprintf(buf, len, "%u.%02u MB", (unsigned)(bytes / (1024u * 1024u)),
                 (unsigned)((bytes % (1024u * 1024u)) * 100u / (1024u * 1024u)));
    } else if (bytes >= 1024u) {
        snprintf(buf, len, "%u KB", (unsigned)(bytes / 1024u));
    } else {
        snprintf(buf, len, "%u B", (unsigned)bytes);
    }
}

static void sys_info_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    char line[96];
    char heap[24];
    char min_heap[24];
    char int_heap[24];
    char psram_heap[24];

    if (sys_heap_label) {
        format_bytes(heap, sizeof(heap), esp_get_free_heap_size());
        format_bytes(min_heap, sizeof(min_heap), esp_get_minimum_free_heap_size());
        snprintf(line, sizeof(line), "Heap free: %s  min ever: %s", heap, min_heap);
        lv_label_set_text(sys_heap_label, line);
    }

    if (sys_mem_label) {
        format_bytes(int_heap, sizeof(int_heap), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        format_bytes(psram_heap, sizeof(psram_heap), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        snprintf(line, sizeof(line), "Internal free: %s  PSRAM free: %s", int_heap, psram_heap);
        lv_label_set_text(sys_mem_label, line);
    }

    if (sys_flash_label) {
        uint32_t flash_size = 0;
        esp_err_t err = esp_flash_get_physical_size(NULL, &flash_size);
        if (err == ESP_OK && flash_size > 0) {
            format_bytes(heap, sizeof(heap), flash_size);
            snprintf(line, sizeof(line), "Flash size: %s", heap);
        } else {
            snprintf(line, sizeof(line), "Flash size: unavailable (%s)", esp_err_to_name(err));
        }
        lv_label_set_text(sys_flash_label, line);
    }

    if (sys_cpu_label) {
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
        static char stats[768];
        vTaskGetRunTimeStats(stats);
        char *idle0 = strstr(stats, "IDLE0");
        char *idle1 = strstr(stats, "IDLE1");
        unsigned long pct0 = 100, pct1 = 100;
        if (idle0) {
            char *p = idle0;
            while (*p && *p != '\t' && *p != ' ') p++;
            while (*p == '\t' || *p == ' ') p++;
            while (*p && (*p < '0' || *p > '9') && *p != '\n') p++;
            if (*p >= '0' && *p <= '9') pct0 = strtoul(p, NULL, 10);
        }
        if (idle1) {
            char *p = idle1;
            while (*p && *p != '\t' && *p != ' ') p++;
            while (*p == '\t' || *p == ' ') p++;
            while (*p && (*p < '0' || *p > '9') && *p != '\n') p++;
            if (*p >= '0' && *p <= '9') pct1 = strtoul(p, NULL, 10);
        }
        snprintf(line, sizeof(line), "CPU util (approx): core0 %lu%% core1 %lu%%",
                 100ul - (pct0 > 100 ? 100 : pct0),
                 100ul - (pct1 > 100 ? 100 : pct1));
#else
        UBaseType_t tasks = uxTaskGetNumberOfTasks();
        snprintf(line, sizeof(line), "CPU util: enable CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS (%u tasks)", (unsigned)tasks);
#endif
        lv_label_set_text(sys_cpu_label, line);
    }

    if (sys_chip_label) {
        esp_chip_info_t info;
        esp_chip_info(&info);
        snprintf(line, sizeof(line), "Chip: cores=%d rev=%d  IDF %s",
                 info.cores, info.revision, esp_get_idf_version());
        lv_label_set_text(sys_chip_label, line);
    }
}

lv_obj_t *wifi_config_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(98), LV_PCT(98));
    lv_obj_center(root);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_all(root, 8, 0);
    lv_obj_set_style_pad_row(root, 6, 0);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "System");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    sys_chip_label = lv_label_create(root);
    lv_obj_set_width(sys_chip_label, LV_PCT(100));
    lv_label_set_long_mode(sys_chip_label, LV_LABEL_LONG_WRAP);

    sys_heap_label = lv_label_create(root);
    lv_obj_set_width(sys_heap_label, LV_PCT(100));
    lv_label_set_long_mode(sys_heap_label, LV_LABEL_LONG_WRAP);

    sys_mem_label = lv_label_create(root);
    lv_obj_set_width(sys_mem_label, LV_PCT(100));
    lv_label_set_long_mode(sys_mem_label, LV_LABEL_LONG_WRAP);

    sys_flash_label = lv_label_create(root);
    lv_obj_set_width(sys_flash_label, LV_PCT(100));
    lv_label_set_long_mode(sys_flash_label, LV_LABEL_LONG_WRAP);

    sys_cpu_label = lv_label_create(root);
    lv_obj_set_width(sys_cpu_label, LV_PCT(100));
    lv_label_set_long_mode(sys_cpu_label, LV_LABEL_LONG_WRAP);

    kb = lv_keyboard_create(lv_scr_act());
    lv_obj_set_size(kb, LV_PCT(100), LV_PCT(40));
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *inact_ta = display_config_add_ui(root);
    if (inact_ta) {
        lv_obj_add_event_cb(inact_ta, ta_focus_cb, LV_EVENT_FOCUSED, NULL);
        lv_obj_add_event_cb(inact_ta, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
    }

    lv_obj_t *wifi_title = lv_label_create(root);
    lv_label_set_text(wifi_title, "WiFi");
    lv_obj_set_style_text_font(wifi_title, &lv_font_montserrat_20, 0);

    lv_obj_t *l = lv_label_create(root);
    lv_label_set_text(l, "SSID");
    ssid_ta = lv_textarea_create(root);
    lv_textarea_set_one_line(ssid_ta, true);
    lv_obj_set_width(ssid_ta, LV_PCT(100));
    lv_obj_add_event_cb(ssid_ta, ta_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ssid_ta, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    l = lv_label_create(root);
    lv_label_set_text(l, "Password");
    pass_ta = lv_textarea_create(root);
    lv_textarea_set_password_mode(pass_ta, true);
    lv_textarea_set_one_line(pass_ta, true);
    lv_obj_set_width(pass_ta, LV_PCT(100));
    lv_obj_add_event_cb(pass_ta, ta_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(pass_ta, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    lv_obj_t *row = lv_obj_create(root);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);

    lv_obj_t *btn = lv_button_create(row);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "Connect");
    lv_obj_add_event_cb(btn, connect_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *scan_btn = lv_button_create(row);
    lv_obj_t *sl = lv_label_create(scan_btn);
    lv_label_set_text(sl, "Scan");
    lv_obj_add_event_cb(scan_btn, scan_cb, LV_EVENT_CLICKED, NULL);

    ssid_list = lv_list_create(root);
    lv_obj_set_size(ssid_list, LV_PCT(100), 140);

    sys_info_timer_cb(NULL);
    lv_timer_create(sys_info_timer_cb, 1000, NULL);
    return root;
}

void wifi_config_auto_connect(void)
{
    nvs_handle_t h;
     esp_err_t err;
    if (nvs_open("wifi", NVS_READONLY, &h) != ESP_OK)
        return;
    char ssid[33] = {0}, pass[65] = {0};
    size_t slen = sizeof(ssid), plen = sizeof(pass);
    if (nvs_get_str(h, "ssid", ssid, &slen) == ESP_OK &&
        nvs_get_str(h, "pass", pass, &plen) == ESP_OK && ssid[0])
    {
        wifi_config_t cfg = {0};
        
        strlcpy((char *)cfg.sta.ssid,
                ssid,
                sizeof(cfg.sta.ssid));

        strlcpy((char *)cfg.sta.password,
                pass,
                sizeof(cfg.sta.password));        
        
        cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
        cfg.sta.pmf_cfg.capable = true;
        cfg.sta.pmf_cfg.required = false;
        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK)
        {
            ESP_LOGE("WIFI", "esp_wifi_set_mode failed: %s",
                     esp_err_to_name(err));
            return;
        }

        err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
        if (err != ESP_OK)
        {
            ESP_LOGE("WIFI", "esp_wifi_set_config failed: %s",
                     esp_err_to_name(err));
            return;
        }

        err = esp_wifi_connect();
        if (err != ESP_OK)
        {
            ESP_LOGE("WIFI", "esp_wifi_connect failed: %s",
                     esp_err_to_name(err));
            return;
        }

        wifi_state = WIFI_ST_CONNECTING;
        ESP_LOGI("WIFI", "Connecting SSID(%lu): %s PASS(%lu): %s", (unsigned long)slen, ssid, (unsigned long)plen, pass);
    }
    nvs_close(h);
}
#ifdef HAVE_EEZ_UI
#include "ui/screens.h"

/* Enlaza los widgets diseñados en EEZ (página Wifi) con la lógica Wi-Fi.
 * Llamar con lock LVGL tomado, después de ui_init(). */
void wifi_config_bind_eez(void)
{
    ssid_ta = objects.wifi_ssid;
    pass_ta = objects.wifi_pass;
    ssid_list = objects.wifi_list;
    kb = objects.wifi_kb;
    eez_status_label = objects.wifi_status;
    auth_dd = objects.wifi_auth;

    lv_obj_t *tas[] = {ssid_ta, pass_ta};
    for (int i = 0; i < 2; i++) {
        if (!tas[i]) continue;
        lv_obj_add_event_cb(tas[i], ta_focus_cb, LV_EVENT_FOCUSED, NULL);
        /* Sin DEFOCUSED: tocar el teclado le saca el foco al textarea y lo ocultaría */
    }
    if (kb)
    {
        lv_obj_remove_flag(kb, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_add_event_cb(kb, ta_defocus_cb, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(kb, ta_defocus_cb, LV_EVENT_CANCEL, NULL);
    }
    connect_btn = objects.wifi_connect;
    if (connect_btn)
    {
        lv_obj_add_event_cb(connect_btn, connect_toggle_cb, LV_EVENT_CLICKED, NULL);
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
            wifi_state = WIFI_ST_CONNECTED;
        update_connect_btn();
    }
    if (objects.wifi_show)
        lv_obj_add_event_cb(objects.wifi_show, show_pass_cb, LV_EVENT_ALL, NULL);
    scan_btn = objects.wifi_scan;
    scan_spinner = objects.wifi_scan_spinner;
    scan_lbl = objects.wifi_scan_lbl;
    if (scan_spinner)   /* arco blanco: se ve sobre el fondo azul del botón */
        lv_obj_set_style_arc_color(scan_spinner, lv_color_white(), LV_PART_INDICATOR);
    if (scan_lbl)
        lv_label_set_text(scan_lbl, "Escanear");
    if (scan_btn)
    {
        lv_obj_add_event_cb(scan_btn, scan_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *icon = lv_obj_get_child(scan_btn, 0);
        if (icon)
            lv_label_set_text(icon, LV_SYMBOL_REFRESH);
    }
    /* Escanear cada vez que se abre la pantalla Wifi */
    if (objects.wifi)
        lv_obj_add_event_cb(objects.wifi, wifi_screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
    if (objects.wifi_list)
        lv_obj_add_flag(objects.wifi_list, LV_OBJ_FLAG_SCROLLABLE);
    status_label = objects.wifi_status;
    ESP_LOGI("WIFI", "bind_eez: scan=%p list=%p", (void *)objects.wifi_scan, (void *)objects.wifi_list);
}
#endif
