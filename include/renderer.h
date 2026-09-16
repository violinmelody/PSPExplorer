#ifndef PSPEXPLORER_RENDERER_H
#define PSPEXPLORER_RENDERER_H
#include "explorer.h"

void renderer_init(void);
void renderer_shutdown(void);
void renderer_begin(int hue);
void renderer_end(void);
void renderer_explorer(const Pane *p, int active, int pane_index, const char *status);
void renderer_header(const char *title, const char *subtitle);
void renderer_menu(const char *title, const char *const *items, int count, int selected);
void renderer_hue(int hue);
void renderer_text_editor(const char *name, const char *buf, int cursor, int key_row, int key_col, int caps);
void renderer_name_prompt(const char *title, const char *value, int key_row, int key_col, int caps, int folder_rules);
void renderer_confirm(const char *title, const char *message, const char *detail, int selected);
void renderer_image(const char *name, const unsigned int *pixels, int w, int h, float zoom, int pan_x, int pan_y);
void renderer_about(void);
void renderer_audio(const char *filename, const char *artist, const char *title, const float *waveform, int points, int position_ms, int duration_ms, int playing, const unsigned int *cover, int cover_w, int cover_h, int selected_button);

#endif
