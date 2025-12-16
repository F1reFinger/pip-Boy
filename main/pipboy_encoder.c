#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "pipboy_state.h"
#include "esp_random.h"


static const char *TAG = "ENC_TASK";

// --- Encoder ISR (Handles button press) ---
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    encoder_event_t event = {ENC_DIR_NONE, false};

    if (gpio_num == PIN_ENCODER_SW) {
        // Simple button press detection (debounce/timing handled in task)
        event.button_press = true;
        xQueueSendFromISR(g_encoder_queue, &event, NULL);
    } 
    // Rotation logic would typically use PCNT or a high-frequency timer
}

// TASK 1: Handles Rotary Encoder Input and Menu Logic
void encoder_task(void *pvParameter) {
    ESP_LOGI(TAG, "Encoder Task started.");

    // --- GPIO Setup for Button and DT/CLK ---
    gpio_set_direction((gpio_num_t)PIN_ENCODER_SW, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)PIN_ENCODER_SW, GPIO_PULLUP_ONLY);
    gpio_set_intr_type((gpio_num_t)PIN_ENCODER_SW, GPIO_INTR_NEGEDGE); // Detect button press

    // Install ISR service only once
    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)PIN_ENCODER_SW, gpio_isr_handler, (void*) PIN_ENCODER_SW);
    
    // Simple state simulation for rotation (replace with PCNT logic)
    while (1) {
        encoder_event_t event;
        
        // --- Process Button Events from Queue (or rotational events) ---
        if (xQueueReceive(g_encoder_queue, &event, 0) == pdTRUE) {
            if (event.button_press || event.direction != ENC_DIR_NONE) {
                // Send event to the Menu Logic Task for state change processing
                xQueueSend(g_menu_update_queue, &event, portMAX_DELAY);
            }
        }
        
        // --- Simulate Rotation (replace with actual PCNT read) ---
        if (g_state.isDemoActive || g_state.isSystemHalted) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // Fictional rotation reading (simulate rotation every 5 seconds)
        if (esp_random() % 1000 < 1) { 
            event.direction = ENC_DIR_CW;
            xQueueSend(g_encoder_queue, &event, portMAX_DELAY);
        } else if (esp_random() % 1000 < 1) {
            event.direction = ENC_DIR_CCW;
            xQueueSend(g_encoder_queue, &event, portMAX_DELAY);
        }

        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}


// TASK 2: Handles Menu State Changes based on Encoder Input (Logic Decoupled from Input)
void menu_logic_task(void *pvParameter) {
    ESP_LOGI(TAG, "Menu Logic Task started.");
    
    while (1) {
        encoder_event_t enc_event;

        // Process combined rotation/button events from the Encoder Task
        if (xQueueReceive(g_menu_update_queue, &enc_event, portMAX_DELAY) == pdTRUE) {
            
            if (g_state.isSystemHalted) continue;

            // 1. Handle Rotation
            if (enc_event.direction != ENC_DIR_NONE) {
                int step = (enc_event.direction == ENC_DIR_CW) ? 1 : -1;

                if (g_state.isSubMenuActive && g_state.currentMenuIndex == 0) {
                    // Sub-Menu Navigation (Vertical)
                    g_state.currentSubMenuIndex = (g_state.currentSubMenuIndex + step) % WIFI_SUB_MENU_SIZE;
                    if (g_state.currentSubMenuIndex < 0) g_state.currentSubMenuIndex = WIFI_SUB_MENU_SIZE - 1;
                    ESP_LOGI(TAG, "Sub-Menu Nav: %d", g_state.currentSubMenuIndex);
                    // No need to send to update queue, just set the flag below
                } else if (!g_state.isDemoActive) {
                    // Main Menu Navigation (Horizontal)
                    g_state.currentMenuIndex = (g_state.currentMenuIndex + step) % MENU_SIZE;
                    if (g_state.currentMenuIndex < 0) g_state.currentMenuIndex = MENU_SIZE - 1;
                    ESP_LOGI(TAG, "Main Menu Nav: %d (%s)", g_state.currentMenuIndex, MENU_ITEMS[g_state.currentMenuIndex]);
                    // No need to send to update queue, just set the flag below
                }
            }
            
            // 2. Handle Button Press
            if (enc_event.button_press) {
                if (g_state.isSubMenuActive && g_state.currentMenuIndex == 0) {
                    // Action in WiFi Sub-Menu
                    if (g_state.currentSubMenuIndex == 0) {
                        ESP_LOGI(TAG, "Action: Toggle WiFi Triggered");
                        xEventGroupSetBits(g_wifi_event_group, WIFI_TOGGLE_BIT); // Command to WiFi Task
                    } else if (g_state.currentSubMenuIndex == 1) {
                        g_state.isBrokerConnected = !g_state.isBrokerConnected;
                        ESP_LOGI(TAG, "Action: Toggle Broker to %d", g_state.isBrokerConnected);
                    } else if (g_state.currentSubMenuIndex == 2) {
                        g_state.isSubMenuActive = false;
                        g_state.isDemoActive = false;
                        ESP_LOGI(TAG, "Action: Back to Main Menu");
                    }
                } else if (g_state.isDemoActive) {
                     // Exit general demo (Audio or Sub-Menu)
                    g_state.isDemoActive = false;
                    g_state.isSubMenuActive = false;
                    ESP_LOGI(TAG, "Action: Exit Demo Mode");
                } else {
                    // Main Menu Action (Enter Sub-Menu/Demo)
                    if (g_state.currentMenuIndex == 0) { // WIFI
                        g_state.isDemoActive = true;
                        g_state.isSubMenuActive = true;
                        g_state.currentSubMenuIndex = 0;
                        ESP_LOGI(TAG, "Action: Enter WIFI Sub-Menu");
                    } else if (g_state.currentMenuIndex == 1) { // AUDIO
                        g_state.isDemoActive = true;
                        ESP_LOGI(TAG, "Action: Enter Audio Demo");
                    } else if (g_state.currentMenuIndex == 2) { // POWER
                        g_state.isSystemHalted = true;
                        ESP_LOGW(TAG, "SYSTEM HALT INITIATED");
                    }
                }
            }
            
            // Trigger a redraw after ANY state change (rotation or button)
            int redraw_flag = 1;
            xQueueSend(g_menu_update_queue, &redraw_flag, portMAX_DELAY);
        }
    }
}