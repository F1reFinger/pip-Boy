#include <stdio.h>
#include <string.h>
#include "esp_lcd_panel_ops.h"
#include "AnimatedGIF.h"
#include "esp_heap_caps.h"

static uint16_t *line_buf = NULL;

void GIFDraw(GIFDRAW *pDraw) {
    uint8_t *s;
    uint16_t *d, *usPalette;
    int x, iWidth;
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)pDraw->pUser;

    if (line_buf == NULL) {
        line_buf = (uint16_t *)heap_caps_malloc(480 * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }

    iWidth = pDraw->iWidth;
    usPalette = pDraw->pPalette;
    s = pDraw->pPixels;
    d = line_buf;

    for (x = 0; x < iWidth; x++) {
        uint8_t c = *s++;
        if (pDraw->ucHasTransparency && c == pDraw->ucTransparent) {
            *d++ = 0x0000; 
        } else {
            uint16_t color = usPalette[c];
            // Byte swap: Necessary for ESP32 -> ST7789 communication
            *d++ = (uint16_t)((color << 8) | (color >> 8)); 
        }
    }

    esp_lcd_panel_draw_bitmap(panel, pDraw->iX, pDraw->iY + pDraw->y, 
                              pDraw->iX + iWidth, pDraw->iY + pDraw->y + 1, line_buf);
}

void play_gif(const char *path, esp_lcd_panel_handle_t panel) {
    static GIFIMAGE gif; 
    GIF_begin(&gif, GIF_PALETTE_RGB565_BE); 
    
    if (GIF_openFile(&gif, path, GIFDraw)) {
        while (GIF_playFrame(&gif, NULL, (void *)panel)) {
            // Processing frame
        }
        GIF_close(&gif);
    }
}