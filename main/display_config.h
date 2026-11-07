#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include <stdint.h>
#include "lvgl.h"

#define DISPLAY_DEFAULT_INACTIVITY_TIMEOUT_MS 10000u

void display_config_load(void);
uint32_t display_config_get_inactivity_timeout_ms(void);
lv_obj_t *display_config_add_ui(lv_obj_t *parent);

#endif
