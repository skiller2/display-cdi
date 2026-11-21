#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver/temperature_sensor.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_clk_tree.h"
#include "esp_timer.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_info.h"

#include "ui/screens.h"

// ---- Sistema ----

// Nombre del modelo de chip a partir del enum de esp_chip_info.
static const char *chip_model_str(esp_chip_model_t m)
{
    switch (m) {
    case CHIP_ESP32:   return "ESP32";
    case CHIP_ESP32S2: return "ESP32-S2";
    case CHIP_ESP32S3: return "ESP32-S3";
    case CHIP_ESP32C3: return "ESP32-C3";
    case CHIP_ESP32C2: return "ESP32-C2";
    case CHIP_ESP32C6: return "ESP32-C6";
    default:           return "Unknown";
    }
}

// Causa del ultimo reinicio (power-on, panic, watchdog, brownout...).
// Se lee al arrancar: no cambia mientras el equipo corre.
static const char *reset_reason_str(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "Power-on";
    case ESP_RST_SW:        return "Software";
    case ESP_RST_PANIC:     return "Panic";
    case ESP_RST_INT_WDT:   return "WDT int";
    case ESP_RST_TASK_WDT:  return "WDT task";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "Deep sleep";
    case ESP_RST_BROWNOUT:  return "Brownout";
    case ESP_RST_EXT:       return "Externo";
    default:                return "Otro";
    }
}

// ---- Memoria ----

// Convierte bytes a texto legible: "512 B", "64 KB" o "7.50 MB".
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

// Velocidad de la flash, tomada de la config de compilacion (menuconfig).
#if CONFIG_ESPTOOLPY_FLASHFREQ_40M
#define FLASH_SPEED_STR "40M"
#elif CONFIG_ESPTOOLPY_FLASHFREQ_80M
#define FLASH_SPEED_STR "80M"
#elif CONFIG_ESPTOOLPY_FLASHFREQ_120M
#define FLASH_SPEED_STR "120M"
#else
#define FLASH_SPEED_STR "?"
#endif

// Modo de la flash (QIO/QOUT/DIO/DOUT), tomado de la config de compilacion.
#if CONFIG_ESPTOOLPY_FLASHMODE_QIO
#define FLASH_MODE_STR "QIO"
#elif CONFIG_ESPTOOLPY_FLASHMODE_QOUT
#define FLASH_MODE_STR "QOUT"
#elif CONFIG_ESPTOOLPY_FLASHMODE_DIO
#define FLASH_MODE_STR "DIO"
#elif CONFIG_ESPTOOLPY_FLASHMODE_DOUT
#define FLASH_MODE_STR "DOUT"
#else
#define FLASH_MODE_STR "?"
#endif

// ---- Rendimiento ----

// Extrae el % de tiempo de una tarea IDLE del texto de vTaskGetRunTimeStats().
// Uso de CPU = 100 - % de IDLE. Solo existe si se activan las run-time stats.
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
static unsigned long idle_pct(const char *stats, const char *name)
{
    char *p = strstr(stats, name);
    if (!p) return 100;
    while (*p && *p != '\t' && *p != ' ') p++;
    while (*p == '\t' || *p == ' ') p++;
    while (*p && (*p < '0' || *p > '9') && *p != '\n') p++;
    if (*p >= '0' && *p <= '9') return strtoul(p, NULL, 10);
    return 100;
}
#endif

// Handle del sensor de temperatura interno. Se crea una vez en sys_info_start();
// queda en NULL si el driver falla (el callback muestra "n/a").
static temperature_sensor_handle_t s_temp;

// Callback del timer LVGL (cada 1 s): lee los datos del sistema y actualiza
// cada label de la pantalla. Cada bloque "if (objects.xxx)" salta si el label
// no existe en la UI.
static void sys_info_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    char line[192];
    char a[24], b[24], c[24];

    // ---- Sistema ----

    // Chip: modelo, nucleos, revision, frecuencia de CPU y version de ESP-IDF.
    if (objects.sys_chip) {
        esp_chip_info_t info;
        esp_chip_info(&info);
        uint32_t hz = 0;
        esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &hz);
        snprintf(line, sizeof(line), "Chip: %s cores=%d rev=%d  CPU %u MHz  IDF %s",
                 chip_model_str(info.model), info.cores, info.revision, (unsigned)(hz / 1000000), esp_get_idf_version());
        lv_label_set_text(objects.sys_chip, line);
    }

    // Firmware: nombre, version y fecha/hora de compilacion (esp_app_desc).
    if (objects.sys_fw) {
        const esp_app_desc_t *app = esp_app_get_description();
        snprintf(line, sizeof(line), "Firmware: %s %s  (%s %s)",
                 app->project_name, app->version, app->date, app->time);
        lv_label_set_text(objects.sys_fw, line);
    }

    // Uptime: tiempo desde el arranque en hh:mm:ss.
    if (objects.sys_uptime) {
        uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
        snprintf(line, sizeof(line), "Uptime: %02u:%02u:%02u",
                 (unsigned)(up / 3600), (unsigned)((up / 60) % 60), (unsigned)(up % 60));
        lv_label_set_text(objects.sys_uptime, line);
    }

    // Motivo del ultimo reset.
    if (objects.sys_reset) {
        snprintf(line, sizeof(line), "Reset reason: %s", reset_reason_str());
        lv_label_set_text(objects.sys_reset, line);
    }

    // ---- Memoria ----

    // Heap: libre ahora, minimo historico y mayor bloque contiguo libre
    // (indica fragmentacion: si es mucho menor que el libre, esta fragmentado).
    if (objects.sys_heap) {
        format_bytes(a, sizeof(a), esp_get_free_heap_size());
        format_bytes(b, sizeof(b), esp_get_minimum_free_heap_size());
        format_bytes(c, sizeof(c), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        snprintf(line, sizeof(line), "Heap free: %s  min: %s  largest blk: %s", a, b, c);
        lv_label_set_text(objects.sys_heap, line);
    }

    // Memoria por tipo: RAM interna y PSRAM externa libres.
    if (objects.sys_mem) {
        format_bytes(a, sizeof(a), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        format_bytes(b, sizeof(b), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        snprintf(line, sizeof(line), "Internal free: %s  PSRAM free: %s", a, b);
        lv_label_set_text(objects.sys_mem, line);
    }

    // Memoria del heap de LVGL: usada, % de uso, % de fragmentacion y mayor
    // bloque libre. Con malloc de libc el monitor devuelve 0 -> "n/a".
    if (objects.sys_lvgl) {
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        if (mon.total_size > 0) {
            format_bytes(a, sizeof(a), mon.total_size - mon.free_size);
            format_bytes(b, sizeof(b), mon.free_biggest_size);
            snprintf(line, sizeof(line), "LVGL mem: %s used (%u%%)  frag %u%%  largest free: %s",
                     a, (unsigned)mon.used_pct, (unsigned)mon.frag_pct, b);
        } else {
            snprintf(line, sizeof(line), "LVGL mem: n/a (stdlib malloc)");
        }
        lv_label_set_text(objects.sys_lvgl, line);
    }

    // Flash: tamano fisico, velocidad y modo.
    if (objects.sys_flash) {
        uint32_t flash_size = 0;
        esp_err_t err = esp_flash_get_physical_size(NULL, &flash_size);
        if (err == ESP_OK && flash_size > 0) {
            format_bytes(a, sizeof(a), flash_size);
            snprintf(line, sizeof(line), "Flash size: %s  %s %s", a, FLASH_SPEED_STR, FLASH_MODE_STR);
        } else {
            snprintf(line, sizeof(line), "Flash size: unavailable (%s)", esp_err_to_name(err));
        }
        lv_label_set_text(objects.sys_flash, line);
    }

    // ---- Rendimiento ----

    // CPU: uso aproximado por nucleo (100 - % IDLE). Sin run-time stats,
    // solo muestra la cantidad de tareas.
    if (objects.sys_cpu) {
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
        static char stats[768];
        vTaskGetRunTimeStats(stats);
        unsigned long p0 = idle_pct(stats, "IDLE0");
        unsigned long p1 = idle_pct(stats, "IDLE1");
        snprintf(line, sizeof(line), "CPU util (approx): core0 %lu%% core1 %lu%%",
                 100ul - (p0 > 100 ? 100 : p0), 100ul - (p1 > 100 ? 100 : p1));
#else
        snprintf(line, sizeof(line), "CPU util: enable CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS (%u tasks)",
                 (unsigned)uxTaskGetNumberOfTasks());
#endif
        lv_label_set_text(objects.sys_cpu, line);
    }

    // Temperatura interna del chip. "n/a" si el sensor no se pudo iniciar.
    if (objects.sys_temp) {
        float deg;
        if (s_temp && temperature_sensor_get_celsius(s_temp, &deg) == ESP_OK) {
            snprintf(line, sizeof(line), "Temperature: %.1f C", deg);
        } else {
            snprintf(line, sizeof(line), "Temperature: n/a");
        }
        lv_label_set_text(objects.sys_temp, line);
    }

    // ---- Red y hora ----

    // Red: IP de la interfaz WiFi STA ("-" si no hay IP) y MAC del equipo.
    if (objects.sys_net) {
        char ip[16] = "-";
        esp_netif_ip_info_t ipi;
        esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta && esp_netif_get_ip_info(sta, &ipi) == ESP_OK && ipi.ip.addr != 0) {
            snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ipi.ip));
        }
        uint8_t mac[6] = {0};
        esp_efuse_mac_get_default(mac);
        snprintf(line, sizeof(line), "WiFi IP: %s  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 ip, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        lv_label_set_text(objects.sys_net, line);
    }

    // Hora del sistema (se llena al sincronizar por SNTP). Si el ano es
    // menor a 2024, se asume que la hora no esta configurada.
    if (objects.sys_time) {
        time_t now;
        struct tm tm;
        time(&now);
        localtime_r(&now, &tm);
        if (tm.tm_year + 1900 >= 2024) {
            snprintf(line, sizeof(line), "System time: %04d-%02d-%02d %02d:%02d:%02d",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        } else {
            snprintf(line, sizeof(line), "System time: not set");
        }
        lv_label_set_text(objects.sys_time, line);
    }
}

// Inicializa el modulo: arranca el sensor de temperatura (una sola vez),
// hace una primera actualizacion inmediata (para no mostrar los textos por
// defecto de EEZ) y programa el refresco cada 1000 ms.
void sys_info_start(void)
{
    // Rango de medicion -10..80 C.
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&cfg, &s_temp) != ESP_OK ||
        temperature_sensor_enable(s_temp) != ESP_OK) {
        s_temp = NULL;
    }
    // Primera lectura manual; el timer recien dispara despues de 1 s.
    sys_info_timer_cb(NULL);
    lv_timer_create(sys_info_timer_cb, 1000, NULL);
}
