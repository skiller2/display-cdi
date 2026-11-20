#pragma once
/* Modelo en RAM del estado del panel CDI (spec 001).
 * Se alimenta con los JSON que llegan por UART. Sin LVGL. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PANEL_MAX_LINES   16
#define PANEL_MAX_INPUTS  16
#define PANEL_MAX_USERS   8
#define PANEL_ALIAS_LEN   32

typedef struct {
    bool alarma, falla, desconexion, tierra, test, flag_fbat;
    int  bateria;            /* % */
    bool alimentacion, red, extincion, siren;
    int  firmware;
    int  modulo_i1, modulo_i2, modulo_o1, modulo_o2;   /* -1 = no instalado */
    bool manual_mode;        /* false = automático */
    bool valid;              /* llegó al menos un barstatus */
} panel_status_t;

typedef struct {
    int  number;
    int  enable;
    int  status;
    char alias[PANEL_ALIAS_LEN];
} panel_bar_t;

typedef struct {
    int  id;
    char username[24];
    int  level;
    char code[12];
} panel_user_t;

typedef struct {
    panel_status_t status;
    panel_bar_t    lines[PANEL_MAX_LINES];
    int            n_lines;
    panel_bar_t    inputs[PANEL_MAX_INPUTS];
    int            n_inputs;
    panel_user_t   users[PANEL_MAX_USERS];
    int            n_users;
    bool           users_from_micro;   /* false = usuarios default */
    char           mac[24];            /* MAC del micro (última recibida) */
    uint32_t       last_rx_ms;         /* tick del último JSON válido */
    uint32_t       version;            /* ++ en cada feed con cambios (para redibujar) */
    uint32_t       bars_version;       /* ++ cuando llegan LINEAS o ENTRADAS */
} panel_model_t;

/* Qué partes actualizó el último feed */
enum {
    PANEL_UPD_STATUS = 1 << 0,
    PANEL_UPD_LINES  = 1 << 1,
    PANEL_UPD_INPUTS = 1 << 2,
    PANEL_UPD_USERS  = 1 << 3,
};

void panel_model_init(void);
/* Devuelve máscara PANEL_UPD_*, o -1 si el JSON es inválido. */
int panel_model_feed(const char *json, size_t len, uint32_t now_ms);
const panel_model_t *panel_model_get(void);
/* Resumen legible de la última actualización (para Logs). */
void panel_model_summary(int upd, char *buf, size_t len);
