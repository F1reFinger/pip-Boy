#include "pipboy_state.h"

// --- FreeRTOS Handles and Events ---
SemaphoreHandle_t g_tft_mutex;
QueueHandle_t g_encoder_queue;
QueueHandle_t g_menu_update_queue;
EventGroupHandle_t g_wifi_event_group;

// --- Global State Management ---
menu_state_t g_state = {
    .currentMenuIndex = 0,
    .currentSubMenuIndex = 0,
    .isSubMenuActive = false,
    .isDemoActive = false,
    .isBrokerConnected = false,
    .isSystemHalted = false
};

// Menu Text Data Definition
const char* MENU_ITEMS[] = {"1. WIFI", "2. AUDIO", "3. POWER"};
const int MENU_SIZE = 3;
const char* WIFI_SUB_MENU_ITEMS[] = {"1. CONNECT WIFI", "2. CONNECT BROKER", "3. BACK"};
const int WIFI_SUB_MENU_SIZE = 3;