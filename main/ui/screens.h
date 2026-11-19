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
    _SCREEN_ID_LAST = 3
};

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *logs;
    lv_obj_t *sys_info;
    lv_obj_t *obj0;
    lv_obj_t *obj1;
    lv_obj_t *enviar_ack;
    lv_obj_t *enviar_reset;
    lv_obj_t *enviar_test;
    lv_obj_t *obj2;
    lv_obj_t *lista_logs;
    lv_obj_t *sys_chip;
    lv_obj_t *sys_heap;
    lv_obj_t *sys_mem;
    lv_obj_t *sys_flash;
    lv_obj_t *sys_cpu;
    lv_obj_t *obj3;
} objects_t;

extern objects_t objects;

void create_screen_main();
void tick_screen_main();

void create_screen_logs();
void tick_screen_logs();

void create_screen_sys_info();
void tick_screen_sys_info();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/