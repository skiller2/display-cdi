#pragma once
/* Pantalla Main (spec 001): dibuja panel_model sobre los widgets de EEZ.
 * Llamar con lock LVGL tomado, después de ui_init(). */
void panel_ui_start(void);

/* Ventana de aviso modal con botón OK (setAlert de la web). */
void panel_ui_notice(const char *title, const char *text);

/* Estado desconocido (fondo gris) hasta que llegue el próximo dato de la
 * central. Llamar tras enviar Reset (igual que systemUnknown del widget). */
void panel_ui_set_unknown(void);
