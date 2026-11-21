#pragma once
/* Textos ES copiados de cdi/web/src/dictionary.js (spec 001).
 * Sin tildes: la fuente Montserrat de LVGL no las incluye. */

#define TXT_LINE_NAME   "Linea "
#define TXT_INPUT_NAME  "Entrada "
#define TXT_SERVER_CONN "Sin conexion con la central"

/* Barras de estado general (barstatus) en bars_list */
#define TXT_ST_ALARMA      "Alarma general"
#define TXT_ST_EXTINCION   "Extincion activada"
#define TXT_ST_SIRENA      "Sirena activada"
#define TXT_ST_FALLA       "Falla general"
#define TXT_ST_TIERRA      "Falla a tierra"
#define TXT_ST_DESCONEXION "Desconexion"
#define TXT_ST_ALIM        "Falla alimentacion"
#define TXT_ST_RED         "Sin conexion WiFi"
#define TXT_ST_BATERIA     "Falla bateria"
#define TXT_ST_MODULO      "Falla modulo %s"
#define TXT_ST_MANUAL      "Modo Manual"
#define TXT_ST_TEST        "Test activo"
#define TXT_ST_SUB         "Estado general"

#define TXT_ACK_INPUT_HDR   "Aceptacion"
#define TXT_ACK_INPUT_TXT   "Entrada de aceptacion recibida"
#define TXT_RESET_INPUT_HDR "Reset"
#define TXT_RESET_INPUT_TXT "Reset enviado correctamente"

/* statusLyE.es, índice = código de estado */
static const char *const TXT_STATUS[] = {
    "Normal", "Alarma", "Alarma", "Pre alarma", "Alarma Tecnica", "Falla",
    "Linea abierta", "Linea en corto", "Excluida", "Pulsador de Descarga Manual PDM",
    "Aceptacion", "Reset", "Aborto", "Sistema inicializado", "Silenciar sirena",
    "Bateria baja", "Bateria completa", "Falla alimentacion", "Alimentacion OK", "Robo",
    "Keep Alive", "Puerta Abierta", "Presencia", "Temperatura",
    "Baja Presion Cilindro / Red", "Corte de Suministro", "Evacuacion",
    "Modo Manual", "Test", "Modo Automatico", "Test Desactivado", "Test Activado",
};
#define TXT_STATUS_N ((int)(sizeof(TXT_STATUS) / sizeof(TXT_STATUS[0])))
