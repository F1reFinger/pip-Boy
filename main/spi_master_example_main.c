#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "dirent.h"

// ===== DISPLAY PINS FOR FREENOVE ESP32-S3 =====
#define TFT_CS     5    // GPIO5
#define TFT_RST    4    // GPIO4
#define TFT_DC     16   // GPIO16
#define TFT_MOSI   11   // GPIO11 - SPI2 MOSI
#define TFT_SCLK   12   // GPIO12 - SPI2 SCLK

// ===== ROTARY ENCODER PINS =====
#define ENC_CLK    13   // GPIO13
#define ENC_DT     14   // GPIO14
#define ENC_SW     15   // GPIO15

// ===== SD CARD PINS (Built-in on Freenove ESP32-S3) =====
#define SD_CLK     39   // GPIO39 - SD_CLK
#define SD_CMD     38   // GPIO38 - SD_CMD
#define SD_D0      40   // GPIO40 - SD_DATA

// ===== DISPLAY DIMENSIONS =====
// After 90° rotation: 240x320 becomes 320x240
#define DISPLAY_WIDTH   320   // Swapped for 90° rotation
#define DISPLAY_HEIGHT  240   // Swapped for 90° rotation

// ===== COLOR DEFINITIONS (RGB565) =====
#define COLOR_BLACK         0x0000
#define COLOR_CLASSIC_GREEN 0x07E0
#define COLOR_CYAN          0x07FF
#define COLOR_ORANGE        0xFC00
#define COLOR_LIGHT_BLUE    0xAFE5
#define COLOR_WHITE         0xFFFF
#define COLOR_RED           0xF800
#define COLOR_GREEN         0x07E0
#define COLOR_BLUE          0x001F

// ===== ST7789 COMMANDS =====
#define ST7789_SWRESET 0x01
#define ST7789_SLPOUT  0x11
#define ST7789_COLMOD  0x3A
#define ST7789_MADCTL  0x36
#define ST7789_CASET   0x2A
#define ST7789_RASET   0x2B
#define ST7789_RAMWR   0x2C
#define ST7789_INVON   0x21
#define ST7789_NORON   0x13
#define ST7789_DISPON  0x29

// ===== GLOBAL VARIABLES =====
static spi_device_handle_t spi_handle;

// ===== COLOR PALETTE =====
typedef struct {
    uint16_t color;
    char name[20];
} ColorPalette;

const ColorPalette palettes[] = {
    {COLOR_CLASSIC_GREEN, "CLASSIC GREEN"},
    {COLOR_CYAN, "CYAN"},
    {COLOR_ORANGE, "ORANGE"},
    {COLOR_LIGHT_BLUE, "LIGHT BLUE"},
    {COLOR_WHITE, "WHITE"}
};

const int num_palettes = sizeof(palettes) / sizeof(palettes[0]);
static int current_palette_index = 0;
static uint16_t ui_color = COLOR_CLASSIC_GREEN;
static uint16_t bg_color = COLOR_BLACK;

// ===== MENU SYSTEM =====
static int current_menu_index = 0;
static const char* menu_items[] = {"1. THEME", "2. AUDIO", "3. IMAGE"};
static const int menu_size = 3;
static bool is_demo_active = false;

// ===== ENCODER STATE =====
static volatile int32_t encoder_count = 0;
static volatile bool button_pressed = false;
static volatile uint32_t last_encoder_time = 0;
static volatile uint32_t last_button_time = 0;
static portMUX_TYPE encoder_mux = portMUX_INITIALIZER_UNLOCKED;

// ===== SIMPLE 5x7 FONT =====
static const uint8_t font_5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // Space
    {0x00,0x00,0x5F,0x00,0x00}, // !
    {0x00,0x07,0x00,0x07,0x00}, // "
    {0x14,0x7F,0x14,0x7F,0x14}, // #
    {0x24,0x2A,0x7F,0x2A,0x12}, // $
    {0x23,0x13,0x08,0x64,0x62}, // %
    {0x36,0x49,0x55,0x22,0x50}, // &
    {0x00,0x05,0x03,0x00,0x00}, // '
    {0x00,0x1C,0x22,0x41,0x00}, // (
    {0x00,0x41,0x22,0x1C,0x00}, // )
    {0x14,0x08,0x3E,0x08,0x14}, // *
    {0x08,0x08,0x3E,0x08,0x08}, // +
    {0x00,0x50,0x30,0x00,0x00}, // ,
    {0x08,0x08,0x08,0x08,0x08}, // -
    {0x00,0x60,0x60,0x00,0x00}, // .
    {0x20,0x10,0x08,0x04,0x02}, // /
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    {0x00,0x36,0x36,0x00,0x00}, // :
    {0x00,0x56,0x36,0x00,0x00}, // ;
    {0x08,0x14,0x22,0x41,0x00}, // <
    {0x14,0x14,0x14,0x14,0x14}, // =
    {0x00,0x41,0x22,0x14,0x08}, // >
    {0x02,0x01,0x51,0x09,0x06}, // ?
    {0x32,0x49,0x79,0x41,0x3E}, // @
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
    {0x00,0x7F,0x41,0x41,0x00}, // [
    {0x02,0x04,0x08,0x10,0x20}, // backslash
    {0x00,0x41,0x41,0x7F,0x00}, // ]
    {0x04,0x02,0x01,0x02,0x04}, // ^
    {0x40,0x40,0x40,0x40,0x40}, // _
};

// ===== FUNCTION DECLARATIONS =====
static void tft_command(uint8_t cmd);
static void tft_data(uint8_t *data, int len);
static void tft_set_addr_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
static void tft_fill_screen(uint16_t color);
static void tft_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
static void tft_draw_h_line(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
static void tft_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
static void tft_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
static void tft_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color);
static void tft_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color);
static bool display_image(const char *filename);
static void tft_init(void);
static void encoder_init(void);
static void apply_palette(void);
static void draw_please_stand_by(void);
static void draw_full_menu(int selected_index);
static void show_menu_content(int index);
static void update_menu_selection(int old_index, int new_index);
static void show_audio_demo(void);
static bool init_sd_card(void);

// ===== DISPLAY FUNCTIONS =====
static void tft_command(uint8_t cmd) {
    gpio_set_level(TFT_DC, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(spi_handle, &t);
}

static void tft_data(uint8_t *data, int len) {
    gpio_set_level(TFT_DC, 1);
    if (len <= 0) return;

    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(spi_handle, &t);
}

static void tft_set_addr_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    uint8_t data[4];

    tft_command(ST7789_CASET);
    data[0] = x1 >> 8;
    data[1] = x1 & 0xFF;
    data[2] = x2 >> 8;
    data[3] = x2 & 0xFF;
    tft_data(data, 4);

    tft_command(ST7789_RASET);
    data[0] = y1 >> 8;
    data[1] = y1 & 0xFF;
    data[2] = y2 >> 8;
    data[3] = y2 & 0xFF;
    tft_data(data, 4);

    tft_command(ST7789_RAMWR);
}

static void tft_fill_screen(uint16_t color) {
    tft_set_addr_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);

    gpio_set_level(TFT_DC, 1);

    uint8_t color_high = color >> 8;
    uint8_t color_low = color & 0xFF;

    // Fill screen in chunks
    uint8_t buffer[128];
    for (int i = 0; i < 128; i += 2) {
        buffer[i] = color_high;
        buffer[i + 1] = color_low;
    }

    int total_pixels = DISPLAY_WIDTH * DISPLAY_HEIGHT;
    int pixels_sent = 0;

    while (pixels_sent < total_pixels) {
        int pixels_to_send = 64;
        if (pixels_sent + pixels_to_send > total_pixels) {
            pixels_to_send = total_pixels - pixels_sent;
        }

        spi_transaction_t t = {
            .length = pixels_to_send * 16,
            .tx_buffer = buffer,
        };
        spi_device_polling_transmit(spi_handle, &t);

        pixels_sent += pixels_to_send;
    }
}

static void tft_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) return;
    if (x + w > DISPLAY_WIDTH) w = DISPLAY_WIDTH - x;
    if (y + h > DISPLAY_HEIGHT) h = DISPLAY_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    tft_set_addr_window(x, y, x + w - 1, y + h - 1);
    gpio_set_level(TFT_DC, 1);

    uint8_t color_high = color >> 8;
    uint8_t color_low = color & 0xFF;

    // Fill rectangle
    for (int i = 0; i < w * h; i++) {
        uint8_t data[2] = {color_high, color_low};
        tft_data(data, 2);
    }
}

static void tft_draw_h_line(uint16_t x, uint16_t y, uint16_t w, uint16_t color) {
    if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) return;
    if (x + w > DISPLAY_WIDTH) w = DISPLAY_WIDTH - x;
    if (w <= 0) return;

    tft_set_addr_window(x, y, x + w - 1, y);
    gpio_set_level(TFT_DC, 1);

    uint8_t color_high = color >> 8;
    uint8_t color_low = color & 0xFF;

    for (int i = 0; i < w; i++) {
        uint8_t data[2] = {color_high, color_low};
        tft_data(data, 2);
    }
}

static void tft_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    // Top line
    tft_draw_h_line(x, y, w, color);
    // Bottom line
    tft_draw_h_line(x, y + h - 1, w, color);
    
    // Left and right lines
    for (int i = 0; i < h; i++) {
        tft_set_addr_window(x, y + i, x, y + i);
        gpio_set_level(TFT_DC, 1);
        uint8_t data[2] = {color >> 8, color & 0xFF};
        tft_data(data, 2);
        
        tft_set_addr_window(x + w - 1, y + i, x + w - 1, y + i);
        tft_data(data, 2);
    }
}

static void tft_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    int16_t steep = abs(y1 - y0) > abs(x1 - x0);
    if (steep) {
        // Swap x and y
        uint16_t temp = x0; x0 = y0; y0 = temp;
        temp = x1; x1 = y1; y1 = temp;
    }
    
    if (x0 > x1) {
        uint16_t temp = x0; x0 = x1; x1 = temp;
        temp = y0; y0 = y1; y1 = temp;
    }
    
    int16_t dx = x1 - x0;
    int16_t dy = abs(y1 - y0);
    int16_t err = dx / 2;
    int16_t ystep = (y0 < y1) ? 1 : -1;
    
    for (; x0 <= x1; x0++) {
        if (steep) {
            tft_set_addr_window(y0, x0, y0, x0);
        } else {
            tft_set_addr_window(x0, y0, x0, y0);
        }
        gpio_set_level(TFT_DC, 1);
        uint8_t data[2] = {color >> 8, color & 0xFF};
        tft_data(data, 2);
        
        err -= dy;
        if (err < 0) {
            y0 += ystep;
            err += dx;
        }
    }
}

static void tft_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color) {
    if (c < 32 || c > 126) return;
    
    int font_index = c - 32;
    
    for (int col = 0; col < 5; col++) {
        uint8_t line = font_5x7[font_index][col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                tft_set_addr_window(x + col, y + row, x + col, y + row);
                gpio_set_level(TFT_DC, 1);
                uint8_t data[2] = {color >> 8, color & 0xFF};
                tft_data(data, 2);
            } else if (bg_color != color) {
                tft_set_addr_window(x + col, y + row, x + col, y + row);
                gpio_set_level(TFT_DC, 1);
                uint8_t data[2] = {bg_color >> 8, bg_color & 0xFF};
                tft_data(data, 2);
            }
        }
    }
}

static void tft_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color) {
    uint16_t start_x = x;
    while (*str) {
        if (*str == '\n') {
            x = start_x;
            y += 8;
        } else {
            tft_draw_char(x, y, *str, color, bg_color);
            x += 6;
            if (x + 6 > DISPLAY_WIDTH) {
                x = start_x;
                y += 8;
            }
        }
        str++;
    }
}

// ===== IMAGE DISPLAY FUNCTION =====
static bool display_image(const char *filename) {
    printf("Displaying image: %s\n", filename);

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("Failed to open file: %s\n", filename);
        return false;
    }

    printf("File opened successfully\n");

    // For 90° rotated display, we need to use the rotated dimensions
    tft_set_addr_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
    gpio_set_level(TFT_DC, 1);

    // Read and send image data
    uint8_t buffer[512];
    size_t bytes_read;
    size_t total_bytes = 0;
    size_t expected_bytes = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2;

    printf("Sending image data to display");

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        spi_transaction_t t = {
            .length = bytes_read * 8,
            .tx_buffer = buffer,
        };
        spi_device_polling_transmit(spi_handle, &t);

        total_bytes += bytes_read;
        printf(".");
        fflush(stdout);

        if (total_bytes >= expected_bytes) {
            break;
        }
    }

    printf("\n");

    fclose(fp);

    printf("Total bytes sent: %zu\n", total_bytes);

    if (total_bytes < expected_bytes) {
        printf("Warning: File is smaller than expected\n");
        return false;
    }

    printf("Image displayed successfully!\n");
    return true;
}

// ===== DISPLAY INITIALIZATION =====
static void tft_init() {
    printf("Initializing display...\n");

    gpio_set_direction(TFT_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction(TFT_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(TFT_CS, GPIO_MODE_OUTPUT);

    gpio_set_level(TFT_CS, 1);

    printf("Resetting display...\n");
    gpio_set_level(TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    spi_bus_config_t buscfg = {
        .mosi_io_num = TFT_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = TFT_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        printf("SPI bus init failed: 0x%x\n", ret);
        return;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40000000,
        .mode = 0,
        .spics_io_num = TFT_CS,
        .queue_size = 7,
        .flags = SPI_DEVICE_NO_DUMMY,
    };

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle);
    if (ret != ESP_OK) {
        printf("SPI device add failed: 0x%x\n", ret);
        return;
    }

    printf("Sending initialization commands...\n");

    tft_command(ST7789_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));

    tft_command(ST7789_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(255));

    tft_command(ST7789_COLMOD);
    uint8_t colmod = 0x55;
    tft_data(&colmod, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // MADCTL settings for 90° clockwise rotation
    // MY=1, MX=0, MV=1, ML=0, RGB=0, MH=0
    tft_command(ST7789_MADCTL);
    uint8_t madctl = 0xA0;  // 90° rotation: MY=1, MX=0, MV=1
    tft_data(&madctl, 1);

    // For 90° rotation, swap width/height in CASET/RASET
    tft_command(ST7789_CASET);
    uint8_t caset[] = {0x00, 0x00, 0x01, 0x3F};  // 0-319 (320 pixels)
    tft_data(caset, 4);

    tft_command(ST7789_RASET);
    uint8_t raset[] = {0x00, 0x00, 0x00, 0xEF};  // 0-239 (240 pixels)
    tft_data(raset, 4);

    tft_command(ST7789_INVON);
    vTaskDelay(pdMS_TO_TICKS(10));

    tft_command(ST7789_NORON);
    vTaskDelay(pdMS_TO_TICKS(10));

    tft_command(ST7789_DISPON);
    vTaskDelay(pdMS_TO_TICKS(255));

    printf("Display initialized with 90° rotation (320x240)\n");
}

// ===== ROTARY ENCODER FUNCTIONS WITH DEBOUNCING =====
static void IRAM_ATTR encoder_isr_handler(void* arg) {
    static uint8_t last_clk = 1;
    static uint32_t last_enc_time = 0;
    
    uint32_t now = xTaskGetTickCountFromISR();
    
    // Hardware debouncing - 5ms minimum between changes
    if (now - last_enc_time < 5) {
        return;
    }
    
    uint8_t clk = gpio_get_level(ENC_CLK);
    uint8_t dt = gpio_get_level(ENC_DT);
    uint8_t btn = gpio_get_level(ENC_SW);
    
    // Handle rotation
    if (clk != last_clk) {
        portENTER_CRITICAL_ISR(&encoder_mux);
        if (dt != clk) {
            encoder_count++;
        } else {
            encoder_count--;
        }
        last_enc_time = now;
        portEXIT_CRITICAL_ISR(&encoder_mux);
        last_clk = clk;
    }
    
    // Handle button press with debouncing
    static uint32_t last_btn_time = 0;
    if (btn == 0) {  // Button pressed (active low)
        if (now - last_btn_time > 50) {  // 50ms debounce
            button_pressed = true;
            last_btn_time = now;
        }
    }
}

static void encoder_init() {
    printf("Initializing rotary encoder on pins: CLK=%d, DT=%d, SW=%d\n", 
           ENC_CLK, ENC_DT, ENC_SW);
    
    // Configure encoder pins with pull-ups
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ENC_CLK) | (1ULL << ENC_DT) | (1ULL << ENC_SW),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io_conf);
    
    // Install ISR service
    gpio_install_isr_service(0);
    
    // Add ISR handler
    gpio_isr_handler_add(ENC_CLK, encoder_isr_handler, NULL);
    gpio_isr_handler_add(ENC_DT, encoder_isr_handler, NULL);
    gpio_isr_handler_add(ENC_SW, encoder_isr_handler, NULL);
    
    printf("Rotary encoder initialized with debouncing\n");
}

// ===== MENU FUNCTIONS =====
static void apply_palette() {
    ui_color = palettes[current_palette_index].color;
    printf("Theme: %s (0x%04X)\n", palettes[current_palette_index].name, ui_color);
}

static void draw_please_stand_by() {
    tft_fill_screen(bg_color);
    tft_draw_string(120, 100, "STAND BY", ui_color, bg_color);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

static void show_menu_content(int index) {
    // Clear content area (from y=40 to y=200)
    tft_fill_rect(0, 40, DISPLAY_WIDTH, 160, bg_color);
    
    if (index == 0) {
        // THEME SELECTION
        tft_draw_string(100, 80, "THEME SELECTION", ui_color, bg_color);
        
        char theme_text[30];
        snprintf(theme_text, sizeof(theme_text), "Current: %s", palettes[current_palette_index].name);
        tft_draw_string(100, 100, theme_text, ui_color, bg_color);
        
        // Draw color swatch
        tft_fill_rect(140, 140, 40, 40, ui_color);
        tft_draw_rect(140, 140, 40, 40, COLOR_WHITE);
    } else if (index == 1) {
        // AUDIO VISUALIZER
        tft_draw_string(100, 80, "AUDIO VISUALIZER", ui_color, bg_color);
        tft_draw_string(100, 100, "Press to toggle demo", ui_color, bg_color);
    } else if (index == 2) {
        // IMAGE DISPLAY
        tft_draw_string(120, 80, "IMAGE DISPLAY", ui_color, bg_color);
        tft_draw_string(120, 100, "Press to show image", ui_color, bg_color);
    }
}

static void draw_full_menu(int selected_index) {
    tft_fill_screen(bg_color);
    
    // Draw header
    tft_draw_string(120, 5, "PIP-BOY OS", ui_color, bg_color);
    
    char theme_text[30];
    snprintf(theme_text, sizeof(theme_text), "THEME: %s", palettes[current_palette_index].name);
    tft_draw_string(100, 20, theme_text, ui_color, bg_color);
    
    // Draw separator line
    tft_draw_h_line(0, 35, DISPLAY_WIDTH, ui_color);
    
    // Draw menu content
    show_menu_content(selected_index);
    
    // Draw navigation at bottom
    int nav_y = DISPLAY_HEIGHT - 25;
    
    // Draw separator line
    tft_draw_h_line(0, nav_y - 5, DISPLAY_WIDTH, ui_color);
    
    // Draw menu items - centered horizontally
    for (int i = 0; i < menu_size; i++) {
        int x_pos = 60 + (i * 100);
        tft_draw_string(x_pos, nav_y, menu_items[i], ui_color, bg_color);
        
        // Draw selection box around selected item
        if (i == selected_index) {
            tft_draw_rect(x_pos - 4, nav_y - 4, 85, 20, ui_color);
        }
    }
}

static void update_menu_selection(int old_index, int new_index) {
    // Update menu content
    show_menu_content(new_index);
    
    int nav_y = DISPLAY_HEIGHT - 25;
    
    // Clear old selection box and redraw text
    int old_x_pos = 60 + (old_index * 100);
    tft_draw_rect(old_x_pos - 4, nav_y - 4, 85, 20, bg_color);
    tft_draw_string(old_x_pos, nav_y, menu_items[old_index], ui_color, bg_color);
    
    // Draw new selection box
    int new_x_pos = 60 + (new_index * 100);
    tft_draw_rect(new_x_pos - 4, nav_y - 4, 85, 20, ui_color);
    tft_draw_string(new_x_pos, nav_y, menu_items[new_index], ui_color, bg_color);
}

static void show_audio_demo() {
    static float phase = 0.0;
    int center_y = 120;
    
    // Clear demo area
    tft_fill_rect(0, 60, DISPLAY_WIDTH, 100, bg_color);
    
    // Draw sine wave
    int prev_y = center_y;
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
        int y = center_y + (int)(sin(x * 0.05 + phase) * 30);
        if (x > 0) {
            tft_draw_line(x - 1, prev_y, x, y, ui_color);
        }
        prev_y = y;
    }
    phase += 0.2;
}

// ===== SD CARD FUNCTIONS =====
static bool init_sd_card() {
    printf("Mounting SD card...\n");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_PROBING;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = SD_CLK;
    slot_config.cmd = SD_CMD;
    slot_config.d0 = SD_D0;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    sdmmc_card_t *card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

    if (ret == ESP_OK) {
        printf("SD card mounted successfully!\n");
        return true;
    } else {
        printf("SD card mount failed: 0x%x\n", ret);
        return false;
    }
}

// ===== MAIN APPLICATION =====
void app_main(void) {
    printf("\n========================================\n");
    printf("Image Viewer with Rotary Encoder Menu\n");
    printf("Freenove ESP32-S3\n");
    printf("========================================\n");

    printf("\nSettings:\n");
    printf("- Display: 320x240 (90° rotated)\n");
    printf("- Encoder: CLK=GPIO13, DT=GPIO14, SW=GPIO15\n");
    printf("- Menu items: THEME, AUDIO, IMAGE\n");

    // Initialize display with 90° rotation
    tft_init();

    // Initialize rotary encoder with debouncing
    encoder_init();

    // Apply initial palette
    apply_palette();

    // Show startup screen
    printf("\nShowing startup screen...\n");
    draw_please_stand_by();

    // Initialize SD card
    if (!init_sd_card()) {
        printf("SD card failed! Image display won't work.\n");
        
        // Show error pattern
        for (int i = 0; i < 3; i++) {
            tft_fill_screen(COLOR_RED);
            vTaskDelay(pdMS_TO_TICKS(300));
            tft_fill_screen(COLOR_BLACK);
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }

    // Show initial menu
    printf("\nShowing main menu...\n");
    draw_full_menu(current_menu_index);

    printf("\n=== SYSTEM READY ===\n");
    printf("Rotate encoder to navigate menu\n");
    printf("Press encoder button to select\n");
    printf("Current menu: %d - %s\n", current_menu_index, menu_items[current_menu_index]);

    // Main loop
    int32_t last_count = encoder_count;
    TickType_t last_process_time = xTaskGetTickCount();
    bool image_mode = false;
    const char* image_files[] = {
        "/sdcard/vault_boy.raw",
        "/sdcard/vault_boy_rgb.raw",
        "/sdcard/vault_boy_bgr.raw",
        "/sdcard/vault_boy_swap.raw"
    };

    while (1) {
        // Check if we're in image display mode
        if (image_mode) {
            printf("Entering image display mode\n");
            
            // Try to display an image
            bool image_displayed = false;
            for (int i = 0; i < 4; i++) {
                printf("Trying: %s\n", image_files[i]);
                if (display_image(image_files[i])) {
                    image_displayed = true;
                    printf("Image displayed successfully\n");
                    break;
                }
            }
            
            if (!image_displayed) {
                printf("No image found\n");
                tft_fill_screen(COLOR_RED);
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            
            // Wait for encoder input to return to menu
            printf("Rotate encoder to return to menu\n");
            while (1) {
                int32_t current_count;
                portENTER_CRITICAL(&encoder_mux);
                current_count = encoder_count;
                portEXIT_CRITICAL(&encoder_mux);
                
                if (current_count != last_count) {
                    last_count = current_count;
                    break;
                }
                
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            
            image_mode = false;
            printf("Returning to menu\n");
            draw_full_menu(current_menu_index);
            last_process_time = xTaskGetTickCount();
        }

        // Handle encoder rotation with software debouncing
        int32_t current_count;
        portENTER_CRITICAL(&encoder_mux);
        current_count = encoder_count;
        portEXIT_CRITICAL(&encoder_mux);
        
        int32_t delta = current_count - last_count;
        
        if (delta != 0) {
            // Additional software debouncing
            TickType_t now = xTaskGetTickCount();
            if (now - last_process_time > pdMS_TO_TICKS(100)) {  // 100ms debounce
                int step = (delta > 0) ? 1 : -1;
                
                if (!is_demo_active) {
                    int old_index = current_menu_index;
                    current_menu_index = (current_menu_index + step);
                    if (current_menu_index < 0) current_menu_index = menu_size - 1;
                    if (current_menu_index >= menu_size) current_menu_index = 0;
                    
                    if (current_menu_index != old_index) {
                        printf("Menu changed to: %d - %s\n", 
                               current_menu_index, menu_items[current_menu_index]);
                        update_menu_selection(old_index, current_menu_index);
                    }
                }
                
                last_count = current_count;
                last_process_time = now;
            }
        }
        
        // Handle button press
        if (button_pressed) {
            // Button debouncing
            TickType_t now = xTaskGetTickCount();
            if (now - last_process_time > pdMS_TO_TICKS(300)) {
                printf("Button pressed on menu %d - %s\n", 
                       current_menu_index, menu_items[current_menu_index]);
                
                if (current_menu_index == 0) {
                    // Theme selection
                    current_palette_index = (current_palette_index + 1) % num_palettes;
                    apply_palette();
                    draw_full_menu(current_menu_index);
                } 
                else if (current_menu_index == 1) {
                    // Audio demo toggle
                    is_demo_active = !is_demo_active;
                    printf("Audio demo: %s\n", is_demo_active ? "ON" : "OFF");
                    if (!is_demo_active) {
                        draw_full_menu(current_menu_index);
                    }
                }
                else if (current_menu_index == 2) {
                    // Enter image display mode
                    image_mode = true;
                }
                
                button_pressed = false;
                last_process_time = now;
            }
        }
        
        // Handle audio demo if active
        if (is_demo_active && current_menu_index == 1) {
            show_audio_demo();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        
        // Small delay to avoid busy loop
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}