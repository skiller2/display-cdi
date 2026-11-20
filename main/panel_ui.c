#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "texts_es.h"
#include "panel_model.h"
#include "panel_ui.h"
#include "ui/screens.h"

#define REFRESH_MS      200
#define CONN_TIMEOUT_MS 10000   /* sin JSON en este tiempo -> "Sin conexion con el micro" */

static void show(lv_obj_t *o, bool on)
{
    if (!o) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* Íconos = símbolos de LVGL (Montserrat 32) en labels; color por estado */
#define COL_OK   0x4caf50
#define COL_BAD  0xdc3545

static void set_sym(lv_obj_t *o, const char *sym, uint32_t color)
{
    if (!o) return;
    if (strcmp(lv_label_get_text(o), sym) != 0) lv_label_set_text(o, sym);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
}

static void battery_sym(int pct, const char **sym, uint32_t *col)
{
    /* mismos rangos que NavBar.jsx */
    if (pct == 100)           { *sym = LV_SYMBOL_BATTERY_FULL;  *col = COL_OK; }
    else if (pct >= 75)       { *sym = LV_SYMBOL_BATTERY_3;     *col = COL_OK; }
    else if (pct >= 50)       { *sym = LV_SYMBOL_BATTERY_2;     *col = 0xe0a800; }
    else if (pct > 1 && pct <= 25) { *sym = LV_SYMBOL_BATTERY_1; *col = 0xfd7e14; }
    else                      { *sym = LV_SYMBOL_BATTERY_EMPTY; *col = COL_BAD; }
}

static void module_dot(lv_obj_t *o, int st)
{
    if (!o) return;
    uint32_t c = st == 1 ? 0x4CAF50 : st == 0 ? 0xffc107 : 0x6c757d;  /* conectado / desconectado / desconocido */
    lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
}

static void refresh_status(const panel_status_t *s)
{
    show(objects.st_alarma,      s->alarma);
    show(objects.st_falla,       s->falla);
    show(objects.st_desconexion, s->desconexion);
    show(objects.st_tierra,      s->tierra);
    show(objects.st_test,        s->test);
    show(objects.st_extincion,   s->extincion);

    const char *bsym; uint32_t bcol;
    battery_sym(s->bateria, &bsym, &bcol);
    set_sym(objects.st_bateria,      bsym, bcol);
    set_sym(objects.st_alimentacion, LV_SYMBOL_CHARGE, s->alimentacion ? COL_OK : COL_BAD);
    set_sym(objects.st_red,          LV_SYMBOL_WIFI,   s->red ? COL_OK : COL_BAD);

    module_dot(objects.st_mod_i1, s->modulo_i1);
    module_dot(objects.st_mod_i2, s->modulo_i2);
    module_dot(objects.st_mod_o1, s->modulo_o1);
    module_dot(objects.st_mod_o2, s->modulo_o2);

    if (objects.st_autman) lv_label_set_text(objects.st_autman, s->manual_mode ? "MAN" : "AUT");
    if (objects.st_autman_box)
        lv_obj_set_style_bg_color(objects.st_autman_box,
                                  lv_color_hex(s->manual_mode ? 0xef4444 : 0x22c55e), 0);
}

/* ---------------------------------------------------------- barras ---
 * Colores = clases .bar.green/.red/.orange/.yellow de la web (index.css) */
typedef enum { C_GREEN, C_RED, C_ORANGE, C_YELLOW } bar_color_t;
static const struct { uint32_t bg, border, text; } BAR_COLORS[] = {
    [C_GREEN]  = { 0xd1e7dd, 0xbadbcc, 0x0f5132 },
    [C_RED]    = { 0xf8d7da, 0xf5c6cb, 0x721c24 },
    [C_ORANGE] = { 0xfcd6a4, 0xfbc784, 0x7d4925 },
    [C_YELLOW] = { 0xfff3cd, 0xffeeba, 0x856404 },
};

typedef struct {
    bar_color_t           color;
    const char           *icon;   /* LV_SYMBOL_* */
    int                   text;   /* índice en TXT_STATUS */
    int                   rank;   /* orden: menor = más arriba */
} bar_look_t;

/* LineBar.jsx (type line). Los 19..26 muestran "Alarma" igual que la web. */
static bar_look_t line_look(int st)
{
    switch (st) {
    case 2:  return (bar_look_t){ C_RED,    LV_SYMBOL_BELL,       2, 0 };
    case 19: return (bar_look_t){ C_RED,    LV_SYMBOL_EYE_OPEN,   2, 0 };
    case 3:  return (bar_look_t){ C_ORANGE, LV_SYMBOL_BELL,       3, 1 };
    case 4:  return (bar_look_t){ C_RED,    LV_SYMBOL_BELL,       4, 2 };
    case 21: case 22: case 23: case 24: case 25: case 26:
             return (bar_look_t){ C_YELLOW, LV_SYMBOL_BELL,       2, 3 };
    case 6:  return (bar_look_t){ C_YELLOW, LV_SYMBOL_WARNING,      6, 4 };
    case 7:  return (bar_look_t){ C_YELLOW, LV_SYMBOL_WARNING,      7, 5 };
    case 8:  return (bar_look_t){ C_YELLOW, LV_SYMBOL_CLOSE, 8, 6 };
    default: return (bar_look_t){ C_GREEN,  LV_SYMBOL_OK,     0, 7 };
    }
}

/* LineBar.jsx (type input). Orden = customOrder 12,9,1,4,5,0 (+8 tras 5).
 * 10/11 no dibujan barra: muestran aviso (rank -1). */
static bar_look_t input_look(int st)
{
    switch (st) {
    case 12: return (bar_look_t){ C_RED,    LV_SYMBOL_BELL,  12, 0 };
    case 9:  return (bar_look_t){ C_RED,    LV_SYMBOL_BELL,   9, 1 };
    case 1:  return (bar_look_t){ C_RED,    LV_SYMBOL_BELL,   1, 2 };
    case 4:  return (bar_look_t){ C_RED,    LV_SYMBOL_BELL,   4, 3 };
    case 5:  return (bar_look_t){ C_YELLOW, LV_SYMBOL_WARNING,  5, 4 };
    case 8:  return (bar_look_t){ C_YELLOW, LV_SYMBOL_WARNING,  8, 5 };
    case 10: case 11:
             return (bar_look_t){ C_GREEN,  NULL,          st, -1 };
    default: return (bar_look_t){ C_GREEN,  LV_SYMBOL_OK, 0, 6 };
    }
}

/* Ventana de aviso (equivale a setAlert de la web): título, texto y botón OK */
static void notice_ok_cb(lv_event_t *e)
{
    lv_obj_delete((lv_obj_t *)lv_event_get_user_data(e));
}

void panel_ui_notice(const char *title, const char *text)
{
    lv_obj_t *ov = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ov, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_50, 0);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *box = lv_obj_create(ov);
    lv_obj_set_size(box, 420, LV_SIZE_CONTENT);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(0xf8f9fa), 0);
    lv_obj_set_style_border_color(box, lv_color_black(), 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_pad_all(box, 16, 0);
    lv_obj_set_style_pad_row(box, 12, 0);
    lv_obj_set_style_text_color(box, lv_color_black(), 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(box);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_24, 0);
    lv_label_set_text(t, title);

    lv_obj_t *c = lv_label_create(box);
    lv_obj_set_width(c, LV_PCT(100));
    lv_obj_set_style_text_font(c, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(c, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(c, LV_LABEL_LONG_WRAP);
    lv_label_set_text(c, text);

    lv_obj_t *ok = lv_button_create(box);
    lv_obj_set_size(ok, 140, 50);
    lv_obj_set_style_bg_color(ok, lv_color_hex(0xf8f9fa), 0);
    lv_obj_set_style_border_color(ok, lv_color_black(), 0);
    lv_obj_set_style_border_width(ok, 2, 0);
    lv_obj_set_style_radius(ok, 8, 0);
    lv_obj_set_style_shadow_width(ok, 0, 0);
    lv_obj_t *ol = lv_label_create(ok);
    lv_obj_set_style_text_font(ol, &lv_font_montserrat_20, 0);
    lv_label_set_text(ol, "OK");
    lv_obj_center(ol);
    lv_obj_add_event_cb(ok, notice_ok_cb, LV_EVENT_CLICKED, ov);
}

/* Avisa una sola vez cuando una entrada PASA a 10 (ACK) o 11 (reset) */
static void check_input_notices(const panel_model_t *m)
{
    static int last[PANEL_MAX_INPUTS + 1];   /* por número de entrada */
    static bool first = true;
    for (int i = 0; i < m->n_inputs; i++) {
        int n = m->inputs[i].number, st = m->inputs[i].status;
        if (n < 0 || n > PANEL_MAX_INPUTS) continue;
        if (!first && st != last[n]) {
            if (st == 10) panel_ui_notice(TXT_ACK_INPUT_HDR, TXT_ACK_INPUT_TXT);
            else if (st == 11) panel_ui_notice(TXT_RESET_INPUT_HDR, TXT_RESET_INPUT_TXT);
        }
        last[n] = st;
    }
    first = false;
}

typedef struct { const panel_bar_t *b; bar_look_t look; } bar_item_t;

static int cmp_items(const void *a, const void *b)
{
    const bar_item_t *x = a, *y = b;
    if (x->look.rank != y->look.rank) return x->look.rank - y->look.rank;
    return x->b->number - y->b->number;
}

static void bar_create(lv_obj_t *parent, const char *prefix, const char *name,
                       const panel_bar_t *b, const bar_look_t *lk)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(bar, lv_color_hex(BAR_COLORS[lk->color].bg), 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(BAR_COLORS[lk->color].border), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_radius(bar, 4, 0);
    lv_obj_set_style_pad_all(bar, 8, 0);
    lv_obj_set_style_pad_column(bar, 8, 0);
    lv_obj_set_style_text_color(bar, lv_color_hex(BAR_COLORS[lk->color].text), 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_EVENT_BUBBLE);   /* scroll de la lista */

    lv_obj_t *ic = lv_label_create(bar);
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_32, 0);
    lv_obj_set_width(ic, 44);
    lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(ic, lk->icon);

    lv_obj_t *col = lv_obj_create(bar);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(col, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *t = lv_label_create(col);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_label_set_text_fmt(t, "%s%s%d %s", prefix, name, b->number,
                          (lk->text >= 0 && lk->text < TXT_STATUS_N) ? TXT_STATUS[lk->text] : "");

    lv_obj_t *a = lv_label_create(col);
    lv_label_set_text(a, b->alias);
}

/* Borra todas las barras de bars_list menos fault_conn y las vuelve a crear */
static void refresh_bars(const panel_model_t *m)
{
    lv_obj_t *list = objects.bars_list;
    if (!list) return;
    for (int i = (int)lv_obj_get_child_count(list) - 1; i >= 0; i--) {
        lv_obj_t *c = lv_obj_get_child(list, i);
        if (c != objects.fault_conn) lv_obj_delete(c);
    }

    bar_item_t items[PANEL_MAX_LINES > PANEL_MAX_INPUTS ? PANEL_MAX_LINES : PANEL_MAX_INPUTS];

    /* Líneas */
    int n = 0;
    for (int i = 0; i < m->n_lines; i++) {
        if (m->lines[i].enable != 1) continue;
        items[n].b = &m->lines[i];
        items[n].look = line_look(m->lines[i].status);
        n++;
    }
    qsort(items, n, sizeof(items[0]), cmp_items);
    for (int i = 0; i < n; i++)
        bar_create(list, "", TXT_LINE_NAME, items[i].b, &items[i].look);

    /* Entradas, debajo de las líneas. Prefijo de módulo: 1-7 M_I1, 8-14 M_I2 */
    n = 0;
    for (int i = 0; i < m->n_inputs; i++) {
        if (m->inputs[i].enable != 1) continue;
        bar_look_t lk = input_look(m->inputs[i].status);
        if (lk.rank < 0) continue;            /* 10/11: solo aviso */
        items[n].b = &m->inputs[i];
        items[n].look = lk;
        n++;
    }
    qsort(items, n, sizeof(items[0]), cmp_items);
    for (int i = 0; i < n; i++) {
        int num = items[i].b->number;
        const char *pre = (num >= 1 && num <= 7) ? "M_I1 - " : (num >= 8 && num <= 14) ? "M_I2 - " : "";
        bar_create(list, pre, TXT_INPUT_NAME, items[i].b, &items[i].look);
    }

    check_input_notices(m);
}

static void refresh_timer_cb(lv_timer_t *t)
{
    (void)t;
    static uint32_t drawn = UINT32_MAX;
    const panel_model_t *m = panel_model_get();

    /* Conexión con el micro: nunca recibió nada, o pasó el timeout */
    bool conn_ok = m->version > 0 && lv_tick_elaps(m->last_rx_ms) < CONN_TIMEOUT_MS;
    show(objects.fault_conn, !conn_ok);

    if (m->version == drawn) return;     /* sin cambios */
    drawn = m->version;
    if (m->status.valid) refresh_status(&m->status);

    static uint32_t bars_drawn = 0;
    if (m->bars_version != bars_drawn) {
        bars_drawn = m->bars_version;
        refresh_bars(m);
    }
}

void panel_ui_start(void)
{
    /* Íconos de alarma/falla/… a la izquierda: los ocultos no ocupan lugar */
    if (objects.st_left) {
        lv_obj_set_flex_flow(objects.st_left, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(objects.st_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(objects.st_left, 8, 0);
        lv_obj_remove_flag(objects.st_left, LV_OBJ_FLAG_SCROLLABLE);
    }
    if (objects.status_bar) lv_obj_remove_flag(objects.status_bar, LV_OBJ_FLAG_SCROLLABLE);
    /* Lista de barras (falla de conexión + líneas + entradas): columna con scroll */
    if (objects.bars_list) {
        lv_obj_set_flex_flow(objects.bars_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(objects.bars_list, 8, 0);
        lv_obj_add_flag(objects.bars_list, LV_OBJ_FLAG_SCROLLABLE);  /* EEZ lo crea sin SCROLLABLE */
        lv_obj_set_scroll_dir(objects.bars_list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(objects.bars_list, LV_SCROLLBAR_MODE_AUTO);
    }
    if (objects.fault_conn) {
        lv_obj_remove_flag(objects.fault_conn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_width(objects.fault_conn, LV_PCT(100));
    }
    lv_timer_create(refresh_timer_cb, REFRESH_MS, NULL);
}
