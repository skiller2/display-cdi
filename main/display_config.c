#include <stdio.h>
#include <stdlib.h>

#include "display_config.h"
#include "nvs.h"

static uint32_t inactivity_timeout_ms = DISPLAY_DEFAULT_INACTIVITY_TIMEOUT_MS;
static lv_obj_t *inactivity_ta;

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

static void inactivity_save_cb(lv_event_t *e)
{
    (void)e;
    if (!inactivity_ta) return;

    const char *txt = lv_textarea_get_text(inactivity_ta);
    char *end = NULL;
    unsigned long v = strtoul(txt ? txt : "", &end, 10);
    if (end == txt || v < 1000ul || v > 3600000ul) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)inactivity_timeout_ms);
        lv_textarea_set_text(inactivity_ta, buf);
        return;
    }

    inactivity_timeout_ms = (uint32_t)v;
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "inact_ms", inactivity_timeout_ms);
        nvs_commit(h);
        nvs_close(h);
    }
}

lv_obj_t *display_config_add_ui(lv_obj_t *parent)
{
    lv_obj_t *display_title = lv_label_create(parent);
    lv_label_set_text(display_title, "Display");
    lv_obj_set_style_text_font(display_title, &lv_font_montserrat_20, 0);

    lv_obj_t *inact_row = lv_obj_create(parent);
    lv_obj_remove_style_all(inact_row);
    lv_obj_set_width(inact_row, LV_PCT(100));
    lv_obj_set_height(inact_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(inact_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(inact_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(inact_row, 8, 0);

    lv_obj_t *inact_label = lv_label_create(inact_row);
    lv_label_set_text(inact_label, "Inactivity timeout (ms):");

    inactivity_ta = lv_textarea_create(inact_row);
    lv_textarea_set_one_line(inactivity_ta, true);
    lv_textarea_set_accepted_chars(inactivity_ta, "0123456789");
    lv_obj_set_width(inactivity_ta, 140);
    char inact_buf[16];
    snprintf(inact_buf, sizeof(inact_buf), "%lu", (unsigned long)inactivity_timeout_ms);
    lv_textarea_set_text(inactivity_ta, inact_buf);

    lv_obj_t *inact_btn = lv_button_create(inact_row);
    lv_obj_t *inact_btn_lbl = lv_label_create(inact_btn);
    lv_label_set_text(inact_btn_lbl, "Save");
    lv_obj_add_event_cb(inact_btn, inactivity_save_cb, LV_EVENT_CLICKED, NULL);

    return inactivity_ta;
}
