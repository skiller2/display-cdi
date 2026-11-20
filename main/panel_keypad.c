#include <string.h>
#include "esp_log.h"
#include "lvgl.h"
#include "panel_keypad.h"
#include "panel_model.h"

static const char *TAG = "KEYPAD";

static struct {
    lv_obj_t *overlay;      /* fondo oscuro que tapa la pantalla */
    lv_obj_t *dd;           /* usuarios */
    lv_obj_t *ta;           /* código */
    int        required;
    panel_keypad_cb_t on_ok;
    void      *arg;
} k;

/* Mismo layout que la web: 1 2 3 ⌫ / 4 5 6 ↵ / 7 8 9 / 0 */
static const char *KEYS[] = {
    "1", "2", "3", LV_SYMBOL_BACKSPACE, "\n",
    "4", "5", "6", LV_SYMBOL_OK, "\n",
    "7", "8", "9", " ", "\n",
    " ", "0", " ", " ", "",
};

static void close_keypad(void)
{
    if (k.overlay) lv_obj_delete(k.overlay);
    memset(&k, 0, sizeof(k));
}

static void border_restore_cb(lv_timer_t *t)
{
    if (k.ta) {
        lv_obj_set_style_border_color(k.ta, lv_color_hex(0xfafafa), 0);
        lv_textarea_set_text(k.ta, "");
    }
    lv_timer_delete(t);
}

static void try_login(void)
{
    const panel_model_t *m = panel_model_get();
    uint32_t sel = lv_dropdown_get_selected(k.dd);
    const char *code = lv_textarea_get_text(k.ta);

    if (sel < (uint32_t)m->n_users) {
        const panel_user_t *u = &m->users[sel];
        if (strcmp(u->code, code) == 0 && u->level >= k.required) {
            ESP_LOGI(TAG, "acceso ok: %s (nivel %d)", u->username, u->level);
            panel_keypad_cb_t cb = k.on_ok;
            void *arg = k.arg;
            close_keypad();
            if (cb) cb(arg);
            return;
        }
    }
    ESP_LOGW(TAG, "código inválido o nivel insuficiente (req %d)", k.required);
    /* Error: borde rojo 250 ms y se borra el código (igual que la web) */
    lv_obj_set_style_border_color(k.ta, lv_color_hex(0xff6666), 0);
    lv_timer_create(border_restore_cb, 250, NULL);
}

static void keys_cb(lv_event_t *e)
{
    lv_obj_t *bm = lv_event_get_target(e);
    const char *txt = lv_buttonmatrix_get_button_text(bm, lv_buttonmatrix_get_selected_button(bm));
    if (!txt || txt[0] == ' ') return;
    if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) lv_textarea_delete_char(k.ta);
    else if (strcmp(txt, LV_SYMBOL_OK) == 0)   try_login();
    else                                       lv_textarea_add_text(k.ta, txt);
}

static void close_cb(lv_event_t *e)
{
    (void)e;
    close_keypad();
}

void panel_keypad_open(int required_level, panel_keypad_cb_t on_ok, void *arg)
{
    if (k.overlay) close_keypad();
    k.required = required_level;
    k.on_ok = on_ok;
    k.arg = arg;

    /* Overlay modal en la capa superior: bloquea toques a la pantalla de abajo */
    k.overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(k.overlay);
    lv_obj_set_size(k.overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(k.overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(k.overlay, LV_OPA_50, 0);
    lv_obj_add_flag(k.overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *box = lv_obj_create(k.overlay);
    lv_obj_set_size(box, 360, 440);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x303030), 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 12, 0);
    lv_obj_set_style_pad_row(box, 10, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    /* Cerrar (X) */
    lv_obj_t *x = lv_button_create(box);
    lv_obj_add_flag(x, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(x, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_size(x, 40, 40);
    lv_obj_set_style_bg_color(x, lv_color_hex(0x606060), 0);
    lv_obj_t *xl = lv_label_create(x);
    lv_label_set_text(xl, LV_SYMBOL_CLOSE);
    lv_obj_center(xl);
    lv_obj_add_event_cb(x, close_cb, LV_EVENT_CLICKED, NULL);

    /* Usuario */
    const panel_model_t *m = panel_model_get();
    k.dd = lv_dropdown_create(box);
    lv_obj_set_width(k.dd, 280);
    lv_dropdown_clear_options(k.dd);
    for (int i = 0; i < m->n_users; i++)
        lv_dropdown_add_option(k.dd, m->users[i].username, LV_DROPDOWN_POS_LAST);

    /* Código (enmascarado) */
    k.ta = lv_textarea_create(box);
    lv_obj_set_width(k.ta, LV_PCT(100));
    lv_textarea_set_one_line(k.ta, true);
    lv_textarea_set_password_mode(k.ta, true);
    lv_textarea_set_max_length(k.ta, 11);
    lv_obj_set_style_text_font(k.ta, &lv_font_montserrat_24, 0);
    lv_obj_set_style_border_width(k.ta, 2, 0);
    lv_obj_set_style_border_color(k.ta, lv_color_hex(0xfafafa), 0);
    lv_obj_remove_flag(k.ta, LV_OBJ_FLAG_CLICK_FOCUSABLE);   /* sin teclado en pantalla */

    /* Teclado numérico */
    lv_obj_t *bm = lv_buttonmatrix_create(box);
    lv_buttonmatrix_set_map(bm, KEYS);
    lv_obj_set_size(bm, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(bm, 1);
    lv_obj_set_style_text_font(bm, &lv_font_montserrat_24, 0);
    /* Huecos vacíos: ocultos e inactivos */
    for (int i = 0; i < 16; i++) {
        const char *t = lv_buttonmatrix_get_button_text(bm, i);
        if (t && t[0] == ' ')
            lv_buttonmatrix_set_button_ctrl(bm, i, LV_BUTTONMATRIX_CTRL_HIDDEN);
    }
    lv_obj_add_event_cb(bm, keys_cb, LV_EVENT_VALUE_CHANGED, NULL);
}
