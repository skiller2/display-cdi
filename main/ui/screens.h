#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_MAIN = 1,
    SCREEN_ID_LOGS = 2,
    SCREEN_ID_SYS_INFO = 3,
    SCREEN_ID_WIFI = 4,
    SCREEN_ID_CONFIG = 5,
    _SCREEN_ID_LAST = 5
};

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *logs;
    lv_obj_t *sys_info;
    lv_obj_t *wifi;
    lv_obj_t *config;
    lv_obj_t *status_bar;
    lv_obj_t *st_left;
    lv_obj_t *st_alarma;
    lv_obj_t *st_falla;
    lv_obj_t *st_desconexion;
    lv_obj_t *st_tierra;
    lv_obj_t *st_test;
    lv_obj_t *st_extincion;
    lv_obj_t *st_bateria;
    lv_obj_t *st_alimentacion;
    lv_obj_t *st_red;
    lv_obj_t *st_mod_i1;
    lv_obj_t *obj0;
    lv_obj_t *st_mod_i2;
    lv_obj_t *obj1;
    lv_obj_t *st_mod_o1;
    lv_obj_t *obj2;
    lv_obj_t *st_mod_o2;
    lv_obj_t *obj3;
    lv_obj_t *st_autman_box;
    lv_obj_t *st_autman;
    lv_obj_t *main_header;
    lv_obj_t *main_title;
    lv_obj_t *main_clock;
    lv_obj_t *btn_ack;
    lv_obj_t *obj4;
    lv_obj_t *btn_reset;
    lv_obj_t *obj5;
    lv_obj_t *btn_test;
    lv_obj_t *obj6;
    lv_obj_t *btn_config;
    lv_obj_t *obj7;
    lv_obj_t *bars_list;
    lv_obj_t *fault_conn;
    lv_obj_t *obj8;
    lv_obj_t *obj9;
    lv_obj_t *fault_time;
    lv_obj_t *fault_time_icon;
    lv_obj_t *fault_time_txt;
    lv_obj_t *obj10;
    lv_obj_t *logs_back;
    lv_obj_t *obj11;
    lv_obj_t *lista_logs;
    lv_obj_t *obj12;
    lv_obj_t *sys_card;
    lv_obj_t *sys_chip;
    lv_obj_t *sys_fw;
    lv_obj_t *sys_uptime;
    lv_obj_t *sys_reset;
    lv_obj_t *sys_heap;
    lv_obj_t *sys_mem;
    lv_obj_t *sys_lvgl;
    lv_obj_t *sys_flash;
    lv_obj_t *sys_cpu;
    lv_obj_t *sys_temp;
    lv_obj_t *sys_net;
    lv_obj_t *sys_time;
    lv_obj_t *obj13;
    lv_obj_t *obj14;
    lv_obj_t *obj15;
    lv_obj_t *obj16;
    lv_obj_t *sys_sep5;
    lv_obj_t *sys_sep6;
    lv_obj_t *sys_sep7;
    lv_obj_t *sys_sep8;
    lv_obj_t *sys_sep9;
    lv_obj_t *sys_sep10;
    lv_obj_t *sys_sep11;
    lv_obj_t *sys_back;
    lv_obj_t *obj17;
    lv_obj_t *obj18;
    lv_obj_t *obj19;
    lv_obj_t *wifi_ssid;
    lv_obj_t *obj20;
    lv_obj_t *wifi_pass;
    lv_obj_t *wifi_connect;
    lv_obj_t *obj21;
    lv_obj_t *wifi_scan;
    lv_obj_t *obj22;
    lv_obj_t *wifi_scan_spinner;
    lv_obj_t *wifi_scan_lbl;
    lv_obj_t *wifi_status;
    lv_obj_t *wifi_list;
    lv_obj_t *wifi_back;
    lv_obj_t *obj23;
    lv_obj_t *wifi_kb;
    lv_obj_t *wifi_show;
    lv_obj_t *obj24;
    lv_obj_t *obj25;
    lv_obj_t *cfg_wifi;
    lv_obj_t *obj26;
    lv_obj_t *obj27;
    lv_obj_t *cfg_sysinfo;
    lv_obj_t *obj28;
    lv_obj_t *obj29;
    lv_obj_t *cfg_logs;
    lv_obj_t *obj30;
    lv_obj_t *obj31;
    lv_obj_t *cfg_back;
    lv_obj_t *obj32;
} objects_t;

extern objects_t objects;

void create_screen_main();
void tick_screen_main();

void create_screen_logs();
void tick_screen_logs();

void create_screen_sys_info();
void tick_screen_sys_info();

void create_screen_wifi();
void tick_screen_wifi();

void create_screen_config();
void tick_screen_config();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/