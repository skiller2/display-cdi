#include <stdio.h>
#include <stdlib.h>

#include "display_config.h"
#include "nvs.h"

static uint32_t inactivity_timeout_ms = DISPLAY_DEFAULT_INACTIVITY_TIMEOUT_MS;

void display_config_load(void)
{
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READONLY, &h) != ESP_OK) {
        inactivity_timeout_ms = DISPLAY_DEFAULT_INACTIVITY_TIMEOUT_MS;
        return;
    }

    uint32_t v = DISPLAY_DEFAULT_INACTIVITY_TIMEOUT_MS;
    if (nvs_get_u32(h, "inact_ms", &v) == ESP_OK && v >= 1000u && v <= 3600000u) {
        inactivity_timeout_ms = v;
    } else {
        inactivity_timeout_ms = DISPLAY_DEFAULT_INACTIVITY_TIMEOUT_MS;
    }
    nvs_close(h);
}

uint32_t display_config_get_inactivity_timeout_ms(void)
{
    return inactivity_timeout_ms ? inactivity_timeout_ms : DISPLAY_DEFAULT_INACTIVITY_TIMEOUT_MS;
}
