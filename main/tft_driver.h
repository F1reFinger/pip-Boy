#ifndef TFT_DRIVER_H
#define TFT_DRIVER_H

#include <stdint.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"

// Display dimensions
#define TFT_WIDTH  320
#define TFT_HEIGHT 240

// Color definitions (16-bit RGB565)
#define PB_GREEN       0x07E0
#define PB_DARK_GREEN  0x03E0
#define ST77XX_BLACK   0x0000
#define ST77XX_WHITE   0xFFFF

// Function prototypes
void tft_init_driver(void);
void tft_fill_screen(uint16_t color);
void tft_draw_text(int x, int y, const char* text, int size, uint16_t color);
void tft_draw_rect(int x, int y, int w, int h, uint16_t color);
void tft_draw_line(int x0, int y0, int x1, int y1, uint16_t color);
void tft_draw_h_line(int x, int y, int w, uint16_t color);
void tft_draw_circle(int x, int y, int r, uint16_t color);
void tft_draw_filled_rect(int x, int y, int w, int h, uint16_t color);
int tft_get_text_width(const char* text, int size);

#endif