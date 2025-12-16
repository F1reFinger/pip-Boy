#include "pipboy_render.h"
#include "pipboy_common.h"
#include "tft_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "PIPBOY_RENDER";

// Your rendering functions here...
void tft_render_task(void *pvParameter) {
    ESP_LOGI(TAG, "TFT Render Task started.");
    int trigger_index;
    
    // Initial full menu draw should happen after splash
    if (xSemaphoreTake(tft_mutex, portMAX_DELAY) == pdTRUE) {
        draw_full_menu(); 
        xSemaphoreGive(tft_mutex);
    }

    while (1) {
        if (g_state.isSystemHalted) {
            // System halt sequence
            if (xSemaphoreTake(tft_mutex, portMAX_DELAY) == pdTRUE) {
                draw_shutdown_sequence(true);
                tft_fill_screen(ST77XX_BLACK);
                xSemaphoreGive(tft_mutex);
            }
            vTaskDelay(portMAX_DELAY);
        }

        // 1. Check for Menu Redraw Trigger
        if (xQueueReceive(menu_update_queue, &trigger_index, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (xSemaphoreTake(tft_mutex, portMAX_DELAY) == pdTRUE) {
                // Redraw logic based on current state
                if (!g_state.isDemoActive && !g_state.isSubMenuActive) {
                    draw_full_menu();
                } else if (g_state.isSubMenuActive) {
                    draw_full_menu();
                } else if (g_state.isDemoActive && g_state.currentMenuIndex == 1) {
                    tft_fill_screen(ST77XX_BLACK);
                    show_audio_demo(false);
                }
                xSemaphoreGive(tft_mutex);
            }
        }
        
        // 2. Continuous Animation (Audio Demo)
        if (g_state.isDemoActive && g_state.currentMenuIndex == 1 && !g_state.isSubMenuActive) {
            if (xSemaphoreTake(tft_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
                show_audio_demo(true);
                xSemaphoreGive(tft_mutex);
            }
        }
        
        // 3. Status Bar Refresh
        static uint64_t last_status_update = 0;
        uint64_t current_time = esp_timer_get_time() / 1000;
        
        if (!g_state.isDemoActive && !g_state.isSystemHalted && 
            (current_time - last_status_update) > 500) {
            if (xSemaphoreTake(tft_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
                draw_clock(); 
                xSemaphoreGive(tft_mutex);
                last_status_update = current_time;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

// Implement all your drawing functions here...
void draw_clock() {
    // In a real IDF app, use esp_sntp or gettimeofday() for real time
    tft_draw_filled_rect(TFT_WIDTH - 80, 0, 80, 18, ST77XX_BLACK);
    // Use a fixed time string for the mock
    tft_draw_text(TFT_WIDTH - 45, 5, "12:00", 1, PB_GREEN); 

    // Draw connection status icon/text near the clock
    EventBits_t uxBits = xEventGroupGetBits(wifi_event_group);
    bool is_connected = (uxBits & WIFI_CONNECTED_BIT) != 0;
    
    tft_draw_filled_rect(TFT_WIDTH - 120, 0, 40, 18, ST77XX_BLACK);
    if (is_connected) {
        tft_draw_text(TFT_WIDTH - 110, 5, "WIFI+", 1, PB_GREEN);
    } else {
        tft_draw_text(TFT_WIDTH - 110, 5, "WIFI-", 1, PB_DARK_GREEN); 
    }
}

void draw_splash_screen() {
    tft_fill_screen(ST77XX_BLACK);
    int centerX = TFT_WIDTH / 2;
    int centerY = TFT_HEIGHT / 2;
    
    // Draw concentric circles
    tft_draw_circle(centerX, centerY, 30, PB_GREEN);
    tft_draw_circle(centerX, centerY, 60, PB_DARK_GREEN);
    
    // Draw Text
    tft_draw_text(centerX - 85, centerY - 15, "PLEASE", 3, PB_GREEN);
    tft_draw_text(centerX - 100, centerY + 20, "STAND BY", 3, PB_GREEN);
    
    vTaskDelay(pdMS_TO_TICKS(1500)); 
}

// Continue with the rest of your drawing functions...
// You'll need to copy all the drawing functions from app_main.c to here

void draw_full_menu() {
    // Copy this function from your app_main.c
}

void draw_wifi_sub_menu(bool initial_draw) {
    // Copy this function from your app_main.c
}

void show_audio_demo(bool running) {
    // Copy this function from your app_main.c
}

void draw_shutdown_sequence(bool is_final) {
    // Copy this function from your app_main.c
}