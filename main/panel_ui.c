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
    bar_color_t  color;
    const char  *icon;   /* LV_SYMBOL_* */
} bar_look_t;

/* Fondo de la pantalla Main según el peor estado (getSystemColor del widget).
 * Degradé = .cdi-main.red/.orange/.yellow/.green del CSS del widget. */
static const struct { uint32_t top, bottom; } SYS_COLORS[] = {
    [C_RED]    = { 0xe35d5b, 0xdc3545 },
    [C_ORANGE] = { 0xff9233, 0xfd7e14 },
    [C_YELLOW] = { 0xffe066, 0xf5c000 },
    [C_GREEN]  = { 0x2ecc71, 0x198754 },
};
#define SYS_COLOR_UNKNOWN 0x808080   /* .cdi-main.unknown: gris */

/* Códigos de estado válidos para líneas (ver TXT_STATUS). Otros se ignoran. */
static bool line_status_known(int st)
{
    switch (st) {
    case 2: case 3: case 4: case 6: case 7: case 8: case 19:
    case 21: case 22: case 23: case 24: case 25: case 26: return true;
    default: return false;
    }
}

/* getBarColor / getBarIcon del widget, type 'line'.
 * Íconos: bell -> BELL, fault -> WARNING, disconnect -> CLOSE, check -> OK */
static bar_look_t line_look(int st)
{
    switch (st) {
    case 2: case 4: case 19:
    case 21: case 22: case 23: case 24: case 25: case 26:
             return (bar_look_t){ C_RED,    LV_SYMBOL_BELL };
    case 3:  return (bar_look_t){ C_ORANGE, LV_SYMBOL_BELL };
    case 6: case 7:
             return (bar_look_t){ C_YELLOW, LV_SYMBOL_WARNING };
    case 8:  return (bar_look_t){ C_YELLOW, LV_SYMBOL_CLOSE };
    default: return (bar_look_t){ C_GREEN,  LV_SYMBOL_OK };
    }
}

/* Códigos de estado válidos para entradas (ver TXT_STATUS). Otros se ignoran. */
static bool input_status_known(int st)
{
    switch (st) {
    case 1: case 4: case 5: case 8: case 9: case 10: case 11: case 12: return true;
    default: return false;
    }
}

/* getBarColor / getBarIcon del widget, type 'input' */
static bar_look_t input_look(int st)
{
    switch (st) {
    case 1: case 4: case 9: case 12:
             return (bar_look_t){ C_RED,    LV_SYMBOL_BELL };
    case 5:  return (bar_look_t){ C_YELLOW, LV_SYMBOL_WARNING };   /* falla */
    case 8:  return (bar_look_t){ C_YELLOW, LV_SYMBOL_CLOSE };     /* excluida (igual que línea 8) */
    default: return (bar_look_t){ C_GREEN,  LV_SYMBOL_OK };
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

/* ---- Lista de barras (igual que updateBars() del widget) ------------------
 * Una sola lista: estado general + líneas + entradas que NO están normales
 * (status != 0) y habilitadas. Orden: rojo, naranja, amarillo, verde y,
 * dentro de cada color, por título. */
#define MAX_BARS (PANEL_MAX_LINES + PANEL_MAX_INPUTS + 16)

typedef struct {
    bar_look_t look;
    char       title[64];
    char       text[PANEL_ALIAS_LEN + 16];
} bar_item_t;

static bar_item_t s_items[MAX_BARS];   /* static: fuera del stack de LVGL */
static int        s_n_items;

static int color_prio(bar_color_t c)
{
    switch (c) {
    case C_RED:    return 1;
    case C_ORANGE: return 2;
    case C_YELLOW: return 3;
    default:       return 4;
    }
}

static int cmp_items(const void *a, const void *b)
{
    const bar_item_t *x = a, *y = b;
    int d = color_prio(x->look.color) - color_prio(y->look.color);
    return d ? d : strcmp(x->title, y->title);
}

static void add_item(bar_color_t c, const char *icon, const char *title, const char *text)
{
    if (s_n_items >= MAX_BARS) return;
    bar_item_t *it = &s_items[s_n_items++];
    it->look = (bar_look_t){ c, icon };
    snprintf(it->title, sizeof(it->title), "%s", title);
    snprintf(it->text, sizeof(it->text), "%s", text ? text : "");
}

static const char *status_text(int st)
{
    return (st >= 0 && st < TXT_STATUS_N) ? TXT_STATUS[st] : TXT_ST_UNKNOWN;
}

/* Crea una barra: ícono + título en negrita y, si hay, el texto debajo */
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

    if (sub && sub[0]) {
        lv_obj_t *a = lv_label_create(col);
        lv_label_set_text(a, sub);
    }
}

/* Estado general como barras: updateStatusBarIcons() del widget.
 * (El widget no muestra alarma ni falla general, sirena ni modo manual.) */
static void add_status_items(const panel_status_t *s)
{
    if (!s->valid) return;
    char buf[32];

    if (s->desconexion) add_item(C_YELLOW, LV_SYMBOL_CLOSE,   TXT_ST_FALLA, TXT_ST_DESCONEXION);
    if (s->tierra)      add_item(C_YELLOW, LV_SYMBOL_WARNING, TXT_ST_FALLA, TXT_ST_TIERRA);
    if (s->test)        add_item(C_GREEN,  LV_SYMBOL_SETTINGS, TXT_ST_TEST, "");
    if (s->extincion)   add_item(C_RED,    LV_SYMBOL_WARNING, TXT_ST_EXTINCION, "");
    if (s->flag_fbat) {
        snprintf(buf, sizeof(buf), TXT_ST_BAT_FALLA, s->bateria);
        add_item(C_YELLOW, LV_SYMBOL_BATTERY_EMPTY, TXT_ST_BATERIA, buf);
    }
    if (!s->alimentacion) add_item(C_YELLOW, LV_SYMBOL_CHARGE, TXT_ST_ALIM, TXT_ST_FALLA);
    if (!s->red)          add_item(C_YELLOW, LV_SYMBOL_WIFI,   TXT_ST_RED,  TXT_ST_FALLA);

    /* Módulos (solo equipo modular): -1 no instalado, 0 falla -> barra, 1 normal */
    if (s->modular) {
        if (s->modulo_i1 == 0) add_item(C_YELLOW, LV_SYMBOL_CLOSE, "M_I1", TXT_ST_DESCONEXION);
        if (s->modulo_i2 == 0) add_item(C_YELLOW, LV_SYMBOL_CLOSE, "M_I2", TXT_ST_DESCONEXION);
        if (s->modulo_o1 == 0) add_item(C_YELLOW, LV_SYMBOL_CLOSE, "M_O1", TXT_ST_DESCONEXION);
        if (s->modulo_o2 == 0) add_item(C_YELLOW, LV_SYMBOL_CLOSE, "M_O2", TXT_ST_DESCONEXION);
    }
}

/* ---- Color del sistema y estado normalizado ------------------------------ */
static bool s_reset_pending;   /* se envió Reset: gris hasta el próximo dato */

static bool system_unknown(const panel_model_t *m)
{
    return s_reset_pending || !m->status.valid;
}

/* Fondo de Main: gris si desconocido; si no, el color de la peor barra
 * (rojo > naranja > amarillo > verde); sin barras = verde. */
static void apply_system_color(const panel_model_t *m)
{
    if (!objects.main) return;
    uint32_t top = SYS_COLOR_UNKNOWN, bottom = SYS_COLOR_UNKNOWN;
    if (!system_unknown(m)) {
        bar_color_t worst = C_GREEN;
        for (int i = 0; i < s_n_items; i++)
            if (color_prio(s_items[i].look.color) < color_prio(worst)) worst = s_items[i].look.color;
        top = SYS_COLORS[worst].top;
        bottom = SYS_COLORS[worst].bottom;
    }
    lv_obj_set_style_bg_color(objects.main, lv_color_hex(top), 0);
    lv_obj_set_style_bg_grad_color(objects.main, lv_color_hex(bottom), 0);
    lv_obj_set_style_bg_grad_dir(objects.main, LV_GRAD_DIR_VER, 0);
}

/* Sin barras: nombre de la instalación y "Equipo normalizado" centrados
 * (normalized-info del widget; el texto solo si el estado es conocido). */
static void normalized_create(lv_obj_t *list, bool unknown)
{
    lv_obj_t *box = lv_obj_create(list);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_PCT(100), 300);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 6, 0);
    lv_obj_add_flag(box, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *n = lv_label_create(box);
    lv_obj_set_style_text_font(n, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(n, lv_color_white(), 0);
    lv_label_set_text(n, TXT_INSTALL_NAME);

    if (!unknown) {
        lv_obj_t *t = lv_label_create(box);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0xbadbcc), 0);
        lv_label_set_text(t, TXT_NORMALIZED);
    }
}

void panel_ui_set_unknown(void)
{
    s_reset_pending = true;
    const panel_model_t *m = panel_model_get();
    apply_system_color(m);
    if (s_n_items == 0 && objects.bars_list) {
        /* rearmar la vista normalizada sin el texto "Equipo normalizado" */
        for (int i = (int)lv_obj_get_child_count(objects.bars_list) - 1; i >= 0; i--) {
            lv_obj_t *c = lv_obj_get_child(objects.bars_list, i);
            if (c != objects.fault_conn && c != objects.fault_time) lv_obj_delete(c);
        }
        normalized_create(objects.bars_list, true);
    }
}

/* Borra todas las barras de bars_list menos fault_conn/fault_time y las vuelve a crear */
static void refresh_bars(const panel_model_t *m)
{
    lv_obj_t *list = objects.bars_list;
    if (!list) return;
    for (int i = (int)lv_obj_get_child_count(list) - 1; i >= 0; i--) {
        lv_obj_t *c = lv_obj_get_child(list, i);
        if (c != objects.fault_conn && c != objects.fault_time) lv_obj_delete(c);
    }

    char title[64];
    s_n_items = 0;
    add_status_items(&m->status);

    /* Líneas: "Linea <n> <estado>" + alias */
    for (int i = 0; i < m->n_lines; i++) {
        const panel_bar_t *b = &m->lines[i];
        if (b->enable != 1 || !line_status_known(b->status)) continue;    /* 0 o código desconocido */
        bar_look_t lk = line_look(b->status);
        snprintf(title, sizeof(title), "%s%d %s", TXT_LINE_NAME, b->number, status_text(b->status));
        add_item(lk.color, lk.icon, title, b->alias);
    }

    /* Entradas: modular -> "M_I<mod> Entrada <1..7> <estado>"; si no "Entrada <n> <estado>" */
    for (int i = 0; i < m->n_inputs; i++) {
        const panel_bar_t *b = &m->inputs[i];
        if (b->enable != 1 || !input_status_known(b->status)) continue;   /* 0 o código desconocido */
        bar_look_t lk = input_look(b->status);
        if (m->status.modular && b->number >= 1) {
            int mod = (b->number + 6) / 7;            /* Math.ceil(n / 7) */
            int num = ((b->number - 1) % 7) + 1;
            snprintf(title, sizeof(title), "M_I%d %s%d %s", mod, TXT_INPUT_NAME, num, status_text(b->status));
        } else {
            snprintf(title, sizeof(title), "%s%d %s", TXT_INPUT_NAME, b->number, status_text(b->status));
        }
        add_item(lk.color, lk.icon, title, b->alias);
    }

    qsort(s_items, s_n_items, sizeof(s_items[0]), cmp_items);
    for (int i = 0; i < s_n_items; i++)
        bar_create_text(list, &s_items[i].look, s_items[i].title, s_items[i].text);
    if (s_n_items == 0) normalized_create(list, system_unknown(m));
    apply_system_color(m);

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
    s_reset_pending = false;             /* llegó dato nuevo tras un Reset */
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
