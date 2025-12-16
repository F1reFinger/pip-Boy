#ifndef PIPBOY_STATE_H
#define PIPBOY_STATE_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

// --- PIN Definitions (Adjust to your Kconfig settings) ---
#define PIN_TFT_CS CONFIG_PIN_TFT_CS
#define PIN_TFT_RST CONFIG_PIN_TFT_RST
#define PIN_TFT_DC CONFIG_PIN_TFT_DC
#define PIN_TFT_MOSI CONFIG_PIN_TFT_MOSI
#define PIN_TFT_SCLK CONFIG_PIN_TFT_SCLK

#define PIN_ENCODER_CLK CONFIG_PIN_ENCODER_CLK
#define PIN_ENCODER_DT CONFIG_PIN_ENCODER_DT
#define PIN_ENCODER_SW CONFIG_PIN_ENCODER_SW

// --- PIP-BOY MENU COLORS (16-bit) ---
#define PB_GREEN 0x07E0
#define PB_DARK_GREEN 0x03E0
#define ST77XX_BLACK 0x0000

// --- Display Geometry ---
#define TFT_WIDTH 320
#define TFT_HEIGHT 240

// --- FreeRTOS Event Bitmasks ---
extern EventGroupHandle_t g_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_TOGGLE_BIT BIT2

// --- Encoder Event Data Structure ---
typedef enum {
    ENC_DIR_NONE,
    ENC_DIR_CW,
    ENC_DIR_CCW
} encoder_direction_t;

typedef struct {
    encoder_direction_t direction;
    bool button_press;
} encoder_event_t;

// --- Global Application State ---
typedef struct {
    int currentMenuIndex;
    int currentSubMenuIndex;
    bool isSubMenuActive;
    bool isDemoActive;
    bool isBrokerConnected;
    bool isSystemHalted;
} menu_state_t;

// --- External References to Global Objects ---
extern menu_state_t g_state;
extern SemaphoreHandle_t g_tft_mutex;
extern QueueHandle_t g_encoder_queue;
extern QueueHandle_t g_menu_update_queue;

// Menu Text Data
extern const char* MENU_ITEMS[];
extern const int MENU_SIZE;
extern const char* WIFI_SUB_MENU_ITEMS[];
extern const int WIFI_SUB_MENU_SIZE;


// --- Task Prototypes ---
void encoder_task(void *pvParameter);
void menu_logic_task(void *pvParameter);
void tft_render_task(void *pvParameter);
void wifi_connection_task(void *pvParameter);

#endif // PIPBOY_STATE_H