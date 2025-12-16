#include <stdio.h>
#include <math.h> 
#include <string.h> 
#include <stdlib.h> 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h" 
#include "freertos/semphr.h" 
#include "esp_log.h"
#include "esp_wifi.h" 
#include "esp_event.h" 
#include "esp_netif.h" 
#include "nvs_flash.h"
#include "esp_timer.h" 
#include "esp_random.h" 
#include "driver/gpio.h"
#include "tft_driver.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Configuration ---
#ifndef CONFIG_PIPBOY_SSID
#define CONFIG_PIPBOY_SSID "Helder OI  FIBRA"
#endif

#ifndef CONFIG_PIPBOY_PASSWORD
#define CONFIG_PIPBOY_PASSWORD "25670980"
#endif

// --- PIN Definitions ---
#define ROTARY_ENCODER_CLK_PIN GPIO_NUM_32
#define ROTARY_ENCODER_DT_PIN  GPIO_NUM_33
#define ROTARY_ENCODER_SW_PIN  GPIO_NUM_27

// --- PIP-BOY MENU COLORS ---
#define PB_GREEN 0x07E0
#define PB_DARK_GREEN 0x03E0
#define ST77XX_BLACK 0x0000

static const char *TAG = "PIPBOY_APP";

// --- Global State Variables ---
static bool isSystemHalted = false;
static bool isDemoActive = false;
static bool isSubMenuActive = false;
static bool isBrokerConnected = false;
static int currentMenuIndex = 0;
static int currentSubMenuIndex = 0;

// Menu items
static const char* menuItems[] = {
    "1. WIFI",
    "2. AUDIO", 
    "3. POWER"
};
static const int menuSize = 3;

// WiFi sub-menu items
static const char* wifiSubMenuItems[] = {
    "1. CONNECT WIFI",
    "2. CONNECT BROKER",
    "3. BACK"
};
static const int wifiSubMenuSize = 3;

// --- FreeRTOS Handles ---
static QueueHandle_t encoder_queue;
static SemaphoreHandle_t tft_mutex;
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT = BIT1;

// --- Encoder State ---
static volatile int32_t encoder_value = 0;
static volatile uint8_t last_encoder_state = 0;
static uint64_t last_encoder_process_time = 0;
static const uint32_t ENCODER_DEBOUNCE_MS = 50; // Reduced for better responsiveness

// WiFi state
static int wifi_status = 0; // 0=disconnected, 1=connecting, 2=connected

// --- Function Prototypes ---
void draw_please_stand_by(void);
void draw_full_menu(int selectedIndex);
void show_menu_content(int index);
void update_menu_selection(int oldIndex, int newIndex);
void run_menu_action(int index);
void draw_wifi_sub_menu(int selectedIndex, bool initialDraw);
void handle_wifi_sub_menu_toggle(int index);
void show_audio_demo(bool running);
void show_power_screen(void);
void draw_shutdown_sequence(bool isFinal);
void draw_clock(void);

// Encoder functions
static uint8_t read_encoder_state(void);
void encoder_task(void *pvParameter);
static void gpio_isr_handler(void* arg); // Declaration without IRAM_ATTR
void init_rotary_encoder(void);

// WiFi functions  
void wifi_connection_task(void *pvParameter);
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

// =========================================================================
//                             I N I T I A L I Z A T I O N
// =========================================================================

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize synchronization primitives
    tft_mutex = xSemaphoreCreateMutex();
    encoder_queue = xQueueCreate(20, sizeof(int32_t)); // Larger queue
    wifi_event_group = xEventGroupCreate();

    if (!tft_mutex || !encoder_queue || !wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create FreeRTOS objects");
        return;
    }

    // Initialize hardware
    init_rotary_encoder();
    tft_init_driver();

    // Draw splash screen
    if (xSemaphoreTake(tft_mutex, portMAX_DELAY) == pdTRUE) {
        draw_please_stand_by();
        xSemaphoreGive(tft_mutex);
    }

    // Create tasks
    xTaskCreate(encoder_task, "encoder", 4096, NULL, 10, NULL); // Increased stack
    xTaskCreate(wifi_connection_task, "wifi", 4096, NULL, 6, NULL);

    // Draw initial menu
    if (xSemaphoreTake(tft_mutex, portMAX_DELAY) == pdTRUE) {
        draw_full_menu(currentMenuIndex);
        xSemaphoreGive(tft_mutex);
    }

    ESP_LOGI(TAG, "Pip-Boy started successfully");
}

// =========================================================================
//                         E N C O D E R   H A N D L I N G  
// =========================================================================

static uint8_t read_encoder_state(void) {
    return (gpio_get_level(ROTARY_ENCODER_CLK_PIN) << 1) | gpio_get_level(ROTARY_ENCODER_DT_PIN);
}

// FIX: Remove IRAM_ATTR from declaration, keep on definition
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    if (gpio_num == ROTARY_ENCODER_SW_PIN) {
        int32_t button_press = -1;
        xQueueSendFromISR(encoder_queue, &button_press, &xHigherPriorityTaskWoken);
    }
    
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void init_rotary_encoder(void) {
    // Configure button pin with interrupt
    gpio_config_t btn_config = {
        .pin_bit_mask = (1ULL << ROTARY_ENCODER_SW_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&btn_config);

    // Configure CLK/DT pins
    gpio_config_t rot_config = {
        .pin_bit_mask = (1ULL << ROTARY_ENCODER_CLK_PIN) | (1ULL << ROTARY_ENCODER_DT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&rot_config);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(ROTARY_ENCODER_SW_PIN, gpio_isr_handler, (void*)ROTARY_ENCODER_SW_PIN);

    last_encoder_state = read_encoder_state();
    ESP_LOGI(TAG, "Rotary encoder initialized");
}

void encoder_task(void *pvParameter) {
    // Silence unused variable warnings
    (void)wifi_status;
    (void)wifiSubMenuItems;
    (void)isBrokerConnected;
    (void)isSystemHalted;

    uint8_t current_state;
    int32_t encoder_delta;
    uint64_t last_rotation_time = 0;
    const uint32_t ROTATION_DEBOUNCE_MS = 5; // Fast rotation detection
    
    while (1) {
        // Process encoder rotation with higher frequency
        current_state = read_encoder_state();
        
        if (current_state != last_encoder_state) {
            uint64_t current_time = esp_timer_get_time() / 1000;
            
            if (current_time - last_rotation_time > ROTATION_DEBOUNCE_MS) {
                // Improved quadrature decoding
                if ((last_encoder_state == 0 && current_state == 2) ||
                    (last_encoder_state == 2 && current_state == 3) ||
                    (last_encoder_state == 3 && current_state == 1) ||
                    (last_encoder_state == 1 && current_state == 0)) {
                    encoder_delta = 1; // CW
                } else {
                    encoder_delta = -1; // CCW
                }
                
                xQueueSend(encoder_queue, &encoder_delta, 0);
                last_rotation_time = current_time;
            }
            last_encoder_state = current_state;
        }

        // Process events from queue
        int32_t event;
        if (xQueueReceive(encoder_queue, &event, pdMS_TO_TICKS(2)) == pdTRUE) {
            uint64_t current_time = esp_timer_get_time() / 1000;
            
            if (current_time - last_encoder_process_time > ENCODER_DEBOUNCE_MS) {
                last_encoder_process_time = current_time;
                
                if (xSemaphoreTake(tft_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    if (event == -1) { // Button press
                        ESP_LOGI(TAG, "Button pressed");
                        if (isSubMenuActive && currentMenuIndex == 0) {
                            handle_wifi_sub_menu_toggle(currentSubMenuIndex);
                            vTaskDelay(pdMS_TO_TICKS(150));
                            draw_wifi_sub_menu(currentSubMenuIndex, true);
                        } else if (!isDemoActive) {
                            run_menu_action(currentMenuIndex);
                            vTaskDelay(pdMS_TO_TICKS(150));
                        } else {
                            isDemoActive = false;
                            tft_fill_screen(ST77XX_BLACK);
                            vTaskDelay(pdMS_TO_TICKS(150));
                            draw_full_menu(currentMenuIndex);
                        }
                    } else { // Rotation
                        int step = (event > 0) ? 1 : -1;
                        
                        if (isSubMenuActive && currentMenuIndex == 0) {
                            int oldSubIndex = currentSubMenuIndex;
                            currentSubMenuIndex = (currentSubMenuIndex + step + wifiSubMenuSize) % wifiSubMenuSize;
                            draw_wifi_sub_menu(currentSubMenuIndex, false);
                        } else if (!isDemoActive) {
                            int oldMenuIndex = currentMenuIndex;
                            currentMenuIndex = (currentMenuIndex + step + menuSize) % menuSize;
                            update_menu_selection(oldMenuIndex, currentMenuIndex);
                        }
                    }
                    xSemaphoreGive(tft_mutex);
                }
            }
        }

        // Handle continuous audio demo with smoother updates
        if (isDemoActive && !isSubMenuActive && currentMenuIndex == 1) {
            static uint64_t last_audio_update = 0;
            uint64_t current_time = esp_timer_get_time() / 1000;
            
            if (current_time - last_audio_update > 30) { // ~33 FPS
                if (xSemaphoreTake(tft_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
                    show_audio_demo(true);
                    xSemaphoreGive(tft_mutex);
                    last_audio_update = current_time;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1)); // Higher frequency for better responsiveness
    }
}

// =========================================================================
//                         D I S P L A Y   F U N C T I O N S
// =========================================================================

void draw_please_stand_by(void) {
    tft_fill_screen(ST77XX_BLACK);
    int centerX = TFT_WIDTH / 2;
    int centerY = TFT_HEIGHT / 2;
    
    // Draw radiating lines (like Arduino version)
    for(int i = 0; i < 360; i += 15) {
        float angle_rad = i * (M_PI / 180.0f);
        tft_draw_line(centerX, centerY, 
                     centerX + (int)(120 * cosf(angle_rad)), 
                     centerY + (int)(120 * sinf(angle_rad)), 
                     PB_DARK_GREEN);
    }

    // Draw concentric circles
    tft_draw_circle(centerX, centerY, 30, PB_GREEN);
    tft_draw_circle(centerX, centerY, 60, PB_DARK_GREEN);
    tft_draw_circle(centerX, centerY, 90, PB_GREEN);
    
    // Draw crosshair
    tft_draw_line(centerX - 120, centerY, centerX + 120, centerY, PB_DARK_GREEN);
    tft_draw_line(centerX, centerY - 120, centerX, centerY + 120, PB_DARK_GREEN);
    
    // Draw text (centered like Arduino version)
    tft_draw_text(centerX - 85, centerY - 15, "PLEASE", 3, PB_GREEN);
    tft_draw_text(centerX - 100, centerY + 20, "STAND BY", 3, PB_GREEN);
    
    vTaskDelay(pdMS_TO_TICKS(3000));
    tft_fill_screen(ST77XX_BLACK);
}

void draw_clock(void) {
    tft_draw_filled_rect(TFT_WIDTH - 80, 0, 80, 18, ST77XX_BLACK);
    tft_draw_text(TFT_WIDTH - 45, 5, "12:00", 1, PB_GREEN);
    
    // Draw WiFi status like Arduino version
    const char* wifi_text = "WIFI-";
    uint16_t wifi_color = PB_DARK_GREEN;
    
    if (wifi_status == 2) { // Connected
        wifi_text = "WIFI+";
        wifi_color = PB_GREEN;
    } else if (wifi_status == 1) { // Connecting
        wifi_text = "WIFI~";
        wifi_color = PB_GREEN;
    }
    
    tft_draw_filled_rect(TFT_WIDTH - 120, 0, 40, 18, ST77XX_BLACK);
    tft_draw_text(TFT_WIDTH - 110, 5, wifi_text, 1, wifi_color);
}

void draw_full_menu(int selectedIndex) {
    tft_fill_screen(ST77XX_BLACK);
    
    // Top status bar (like Arduino version)
    tft_draw_text(10, 5, "PIP-BOY MENU", 1, PB_GREEN);
    draw_clock();
    tft_draw_h_line(0, 18, TFT_WIDTH, PB_DARK_GREEN);

    // Bottom navigation bar (improved centering)
    int navY = TFT_HEIGHT - 25;
    tft_draw_h_line(0, navY - 7, TFT_WIDTH, PB_DARK_GREEN);
    
    // Calculate total width for centering (like Arduino version)
    int totalTextWidth = 0;
    const int itemSpacing = 15;
    for (int i = 0; i < menuSize; i++) {
        totalTextWidth += tft_get_text_width(menuItems[i], 2) + itemSpacing;
    }
    totalTextWidth -= itemSpacing;

    int currentX = (TFT_WIDTH - totalTextWidth) / 2;
    int textHeight = 16;

    for (int i = 0; i < menuSize; i++) {
        int textWidth = tft_get_text_width(menuItems[i], 2);
        
        // Clear area (more thorough like Arduino version)
        tft_draw_filled_rect(currentX - 5, navY - 5, textWidth + 10, textHeight + 10, ST77XX_BLACK);

        if (i == selectedIndex) {
            tft_draw_rect(currentX - 2, navY - 2, textWidth + 4, textHeight + 4, PB_GREEN);
            tft_draw_text(currentX, navY, menuItems[i], 2, PB_GREEN);
        } else {
            tft_draw_text(currentX, navY, menuItems[i], 2, PB_DARK_GREEN);
        }
        currentX += textWidth + itemSpacing;
    }

    show_menu_content(selectedIndex);
}

void show_menu_content(int index) {
    tft_draw_filled_rect(0, 20, TFT_WIDTH, TFT_HEIGHT - 50, ST77XX_BLACK);
    
    switch (index) {
        case 0: 
            draw_wifi_sub_menu(currentSubMenuIndex, true); 
            break;
        case 1: 
            show_audio_demo(false); 
            break;
        case 2: 
            show_power_screen(); 
            break;
    }
}

void update_menu_selection(int oldIndex, int newIndex) {
    if (oldIndex != newIndex) {
        show_menu_content(newIndex);
        
        // Redraw navigation bar with new selection (like Arduino version)
        int navY = TFT_HEIGHT - 25;
        int totalTextWidth = 0;
        const int itemSpacing = 15;
        
        for (int i = 0; i < menuSize; i++) {
            totalTextWidth += tft_get_text_width(menuItems[i], 2) + itemSpacing;
        }
        totalTextWidth -= itemSpacing;

        int currentX = (TFT_WIDTH - totalTextWidth) / 2;
        int textHeight = 16;

        for (int i = 0; i < menuSize; i++) {
            int textWidth = tft_get_text_width(menuItems[i], 2);
            
            tft_draw_filled_rect(currentX - 5, navY - 5, textWidth + 10, textHeight + 10, ST77XX_BLACK);

            if (i == newIndex) {
                tft_draw_rect(currentX - 2, navY - 2, textWidth + 4, textHeight + 4, PB_GREEN);
                tft_draw_text(currentX, navY, menuItems[i], 2, PB_GREEN);
            } else {
                tft_draw_text(currentX, navY, menuItems[i], 2, PB_DARK_GREEN);
            }
            currentX += textWidth + itemSpacing;
        }
    }
}

void run_menu_action(int index) {
    ESP_LOGI(TAG, "Menu action: %d", index);
    
    switch (index) {
        case 0: // WIFI: Enter Sub-Menu
            isDemoActive = true;
            isSubMenuActive = true;
            currentSubMenuIndex = 0;
            draw_wifi_sub_menu(currentSubMenuIndex, true);
            break;
            
        case 1: // AUDIO: Enter Demo Mode
            isDemoActive = true;
            show_audio_demo(false);
            break;
            
        case 2: // POWER: Halt System
            draw_shutdown_sequence(true);
            vTaskDelay(pdMS_TO_TICKS(1500));
            tft_fill_screen(ST77XX_BLACK);
            isSystemHalted = true;
            ESP_LOGW(TAG, "SYSTEM HALTED");
            break;
    }
}

// =========================================================================
//                         W I F I   S U B - M E N U
// =========================================================================

void handle_wifi_sub_menu_toggle(int index) {
    ESP_LOGI(TAG, "WiFi sub-menu action: %d", index);
    
    switch (index) {
        case 0: // CONNECT WIFI
            xEventGroupSetBits(wifi_event_group, BIT2); // Toggle WiFi
            break;
            
        case 1: // CONNECT BROKER
            isBrokerConnected = !isBrokerConnected;
            break;
            
        case 2: // BACK
            isSubMenuActive = false;
            isDemoActive = false;
            // Stop WiFi if not connected
            if (wifi_status != 2) {
                xEventGroupSetBits(wifi_event_group, BIT3); // Force disconnect
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            draw_full_menu(currentMenuIndex);
            break;
    }
}

void draw_wifi_sub_menu(int selectedIndex, bool initialDraw) {
    const int startY = 50;
    const int lineHeight = 25;
    const int startX = 20;
    
    if (initialDraw) {
        // Clear and draw header
        tft_draw_filled_rect(0, 20, TFT_WIDTH, 40, ST77XX_BLACK);
        tft_draw_text(startX + 20, 30, "NETWORK CONFIG", 2, PB_GREEN);
        tft_draw_h_line(startX + 20, 55, TFT_WIDTH - 80, PB_DARK_GREEN);
    }

    // Get WiFi status
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    bool is_connected = (bits & WIFI_CONNECTED_BIT) != 0;
    const char* wifi_status_text = "OFFLINE";
    uint16_t wifi_status_color = PB_DARK_GREEN;
    
    if (is_connected) {
        wifi_status_text = "ONLINE";
        wifi_status_color = PB_GREEN;
        wifi_status = 2;
    } else if (wifi_status == 1) {
        wifi_status_text = "CONNECTING...";
        wifi_status_color = PB_GREEN;
    }

    for (int i = 0; i < wifiSubMenuSize; i++) {
        int y = startY + i * lineHeight + 40;
        int itemW = TFT_WIDTH - startX + 5;
        
        // Clear area for the line item
        tft_draw_filled_rect(startX - 5, y - 5, TFT_WIDTH - startX + 10, lineHeight + 5, ST77XX_BLACK);
        
        if (i == selectedIndex) {
            tft_draw_rect(startX - 2, y - 2, itemW, lineHeight - 4, PB_GREEN);
            tft_draw_text(startX + 10, y + 4, wifiSubMenuItems[i], 2, PB_GREEN);
        } else {
            tft_draw_text(startX + 10, y + 4, wifiSubMenuItems[i], 2, PB_DARK_GREEN);
        }
        
        // Draw status indicators (like Arduino version)
        const char* statusText = "";
        uint16_t statusColor = PB_DARK_GREEN;

        if (i == 0) { // WIFI STATUS
            statusText = wifi_status_text;
            statusColor = wifi_status_color;
        } else if (i == 1) { // BROKER STATUS
            statusText = isBrokerConnected ? "ACTIVE" : "INACTIVE";
            statusColor = isBrokerConnected ? PB_GREEN : PB_DARK_GREEN;
        } 
        
        if (i < 2) {
            tft_draw_text(TFT_WIDTH - 75, y + 4, statusText, 1, statusColor);
        }
        
        // Show IP if connected (like Arduino version)
        if (i == 0 && is_connected) {
            tft_draw_text(TFT_WIDTH / 2 - 60, y + 20, "IP: 192.168.1.100", 1, PB_DARK_GREEN);
        }
    }
}

// =========================================================================
//                         A U D I O   D E M O
// =========================================================================

void show_audio_demo(bool running) {
    static float phase = 0.0;
    static float last_phase = 0.0;
    static float last_amplitude = 50.0;
    static uint64_t last_update = 0;
    
    const int centerY = 120;
    const int maxAmplitude = 50;
    const float frequency = 0.05;
    const int instructionY = TFT_HEIGHT - 40;

    if (!running) {
        // PREVIEW STATE
        tft_draw_filled_rect(0, 20, TFT_WIDTH, TFT_HEIGHT - 50, ST77XX_BLACK);
        tft_draw_text(40, 30, "AUDIO VISUALIZER", 2, PB_GREEN);
        tft_draw_h_line(40, 55, TFT_WIDTH - 80, PB_DARK_GREEN);
        tft_draw_text(30, 70, "Press button to activate visualizer.", 1, PB_GREEN);
        
        // Draw static preview wave
        int prevY = centerY;
        for (int x = 0; x < TFT_WIDTH; x++) {
            int y = centerY + (int)(sinf(x * 0.05) * 20);
            if (x > 0) tft_draw_line(x - 1, prevY, x, y, PB_GREEN);
            prevY = y;
        }
        
        // Reset animation state
        phase = 0.0;
        last_phase = 0.0;
        last_amplitude = maxAmplitude;
        
    } else {
        // RUNNING ANIMATION (smooth like Arduino version)
        uint64_t current_time = esp_timer_get_time() / 1000;
        
        if (current_time - last_update > 30) { // ~33 FPS
            last_update = current_time;
            
            // Vary amplitude for pulsing effect
            float current_amplitude = maxAmplitude + (sinf(current_time * 0.005) * 10);
            if (current_amplitude < 10) current_amplitude = 10;

            // 1. ERASE PREVIOUS WAVE
            int y_prev_erase = centerY + (int)(sinf(0 * frequency + last_phase) * last_amplitude);
            for (int x = 1; x < TFT_WIDTH; x++) {
                int y_current_erase = centerY + (int)(sinf(x * frequency + last_phase) * last_amplitude);
                tft_draw_line(x - 1, y_prev_erase, x, y_current_erase, ST77XX_BLACK);
                y_prev_erase = y_current_erase;
            }
            
            // 2. Calculate new phase
            phase += 0.1;
            if (phase > 2 * M_PI) phase -= 2 * M_PI;
            
            last_phase = phase;
            last_amplitude = current_amplitude;
            
            // 3. DRAW NEW WAVE
            int y_prev_draw = centerY + (int)(sinf(0 * frequency + phase) * current_amplitude);
            for (int x = 1; x < TFT_WIDTH; x++) {
                int y_current_draw = centerY + (int)(sinf(x * frequency + phase) * current_amplitude);
                tft_draw_line(x - 1, y_prev_draw, x, y_current_draw, PB_GREEN);
                y_prev_draw = y_current_draw;
            }

            // 4. Redraw fixed elements
            tft_draw_h_line(0, centerY, TFT_WIDTH, PB_DARK_GREEN);
            tft_draw_filled_rect(0, instructionY - 5, TFT_WIDTH, 15, ST77XX_BLACK);
            tft_draw_text(40, instructionY, "Press button to return to menu.", 1, PB_GREEN);
        }
    }
}

// =========================================================================
//                         P O W E R   S C R E E N
// =========================================================================

void show_power_screen(void) {
    tft_draw_filled_rect(0, 20, TFT_WIDTH, TFT_HEIGHT - 50, ST77XX_BLACK);
    draw_shutdown_sequence(false);
    tft_draw_text(60, TFT_HEIGHT - 60, "Select 'POWER' to halt the system.", 1, PB_DARK_GREEN);
}

void draw_shutdown_sequence(bool is_final) {
    tft_fill_screen(ST77XX_BLACK);
    int centerX = TFT_WIDTH / 2;
    int centerY = TFT_HEIGHT / 2;
    
    // Draw Vault-Tec style symbol (like Arduino version)
    int symbolRadius = 45;
    int barLength = 70;
    int barThickness = 8;
    int gap = 15;
    int offset = 10;
    int textY = centerY + symbolRadius + 30;

    // Outer circles
    tft_draw_circle(centerX, centerY, symbolRadius, PB_GREEN);
    tft_draw_circle(centerX, centerY, symbolRadius - 10, PB_GREEN);
    tft_draw_circle(centerX, centerY, symbolRadius - 20, PB_GREEN);
    tft_draw_circle(centerX, centerY, symbolRadius - 30, PB_GREEN);

    // Horizontal Bars
    tft_draw_filled_rect(centerX - barLength - offset, centerY - gap, barLength, barThickness, PB_GREEN);
    tft_draw_filled_rect(centerX + offset, centerY - gap, barLength, barThickness, PB_GREEN);
    
    tft_draw_filled_rect(centerX - barLength - 10 - offset, centerY - gap + barThickness + 5, barLength + 20, barThickness, PB_GREEN);
    tft_draw_filled_rect(centerX - 10 + offset, centerY - gap + barThickness + 5, barLength + 20, barThickness, PB_GREEN);

    if (is_final) {
        // Shutdown sequence text
        tft_draw_text(20, textY, "SYSTEM SHUTDOWN SEQUENCE INITIATED...", 1, PB_GREEN);
        vTaskDelay(pdMS_TO_TICKS(300));
        tft_draw_text(20, textY + 15, "CLOSING ALL MODULES...", 1, PB_GREEN);
        vTaskDelay(pdMS_TO_TICKS(300));
        tft_draw_text(20, textY + 30, "POWERING DOWN DISPLAY.", 1, PB_GREEN);
        vTaskDelay(pdMS_TO_TICKS(300));
        tft_draw_text(20, textY + 45, "GOODBYE.", 1, PB_GREEN);
        
        // Fade animation
        for (int i = 0; i < 3; i++) {
            tft_fill_screen(ST77XX_BLACK);
            vTaskDelay(pdMS_TO_TICKS(100));
            // Redraw in dim green
            tft_draw_circle(centerX, centerY, symbolRadius, PB_DARK_GREEN);
            tft_draw_circle(centerX, centerY, symbolRadius - 10, PB_DARK_GREEN);
            tft_draw_circle(centerX, centerY, symbolRadius - 20, PB_DARK_GREEN);
            tft_draw_circle(centerX, centerY, symbolRadius - 30, PB_DARK_GREEN);
            tft_draw_filled_rect(centerX - barLength - offset, centerY - gap, barLength, barThickness, PB_DARK_GREEN);
            tft_draw_filled_rect(centerX + offset, centerY - gap, barLength, barThickness, PB_DARK_GREEN);
            tft_draw_filled_rect(centerX - barLength - 10 - offset, centerY - gap + barThickness + 5, barLength + 20, barThickness, PB_DARK_GREEN);
            tft_draw_filled_rect(centerX - 10 + offset, centerY - gap + barThickness + 5, barLength + 20, barThickness, PB_DARK_GREEN);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// =========================================================================
//                         W I F I   T A S K
// =========================================================================

void wifi_connection_task(void *pvParameter) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    bool wifi_active = false;

    while(1) {
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group, 
                                             BIT2 | BIT3, 
                                             pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & BIT2) { // Toggle WiFi
            if (wifi_active) {
                ESP_LOGI(TAG, "Disconnecting WiFi");
                esp_wifi_disconnect();
                esp_wifi_stop();
                wifi_active = false;
                wifi_status = 0;
                xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
            } else {
                ESP_LOGI(TAG, "Connecting to WiFi: %s", CONFIG_PIPBOY_SSID);
                wifi_config_t wifi_config = {
                    .sta = {
                        .ssid = CONFIG_PIPBOY_SSID,
                        .password = CONFIG_PIPBOY_PASSWORD,
                    },
                };
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                ESP_ERROR_CHECK(esp_wifi_start());
                ESP_ERROR_CHECK(esp_wifi_connect());
                wifi_active = true;
                wifi_status = 1; // Connecting
            }
        }
        
        if (bits & BIT3) { // Force disconnect
            if (wifi_active) {
                esp_wifi_disconnect();
                esp_wifi_stop();
                wifi_active = false;
                wifi_status = 0;
                xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
            }
        }

        // Trigger menu redraw
        int trigger = 1;
        xQueueSend(encoder_queue, &trigger, 0);
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA started");
        wifi_status = 1; // Connecting
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected");
        wifi_status = 0;
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_status = 2;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }

    // Trigger menu redraw
    int trigger = 1;
    xQueueSend(encoder_queue, &trigger, 0);
}