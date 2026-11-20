#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_info.h"

#include "ui/screens.h"

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

static void sys_info_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    char line[96];
    char a[24], b[24];

    if (objects.sys_heap) {
        format_bytes(a, sizeof(a), esp_get_free_heap_size());
        format_bytes(b, sizeof(b), esp_get_minimum_free_heap_size());
        snprintf(line, sizeof(line), "Heap free: %s  min ever: %s", a, b);
        lv_label_set_text(objects.sys_heap, line);
    }

    if (objects.sys_mem) {
        format_bytes(a, sizeof(a), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        format_bytes(b, sizeof(b), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        snprintf(line, sizeof(line), "Internal free: %s  PSRAM free: %s", a, b);
        lv_label_set_text(objects.sys_mem, line);
    }

    if (objects.sys_flash) {
        uint32_t flash_size = 0;
        esp_err_t err = esp_flash_get_physical_size(NULL, &flash_size);
        if (err == ESP_OK && flash_size > 0) {
            format_bytes(a, sizeof(a), flash_size);
            snprintf(line, sizeof(line), "Flash size: %s", a);
        } else {
            snprintf(line, sizeof(line), "Flash size: unavailable (%s)", esp_err_to_name(err));
        }
        lv_label_set_text(objects.sys_flash, line);
    }

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

    if (objects.sys_chip) {
        esp_chip_info_t info;
        esp_chip_info(&info);
        snprintf(line, sizeof(line), "Chip: cores=%d rev=%d  IDF %s",
                 info.cores, info.revision, esp_get_idf_version());
        lv_label_set_text(objects.sys_chip, line);
    }
}

void sys_info_start(void)
{
    sys_info_timer_cb(NULL);
    lv_timer_create(sys_info_timer_cb, 1000, NULL);
}
