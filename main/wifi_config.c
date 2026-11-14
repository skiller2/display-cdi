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

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *d = event_data;
        ESP_LOGI("WIFI", "Disconnected reason: %d", d ? d->reason : -1);
        if (status_label)
            lv_label_set_text(status_label, "Disconnected");
        if (wifi_icon_label)
        {
            lv_label_set_text(wifi_icon_label, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_decor(wifi_icon_label, LV_TEXT_DECOR_STRIKETHROUGH, 0);
            lv_obj_set_style_text_color(wifi_icon_label, lv_color_hex(0xff0000), 0);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *ev = event_data;
        char buf[64];
        snprintf(buf, sizeof(buf), "Connected\nIP: " IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI("WIFI", "Connected to WiFi, IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        if (status_label)
            lv_label_set_text(status_label, buf);
        if (wifi_icon_label)
        {
            lv_label_set_text(wifi_icon_label, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_decor(wifi_icon_label, LV_TEXT_DECOR_NONE, 0);
            lv_obj_set_style_text_color(wifi_icon_label, lv_color_hex(0x0080ff), 0);
        }
    }
}
static lv_obj_t *ssid_ta;
static lv_obj_t *pass_ta;
static lv_obj_t *ssid_list;
static lv_obj_t *kb;
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
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;

    status_modal = lv_msgbox_create(lv_scr_act());
    lv_msgbox_add_title(status_modal, "WiFi");
    lv_msgbox_add_text(status_modal, "Connecting...");
    lv_msgbox_add_close_button(status_modal);
    lv_obj_center(status_modal);
    status_label = lv_label_create(lv_msgbox_get_content(status_modal));
    lv_label_set_text(status_label, "Connecting...");

    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) == ESP_OK)
    {
        nvs_set_str(h, "ssid", ssid);
        nvs_set_str(h, "pass", pass);
        nvs_commit(h);
        nvs_close(h);
    }

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_connect();
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

static void scan_cb(lv_event_t *e)
{
    wifi_scan_config_t sc = {0};
    esp_wifi_scan_start(&sc, true);
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    wifi_ap_record_t *recs = calloc(n, sizeof(wifi_ap_record_t));
    esp_wifi_scan_get_ap_records(&n, recs);

    lv_obj_clean(ssid_list);
    for (uint16_t i = 0; i < n; i++)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s  (RSSI %d, Ch %d)", (char *)recs[i].ssid, recs[i].rssi, recs[i].primary);
        lv_obj_t *btn = lv_list_add_button(ssid_list, LV_SYMBOL_WIFI, buf);
        lv_obj_add_event_cb(btn, ssid_select_cb, LV_EVENT_CLICKED, NULL);
    }
    free(recs);
}

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

        ESP_LOGI("WIFI", "Connecting SSID(%lu): %s PASS(%lu): %s", (unsigned long)slen, ssid, (unsigned long)plen, pass);
    }
    nvs_close(h);
}