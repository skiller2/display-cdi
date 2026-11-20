#pragma once
/* Pantalla Main (spec 001): dibuja panel_model sobre los widgets de EEZ.
 * Llamar con lock LVGL tomado, después de ui_init(). */
void panel_ui_start(void);

/* Ventana de aviso modal con botón OK (setAlert de la web). */
void panel_ui_notice(const char *title, const char *text);
