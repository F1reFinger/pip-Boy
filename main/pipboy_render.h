#ifndef PIPBOY_RENDER_H
#define PIPBOY_RENDER_H

#include <stdint.h>
#include <stdbool.h>

// Function declarations
void tft_render_task(void *pvParameter);
void draw_clock(void);
void draw_splash_screen(void);
void draw_full_menu(void);
void draw_wifi_sub_menu(bool initial_draw);
void show_audio_demo(bool running);
void draw_shutdown_sequence(bool is_final);

#endif