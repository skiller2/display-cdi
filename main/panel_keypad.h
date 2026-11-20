#pragma once
/* Teclado PIN (spec 001, etapa 7). Réplica de cdi/web Keypad.jsx.
 * Usuarios: panel_model (USR o default). Llamar con lock LVGL tomado. */

typedef void (*panel_keypad_cb_t)(void *arg);

/* Abre el teclado. Con un código válido de nivel >= required_level
 * se cierra y llama on_ok(arg). */
void panel_keypad_open(int required_level, panel_keypad_cb_t on_ok, void *arg);
