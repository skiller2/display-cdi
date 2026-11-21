#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "lvgl.h"
#include "texts_es.h"
#include "panel_model.h"
#include "panel_ui.h"
#include "ui/screens.h"

#define REFRESH_MS      200
#define CLOCK_MIN_YEAR  2024    /* hora del sistema anterior a este año = no seteada -> "--:--:--" */
#define CONN_TIMEOUT_MS 5000    /* sin JSON en este tiempo -> "Sin conexion con la central" */

static void show(lv_obj_t *o, bool on)
{
    if (!o) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* Encabezado: solo el modo AUT/MAN. Todo lo demás de barstatus (eventos,
 * batería, alimentación, WiFi, módulos) se muestra como barras en bars_list
 * cuando está en falla (refresh_status_bars). */
static void refresh_status(const panel_status_t *s)
{
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

/* Crea una barra en la lista: ícono + título + subtítulo, con los colores de lk */
static void bar_create_text(lv_obj_t *parent, const bar_look_t *lk, const char *title, const char *sub)
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
    lv_label_set_text(t, title);

    lv_obj_t *a = lv_label_create(col);
    lv_label_set_text(a, sub);
}

/* Barra de línea o entrada: "<prefijo><Linea|Entrada> <n> <estado>" + alias */
static void bar_create(lv_obj_t *parent, const char *prefix, const char *name,
                       const panel_bar_t *b, const bar_look_t *lk)
{
    char title[96];
    snprintf(title, sizeof(title), "%s%s%d %s", prefix, name, b->number,
             (lk->text >= 0 && lk->text < TXT_STATUS_N) ? TXT_STATUS[lk->text] : "");
    bar_create_text(parent, lk, title, b->alias);
}

/* ---- Estado general (barstatus) como barras -------------------------------
 * Solo los estados anormales generan barra. Van arriba de líneas y entradas,
 * en este orden (más grave primero). */
static void status_bar_add(lv_obj_t *list, bar_color_t c, const char *icon, const char *title)
{
    bar_look_t lk = { c, icon, -1, 0 };
    bar_create_text(list, &lk, title, TXT_ST_SUB);
}

static void refresh_status_bars(lv_obj_t *list, const panel_status_t *s)
{
    if (!s->valid) return;
    char buf[48];

    if (s->alarma)       status_bar_add(list, C_RED,    LV_SYMBOL_BELL,    TXT_ST_ALARMA);
    if (s->extincion)    status_bar_add(list, C_RED,    LV_SYMBOL_WARNING, TXT_ST_EXTINCION);
    if (s->siren)        status_bar_add(list, C_ORANGE, LV_SYMBOL_VOLUME_MAX, TXT_ST_SIRENA);
    if (s->falla)        status_bar_add(list, C_YELLOW, LV_SYMBOL_WARNING, TXT_ST_FALLA);
    if (s->tierra)       status_bar_add(list, C_YELLOW, LV_SYMBOL_WARNING, TXT_ST_TIERRA);
    if (s->desconexion)  status_bar_add(list, C_YELLOW, LV_SYMBOL_CLOSE,   TXT_ST_DESCONEXION);
    if (!s->alimentacion) status_bar_add(list, C_YELLOW, LV_SYMBOL_CHARGE, TXT_ST_ALIM);
    if (!s->red)         status_bar_add(list, C_YELLOW, LV_SYMBOL_WIFI,    TXT_ST_RED);
    /* FLAGFBAT = la central indica falla de batería; se muestra con el % actual */
    if (s->flag_fbat) {
        snprintf(buf, sizeof(buf), "%s %d %%", TXT_ST_BATERIA, s->bateria);
        status_bar_add(list, C_YELLOW, LV_SYMBOL_BATTERY_1, buf);
    }
    /* Módulos: -1 = no instalado (se ignora), 0 = falla (barra), 1 = normal */
    const struct { int st; const char *name; } mods[] = {
        { s->modulo_i1, "I1" }, { s->modulo_i2, "I2" }, { s->modulo_o1, "O1" }, { s->modulo_o2, "O2" },
    };
    for (int i = 0; i < 4; i++) {
        if (mods[i].st != 0) continue;
        snprintf(buf, sizeof(buf), TXT_ST_MODULO, mods[i].name);
        status_bar_add(list, C_YELLOW, LV_SYMBOL_CLOSE, buf);
    }
    if (s->manual_mode)  status_bar_add(list, C_ORANGE, LV_SYMBOL_SETTINGS, TXT_ST_MANUAL);
    if (s->test)         status_bar_add(list, C_ORANGE, LV_SYMBOL_SETTINGS, TXT_ST_TEST);
}

/* Borra todas las barras de bars_list menos fault_conn/fault_time y las vuelve a crear:
 * estado general, líneas y entradas */
static void refresh_bars(const panel_model_t *m)
{
    lv_obj_t *list = objects.bars_list;
    if (!list) return;
    for (int i = (int)lv_obj_get_child_count(list) - 1; i >= 0; i--) {
        lv_obj_t *c = lv_obj_get_child(list, i);
        if (c != objects.fault_conn && c != objects.fault_time) lv_obj_delete(c);
    }

    /* Estado general primero */
    refresh_status_bars(list, &m->status);

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

/* Reloj HH:MM:SS del encabezado de Main. Solo toca el label cuando cambia el texto. */
static void refresh_clock(void)
{
    if (!objects.main_clock) return;
    static char last[12];
    char buf[12] = "--:--:--";
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year + 1900 >= CLOCK_MIN_YEAR)
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    if (strcmp(buf, last) != 0) {
        strcpy(last, buf);
        lv_label_set_text(objects.main_clock, buf);
    }
}

static void refresh_timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh_clock();
    static uint32_t drawn = UINT32_MAX;
    const panel_model_t *m = panel_model_get();

    /* Conexión con la central: nunca recibió nada, o pasó el timeout */
    bool conn_ok = m->version > 0 && lv_tick_elaps(m->last_rx_ms) < CONN_TIMEOUT_MS;
    show(objects.fault_conn, !conn_ok);
    /* Falla de hora: solo con enlace vivo (si no, la diferencia es un dato viejo) */
    show(objects.fault_time, conn_ok && m->time_fault);

    if (m->version == drawn) return;     /* sin cambios */
    drawn = m->version;
    if (m->status.valid) refresh_status(&m->status);
    /* La lista incluye el estado general: se rearma con cualquier cambio */
    refresh_bars(m);
}

void panel_ui_start(void)
{
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
    if (objects.fault_time) {
        lv_obj_remove_flag(objects.fault_time, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_width(objects.fault_time, LV_PCT(100));
    }
    lv_timer_create(refresh_timer_cb, REFRESH_MS, NULL);
}
