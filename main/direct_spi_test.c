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

// ST7789 commands
#define ST7789_NOP     0x00
#define ST7789_SWRESET 0x01
#define ST7789_RDDID   0x04
#define ST7789_RDDST   0x09
#define ST7789_SLPIN   0x10
#define ST7789_SLPOUT  0x11
#define ST7789_PTLON   0x12
#define ST7789_NORON   0x13
#define ST7789_INVOFF  0x20
#define ST7789_INVON   0x21
#define ST7789_DISPOFF 0x28
#define ST7789_DISPON  0x29
#define ST7789_CASET   0x2A
#define ST7789_RASET   0x2B
#define ST7789_RAMWR   0x2C
#define ST7789_RAMRD   0x2E
#define ST7789_MADCTL  0x36
#define ST7789_COLMOD  0x3A

void send_cmd(spi_device_handle_t spi, uint8_t cmd) {
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    
    gpio_set_level(PIN_NUM_DC, 0); // Command mode
    t.length = 8;
    t.tx_buffer = &cmd;
    ret = spi_device_polling_transmit(spi, &t);
    assert(ret == ESP_OK);
}

void send_data(spi_device_handle_t spi, uint8_t data) {
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    
    gpio_set_level(PIN_NUM_DC, 1); // Data mode
    t.length = 8;
    t.tx_buffer = &data;
    ret = spi_device_polling_transmit(spi, &t);
    assert(ret == ESP_OK);
}

void send_data_multiple(spi_device_handle_t spi, const uint8_t *data, int len) {
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
    printf("=== Direct SPI ST7789 Test ===\n");
    
    // 1. Setup GPIOs
    gpio_set_direction(PIN_NUM_BCKL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_BCKL, 1); // Backlight ON
    
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_CS, GPIO_MODE_OUTPUT);
    
    // 2. Hardware reset
    printf("Resetting display...\n");
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
    
    // CS high initially
    gpio_set_level(PIN_NUM_CS, 1);
    
    // 4. Initialize display with basic commands
    printf("Sending initialization commands...\n");
    
    gpio_set_level(PIN_NUM_CS, 0); // CS low
    
    // Software reset
    send_cmd(spi, ST7789_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));
    
    // Sleep out
    send_cmd(spi, ST7789_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(150));
    
    // Color mode: 16-bit RGB565
    send_cmd(spi, ST7789_COLMOD);
    send_data(spi, 0x55); // 16-bit/pixel
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Display inversion on (sometimes needed)
    send_cmd(spi, ST7789_INVON);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Memory data access control
    send_cmd(spi, ST7789_MADCTL);
    send_data(spi, 0x00); // Normal orientation
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Normal display on
    send_cmd(spi, ST7789_NORON);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Display on
    send_cmd(spi, ST7789_DISPON);
    vTaskDelay(pdMS_TO_TICKS(150));
    
    // 5. Draw a test pattern
    printf("Drawing test pattern...\n");
    
    // Set column address (X: 0-239)
    send_cmd(spi, ST7789_CASET);
    send_data(spi, 0x00); send_data(spi, 0x00); // Start X
    send_data(spi, 0x00); send_data(spi, 0xEF); // End X (239)
    
    // Set row address (Y: 0-319)
    send_cmd(spi, ST7789_RASET);
    send_data(spi, 0x00); send_data(spi, 0x00); // Start Y
    send_data(spi, 0x01); send_data(spi, 0x3F); // End Y (319)
    
    // Write to RAM
    send_cmd(spi, ST7789_RAMWR);
    
    // Fill screen with red
    printf("Filling with red...\n");
    for (int i = 0; i < 240 * 320; i++) {
        send_data(spi, 0xF8); // R5 + G3 high bits
        send_data(spi, 0x00); // G3 low bits + B5
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Fill screen with green
    printf("Filling with green...\n");
    send_cmd(spi, ST7789_CASET);
    send_data(spi, 0x00); send_data(spi, 0x00);
    send_data(spi, 0x00); send_data(spi, 0xEF);
    send_cmd(spi, ST7789_RASET);
    send_data(spi, 0x00); send_data(spi, 0x00);
    send_data(spi, 0x01); send_data(spi, 0x3F);
    send_cmd(spi, ST7789_RAMWR);
    
    for (int i = 0; i < 240 * 320; i++) {
        send_data(spi, 0x07); // R5 + G3 high bits
        send_data(spi, 0xE0); // G3 low bits + B5
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Fill screen with blue
    printf("Filling with blue...\n");
    send_cmd(spi, ST7789_CASET);
    send_data(spi, 0x00); send_data(spi, 0x00);
    send_data(spi, 0x00); send_data(spi, 0xEF);
    send_cmd(spi, ST7789_RASET);
    send_data(spi, 0x00); send_data(spi, 0x00);
    send_data(spi, 0x01); send_data(spi, 0x3F);
    send_cmd(spi, ST7789_RAMWR);
    
    for (int i = 0; i < 240 * 320; i++) {
        send_data(spi, 0x00); // R5 + G3 high bits
        send_data(spi, 0x1F); // G3 low bits + B5
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    gpio_set_level(PIN_NUM_CS, 1); // CS high
    
    printf("\n=== Test Complete ===\n");
    printf("If screen remained black:\n");
    printf("1. Check ALL wiring connections\n");
    printf("2. Display might need 5V (some versions)\n");
    printf("3. Try swapping MOSI/MISO\n");
    printf("4. Check if display is actually ST7789\n");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}