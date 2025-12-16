#include "tft_driver.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"

static const char *TAG = "TFT_DRIVER";

// Your pin definitions
#define TFT_MOSI GPIO_NUM_23
#define TFT_SCLK GPIO_NUM_18
#define TFT_CS   GPIO_NUM_5
#define TFT_RST  GPIO_NUM_4
#define TFT_DC   GPIO_NUM_16

static spi_device_handle_t spi;

// ST7789 commands
#define ST7789_NOP     0x00
#define ST7789_SWRESET 0x01
#define ST7789_SLPIN   0x10
#define ST7789_SLPOUT  0x11
#define ST7789_INVOFF  0x20
#define ST7789_INVON   0x21
#define ST7789_DISPOFF 0x28
#define ST7789_DISPON  0x29
#define ST7789_CASET   0x2A
#define ST7789_RASET   0x2B
#define ST7789_RAMWR   0x2C
#define ST7789_MADCTL  0x36

static void tft_write_command(uint8_t cmd) {
    gpio_set_level(TFT_DC, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(spi, &t);
}

static void tft_write_data(uint8_t data) {
    gpio_set_level(TFT_DC, 1);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &data,
    };
    spi_device_polling_transmit(spi, &t);
}

static void tft_write_data_16(uint16_t data) {
    gpio_set_level(TFT_DC, 1);
    uint8_t buffer[2] = {data >> 8, data & 0xFF};
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = buffer,
    };
    spi_device_polling_transmit(spi, &t);
}

static void tft_set_address_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    tft_write_command(ST7789_CASET);
    tft_write_data(x1 >> 8);
    tft_write_data(x1 & 0xFF);
    tft_write_data(x2 >> 8);
    tft_write_data(x2 & 0xFF);

    tft_write_command(ST7789_RASET);
    tft_write_data(y1 >> 8);
    tft_write_data(y1 & 0xFF);
    tft_write_data(y2 >> 8);
    tft_write_data(y2 & 0xFF);

    tft_write_command(ST7789_RAMWR);
}

void tft_init_driver(void) {
    ESP_LOGI(TAG, "Initializing TFT Display");

    // Configure GPIO pins
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TFT_DC) | (1ULL << TFT_RST) | (1ULL << TFT_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Reset display
    gpio_set_level(TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // SPI bus configuration
    spi_bus_config_t buscfg = {
        .mosi_io_num = TFT_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = TFT_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_WIDTH * TFT_HEIGHT * 2 + 8,
    };

    // Initialize SPI bus
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // SPI device configuration
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40 * 1000 * 1000, // 40 MHz
        .mode = 0,
        .spics_io_num = TFT_CS,
        .queue_size = 7,
        .flags = SPI_DEVICE_NO_DUMMY,
    };

    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi));

    // Initialize ST7789
    tft_write_command(ST7789_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));

    tft_write_command(ST7789_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(255));

    // Memory Data Access Control
    tft_write_command(ST7789_MADCTL);
    tft_write_data(0x08); // RGB color filter panel

    // Interface Pixel Format
    tft_write_command(0x3A);
    tft_write_data(0x55); // 16 bits per pixel

    // Display Inversion On
    tft_write_command(ST7789_INVON);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Normal Display Mode On
    tft_write_command(0x13);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Display On
    tft_write_command(ST7789_DISPON);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Clear screen
    tft_fill_screen(ST77XX_BLACK);

    ESP_LOGI(TAG, "TFT Display Initialized Successfully");
}

void tft_fill_screen(uint16_t color) {
    tft_set_address_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    
    gpio_set_level(TFT_DC, 1);
    
    // Send color data for entire screen
    uint32_t size = TFT_WIDTH * TFT_HEIGHT;
    spi_transaction_t t = {
        .length = size * 16,
        .tx_buffer = NULL,
    };
    
    // Use a simple approach - send the same color repeatedly
    uint16_t color_buffer[64]; // Small buffer
    for (int i = 0; i < 64; i++) {
        color_buffer[i] = color;
    }
    
    t.tx_buffer = color_buffer;
    t.length = 64 * 16;
    
    for (uint32_t i = 0; i < size; i += 64) {
        uint32_t chunk_size = (size - i) > 64 ? 64 : (size - i);
        if (chunk_size < 64) {
            t.length = chunk_size * 16;
        }
        spi_device_polling_transmit(spi, &t);
    }
}

void tft_draw_filled_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0 || y < 0 || x + w > TFT_WIDTH || y + h > TFT_HEIGHT) {
        return;
    }

    tft_set_address_window(x, y, x + w - 1, y + h - 1);
    
    gpio_set_level(TFT_DC, 1);
    
    uint32_t size = w * h;
    spi_transaction_t t = {
        .length = size * 16,
        .tx_buffer = NULL,
    };
    
    // Use a buffer for the color data
    uint16_t color_buffer[32];
    for (int i = 0; i < 32; i++) {
        color_buffer[i] = color;
    }
    
    t.tx_buffer = color_buffer;
    t.length = 32 * 16;
    
    for (uint32_t i = 0; i < size; i += 32) {
        uint32_t chunk_size = (size - i) > 32 ? 32 : (size - i);
        if (chunk_size < 32) {
            t.length = chunk_size * 16;
        }
        spi_device_polling_transmit(spi, &t);
    }
}

// Simple text drawing (basic implementation)
void tft_draw_text(int x, int y, const char* text, int size, uint16_t color) {
    // This is a basic implementation - draw simple blocks for text
    int char_width = 6 * size;
    int char_height = 8 * size;
    
    for (int i = 0; text[i] != '\0'; i++) {
        // Draw a simple block for each character
        tft_draw_filled_rect(x + i * char_width, y, char_width - 1, char_height, color);
    }
}

void tft_draw_rect(int x, int y, int w, int h, uint16_t color) {
    // Draw horizontal lines
    tft_draw_filled_rect(x, y, w, 1, color);
    tft_draw_filled_rect(x, y + h - 1, w, 1, color);
    
    // Draw vertical lines
    tft_draw_filled_rect(x, y, 1, h, color);
    tft_draw_filled_rect(x + w - 1, y, 1, h, color);
}

void tft_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    // Simple line drawing using filled rectangles
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        tft_draw_filled_rect(x0, y0, 1, 1, color);
        
        if (x0 == x1 && y0 == y1) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void tft_draw_h_line(int x, int y, int w, uint16_t color) {
    tft_draw_filled_rect(x, y, w, 1, color);
}

void tft_draw_circle(int x, int y, int r, uint16_t color) {
    // Simple circle drawing using filled rectangles
    for (int i = -r; i <= r; i++) {
        for (int j = -r; j <= r; j++) {
            if (i*i + j*j <= r*r) {
                tft_draw_filled_rect(x + i, y + j, 1, 1, color);
            }
        }
    }
}

int tft_get_text_width(const char* text, int size) {
    // Simple estimation - 6 pixels per character * size
    return strlen(text) * 6 * size;
}