#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include "lvgl.h"

lv_obj_t *wifi_config_create(lv_obj_t *parent);
void wifi_config_auto_connect(void);
void wifi_init(void);
#endif