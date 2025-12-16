#ifndef PIPBOY_COMMON_H
#define PIPBOY_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

// --- Global State Management ---
typedef struct {
    int currentMenuIndex;
    int currentSubMenuIndex;
    bool isSubMenuActive;
    bool isDemoActive;
    bool isBrokerConnected;
    bool isSystemHalted;
} menu_state_t;

// --- Encoder Event Definitions ---
typedef enum {
    ENC_DIR_NONE,
    ENC_DIR_CW,
    ENC_DIR_CCW
} encoder_direction_t;

typedef struct {
    encoder_direction_t direction;
    bool button_press;
} encoder_event_t;

// --- External Global Variables ---
extern menu_state_t g_state;
extern SemaphoreHandle_t tft_mutex;
extern QueueHandle_t encoder_queue;
extern QueueHandle_t menu_update_queue;
extern EventGroupHandle_t wifi_event_group;
extern const int WIFI_CONNECTED_BIT;

#endif