#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#define PIN_NUM_MOSI   11
#define PIN_NUM_CLK    12
#define PIN_NUM_CS     13
#define PIN_NUM_DC     9
#define PIN_NUM_RST    10
#define PIN_NUM_BCKL   14

// Common display commands
#define CMD_SWRESET    0x01
#define CMD_SLPOUT     0x11
#define CMD_INVOFF     0x20
#define CMD_INVON      0x21
#define CMD_DISPOFF    0x28
#define CMD_DISPON     0x29
#define CMD_CASET      0x2A
#define CMD_RASET      0x2B
#define CMD_RAMWR      0x2C
#define CMD_MADCTL     0x36
#define CMD_COLMOD     0x3A

// Send command to display
void lcd_cmd(spi_device_handle_t spi, uint8_t cmd) {
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    
    gpio_set_level(PIN_NUM_DC, 0); // Command mode
    t.length = 8;
    t.tx_buffer = &cmd;
    ret = spi_device_polling_transmit(spi, &t);
    assert(ret == ESP_OK);
}

// Send data to display
void lcd_data(spi_device_handle_t spi, uint8_t data) {
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    
    gpio_set_level(PIN_NUM_DC, 1); // Data mode
    t.length = 8;
    t.tx_buffer = &data;
    ret = spi_device_polling_transmit(spi, &t);
    assert(ret == ESP_OK);
}

// Send multiple bytes of data
void lcd_data_multiple(spi_device_handle_t spi, uint8_t *data, int len) {
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    
    gpio_set_level(PIN_NUM_DC, 1); // Data mode
    t.length = len * 8;
    t.tx_buffer = data;
    ret = spi_device_polling_transmit(spi, &t);
    assert(ret == ESP_OK);
}

void app_main(void) {
    printf("=== Simple Display Initialization ===\n");
    
    // 1. Setup GPIOs
    gpio_set_direction(PIN_NUM_BCKL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_BCKL, 1); // Backlight ON
    
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_CS, GPIO_MODE_OUTPUT);
    
    // 2. Hardware reset
    printf("Hardware reset...\n");
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 3. Initialize SPI
    printf("Initializing SPI...\n");
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 320 * 240 * 2,
    };
    
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10000000, // 10MHz
        .mode = 0,                  // SPI mode 0
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 7,
        .flags = SPI_DEVICE_NO_DUMMY,
    };
    
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_DISABLED);
    
    spi_device_handle_t spi;
    spi_bus_add_device(SPI2_HOST, &devcfg, &spi);
    
    // CS active low
    gpio_set_level(PIN_NUM_CS, 0);
    
    // 4. Initialize display (generic initialization)
    printf("Initializing display...\n");
    
    // Software reset
    lcd_cmd(spi, CMD_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));
    
    // Sleep out
    lcd_cmd(spi, CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(150));
    
    // Color mode: 16-bit
    lcd_cmd(spi, CMD_COLMOD);
    lcd_data(spi, 0x55); // 16-bit/pixel
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Memory access control - try different values
    printf("Testing MADCTL values...\n");
    uint8_t madctl_values[] = {0x00, 0x40, 0x80, 0xC0, 0x20, 0x60, 0xA0, 0xE0};
    
    for (int i = 0; i < 8; i++) {
        printf("MADCTL = 0x%02X\n", madctl_values[i]);
        
        lcd_cmd(spi, CMD_MADCTL);
        lcd_data(spi, madctl_values[i]);
        
        // Clear screen to a color
        lcd_cmd(spi, CMD_CASET);
        uint8_t col_data[] = {0x00, 0x00, 0x00, 0xEF}; // 0-239
        lcd_data_multiple(spi, col_data, 4);
        
        lcd_cmd(spi, CMD_RASET);
        uint8_t row_data[] = {0x00, 0x00, 0x01, 0x3F}; // 0-319
        lcd_data_multiple(spi, row_data, 4);
        
        lcd_cmd(spi, CMD_RAMWR);
        
        // Fill with a color based on test number
        uint16_t color;
        switch(i) {
            case 0: color = 0xF800; break; // Red
            case 1: color = 0x07E0; break; // Green
            case 2: color = 0x001F; break; // Blue
            case 3: color = 0xFFFF; break; // White
            case 4: color = 0xFFE0; break; // Yellow
            case 5: color = 0x07FF; break; // Cyan
            case 6: color = 0xF81F; break; // Magenta
            case 7: color = 0x0000; break; // Black
        }
        
        // Send a few pixels
        for (int p = 0; p < 1000; p++) {
            uint8_t color_bytes[2] = {(color >> 8) & 0xFF, color & 0xFF};
            lcd_data_multiple(spi, color_bytes, 2);
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    // Display on
    lcd_cmd(spi, CMD_DISPON);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    gpio_set_level(PIN_NUM_CS, 1); // CS high
    
    printf("\n=== Test Complete ===\n");
    printf("Look for:\n");
    printf("1. Which MADCTL value gave correct orientation\n");
    printf("2. What colors you actually saw\n");
    printf("3. If display was full screen or partial\n");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}