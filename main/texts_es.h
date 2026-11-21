#pragma once
/* Textos ES copiados de cdi/web/src/dictionary.js (spec 001).
 * Sin tildes: la fuente Montserrat de LVGL no las incluye. */

#define TXT_LINE_NAME   "Linea "
#define TXT_INPUT_NAME  "Entrada "
#define TXT_SERVER_CONN "Sin conexion con la central"

/* Sin barras (estado normal), como normalized-info del widget */
#define TXT_INSTALL_NAME "CDI"                  /* el widget usa cfgGeneral.NAME; default "CDI" */
#define TXT_NORMALIZED   "Equipo normalizado"

/* Barras de estado general (barstatus) en bars_list: título + texto,
 * iguales a updateStatusBarIcons() del widget (cdi/widget) */
#define TXT_ST_FALLA       "Falla"
#define TXT_ST_DESCONEXION "Desconexion"
#define TXT_ST_TIERRA      "Fuga a tierra"
#define TXT_ST_TEST        "Equipo en prueba"
#define TXT_ST_EXTINCION   "Extincion"
#define TXT_ST_BATERIA     "Bateria"
#define TXT_ST_BAT_FALLA   "Falla %d%%"
#define TXT_ST_ALIM        "Alimentacion"
#define TXT_ST_RED         "Red"
#define TXT_ST_UNKNOWN     "Desconocido"

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
