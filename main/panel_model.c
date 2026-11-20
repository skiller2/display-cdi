#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "panel_model.h"

static const char *TAG = "PANEL";
static panel_model_t m;

static void set_default_users(void)
{
    m.n_users = 2;
    m.users[0] = (panel_user_t){ .id = 1, .username = "Usuario 1", .level = 1, .code = "1111" };
    m.users[1] = (panel_user_t){ .id = 2, .username = "Usuario 2", .level = 2, .code = "2222" };
    m.users_from_micro = false;
}

void panel_model_init(void)
{
    memset(&m, 0, sizeof(m));
    set_default_users();
}

const panel_model_t *panel_model_get(void) { return &m; }

static bool jbool(const cJSON *o, const char *k, bool def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v);
    if (cJSON_IsNumber(v)) return v->valueint != 0;
    return def;
}

static int jint(const cJSON *o, const char *k, int def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valueint : def;
}

static void jstr(const cJSON *o, const char *k, char *dst, size_t n)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (cJSON_IsString(v) && v->valuestring) {
        strncpy(dst, v->valuestring, n - 1);
        dst[n - 1] = '\0';
    } else {
        dst[0] = '\0';
    }
}

static void parse_status(const cJSON *b)
{
    panel_status_t *s = &m.status;
    s->alarma       = jbool(b, "ALARMA", false);
    s->falla        = jbool(b, "FALLA", false);
    s->desconexion  = jbool(b, "DESCONEXION", false);
    s->tierra       = jbool(b, "TIERRA", false);
    s->test         = jbool(b, "TEST", false);
    s->flag_fbat    = jbool(b, "FLAGFBAT", false);
    s->bateria      = jint(b, "BATERIA", 0);
    s->alimentacion = jbool(b, "ALIMENTACION", false);
    s->red          = jbool(b, "RED", false);
    s->extincion    = jbool(b, "EXTINCION", false);
    s->siren        = jbool(b, "SIREN", false);
    s->firmware     = jint(b, "FIRMWARE", 0);
    s->modulo_i1    = jint(b, "MODULO_I1", -1);
    s->modulo_i2    = jint(b, "MODULO_I2", -1);
    s->modulo_o1    = jint(b, "MODULO_O1", -1);
    s->modulo_o2    = jint(b, "MODULO_O2", -1);
    s->manual_mode  = jbool(b, "MANUALMODE", false);
    s->valid        = true;
}

static int parse_bars(const cJSON *arr, panel_bar_t *out, int max)
{
    int n = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (n >= max) break;
        panel_bar_t *b = &out[n++];
        b->number = jint(it, "number", 0);
        b->enable = jint(it, "enable", 0);
        b->status = jint(it, "status", 0);
        jstr(it, "alias", b->alias, sizeof(b->alias));
    }
    return n;
}

/* base64 -> bytes. Devuelve largo o -1. */
static int b64_decode(const char *in, unsigned char *out, size_t out_max)
{
    static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t acc = 0;
    int bits = 0;
    size_t o = 0;
    for (const char *p = in; *p; p++) {
        if (*p == '=') break;
        const char *q = strchr(tbl, *p);
        if (!q) continue;                 /* ignora \n, espacios */
        acc = (acc << 6) | (uint32_t)(q - tbl);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= out_max) return -1;
            out[o++] = (unsigned char)(acc >> bits);
        }
    }
    return (int)o;
}

static bool parse_users(const char *b64)
{
    size_t max = strlen(b64);              /* decodificado <= codificado */
    unsigned char *buf = malloc(max + 1);
    if (!buf) return false;
    int n = b64_decode(b64, buf, max);
    if (n <= 0) { free(buf); return false; }
    buf[n] = '\0';

    cJSON *arr = cJSON_Parse((const char *)buf);
    free(buf);
    if (!cJSON_IsArray(arr)) { cJSON_Delete(arr); return false; }

    panel_user_t tmp[PANEL_MAX_USERS];
    int cnt = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (cnt >= PANEL_MAX_USERS) break;
        panel_user_t *u = &tmp[cnt];
        u->id = jint(it, "id", 0);
        u->level = jint(it, "levelAccess", 0);
        jstr(it, "username", u->username, sizeof(u->username));
        /* code puede venir como string o número */
        const cJSON *c = cJSON_GetObjectItemCaseSensitive(it, "code");
        if (cJSON_IsNumber(c)) snprintf(u->code, sizeof(u->code), "%d", c->valueint);
        else jstr(it, "code", u->code, sizeof(u->code));
        cnt++;
    }
    cJSON_Delete(arr);
    if (cnt == 0) return false;
    memcpy(m.users, tmp, sizeof(tmp[0]) * cnt);
    m.n_users = cnt;
    m.users_from_micro = true;
    return true;
}

int panel_model_feed(const char *json, size_t len, uint32_t now_ms)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "JSON inválido");
        return -1;
    }

    int upd = 0;
    const cJSON *mac = cJSON_GetObjectItemCaseSensitive(root, "MAC");
    if (cJSON_IsString(mac) && mac->valuestring)
        snprintf(m.mac, sizeof(m.mac), "%s", mac->valuestring);

    const cJSON *b = cJSON_GetObjectItemCaseSensitive(root, "barstatus");
    if (cJSON_IsObject(b)) { parse_status(b); upd |= PANEL_UPD_STATUS; }

    const cJSON *l = cJSON_GetObjectItemCaseSensitive(root, "LINEAS");
    if (cJSON_IsArray(l)) { m.n_lines = parse_bars(l, m.lines, PANEL_MAX_LINES); upd |= PANEL_UPD_LINES; }

    const cJSON *i = cJSON_GetObjectItemCaseSensitive(root, "ENTRADAS");
    if (cJSON_IsArray(i)) { m.n_inputs = parse_bars(i, m.inputs, PANEL_MAX_INPUTS); upd |= PANEL_UPD_INPUTS; }

    const cJSON *u = cJSON_GetObjectItemCaseSensitive(root, "USR");
    if (cJSON_IsString(u) && u->valuestring) {
        if (parse_users(u->valuestring)) upd |= PANEL_UPD_USERS;
        else ESP_LOGW(TAG, "USR inválido");
    }

    cJSON_Delete(root);
    m.last_rx_ms = now_ms;
    if (upd) m.version++;
    if (upd & (PANEL_UPD_LINES | PANEL_UPD_INPUTS)) m.bars_version++;
    return upd;
}

static int count_enabled(const panel_bar_t *b, int n)
{
    int c = 0;
    for (int k = 0; k < n; k++) if (b[k].enable == 1) c++;
    return c;
}

void panel_model_summary(int upd, char *buf, size_t len)
{
    size_t o = 0;
    buf[0] = '\0';
#define ADD(...) do { if (o < len) o += snprintf(buf + o, len - o, __VA_ARGS__); } while (0)
    if (upd < 0) { ADD("JSON inválido"); return; }
    if (upd & PANEL_UPD_STATUS) {
        const panel_status_t *s = &m.status;
        ADD("barstatus: ALARMA=%d FALLA=%d DESC=%d TIERRA=%d TEST=%d BAT=%d%% ALIM=%d RED=%d EXT=%d %s\n",
            s->alarma, s->falla, s->desconexion, s->tierra, s->test, s->bateria,
            s->alimentacion, s->red, s->extincion, s->manual_mode ? "MANUAL" : "AUTO");
    }
    if (upd & PANEL_UPD_LINES)
        ADD("LINEAS: %d recibidas, %d habilitadas\n", m.n_lines, count_enabled(m.lines, m.n_lines));
    if (upd & PANEL_UPD_INPUTS)
        ADD("ENTRADAS: %d recibidas, %d habilitadas\n", m.n_inputs, count_enabled(m.inputs, m.n_inputs));
    if (upd & PANEL_UPD_USERS)
        ADD("USR: %d usuarios\n", m.n_users);
    if (upd == 0)
        ADD("JSON sin claves conocidas");
    if (o > 0 && o < len && buf[o - 1] == '\n') buf[o - 1] = '\0';
#undef ADD
}
