#ifndef EEZ_LVGL_UI_IMAGES_H
#define EEZ_LVGL_UI_IMAGES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_img_dsc_t img_icon_check;
extern const lv_img_dsc_t img_icon_reset;
extern const lv_img_dsc_t img_icon_test;
extern const lv_img_dsc_t img_icon_config;
extern const lv_img_dsc_t img_st_battery100;
extern const lv_img_dsc_t img_st_battery25;
extern const lv_img_dsc_t img_st_battery50;
extern const lv_img_dsc_t img_st_battery75;
extern const lv_img_dsc_t img_st_batteryfault;
extern const lv_img_dsc_t img_st_bell;
extern const lv_img_dsc_t img_st_disconnect;
extern const lv_img_dsc_t img_st_extinction;
extern const lv_img_dsc_t img_st_fault;
extern const lv_img_dsc_t img_st_groundconnection;
extern const lv_img_dsc_t img_st_networkfault;
extern const lv_img_dsc_t img_st_networknormal;
extern const lv_img_dsc_t img_st_powersupplyfault;
extern const lv_img_dsc_t img_st_powersupplynormal;
extern const lv_img_dsc_t img_st_test;
extern const lv_img_dsc_t img_bar_check;
extern const lv_img_dsc_t img_bar_robbery;

#ifndef EXT_IMG_DESC_T
#define EXT_IMG_DESC_T
typedef struct _ext_img_desc_t {
    const char *name;
    const lv_img_dsc_t *img_dsc;
} ext_img_desc_t;
#endif

extern const ext_img_desc_t images[21];

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_IMAGES_H*/