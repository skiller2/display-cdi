#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include "lvgl.h"

void wifi_config_auto_connect(void);
void wifi_init(void);
void wifi_config_bind_eez(void);   /* enlaza widgets de EEZ (página Wifi) */
#endif