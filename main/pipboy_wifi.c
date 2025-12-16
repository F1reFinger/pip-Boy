#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "pipboy_state.h"

static const char *TAG = "WIFI_TASK";

// --- Configuration from Kconfig ---
#ifndef CONFIG_PIPBOY_SSID
#define CONFIG_PIPBOY_SSID "DEFAULT_SSID"
#endif

#ifndef CONFIG_PIPBOY_PASSWORD
#define CONFIG_PIPBOY_PASSWORD "DEFAULT_PASS"
#endif

// Event handler for WiFi and IP events
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi Disconnected");
        xEventGroupClearBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
        // Trigger a redraw in case we are in the WiFi menu
        int redraw_flag = 1; 
        xQueueSend(g_menu_update_queue, &redraw_flag, portMAX_DELAY);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
        // Trigger a redraw to show the connection status
        int redraw_flag = 1; 
        xQueueSend(g_menu_update_queue, &redraw_flag, portMAX_DELAY);
    }
}

// TASK 4: Manages WiFi Connection using ESP-IDF API
void wifi_connection_task(void *pvParameter) {
    // 1. Initialize Network Stack and Event Loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // 2. Register Event Handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    // 3. Initialize WiFi Driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Wait for the logic task to trigger connection, or handle internal toggling
    while(1) {
        // Wait for the toggle command (BIT2) from the menu logic task
        EventBits_t bits = xEventGroupWaitBits(g_wifi_event_group,
                WIFI_TOGGLE_BIT, // Wait only for the toggle command
                pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & WIFI_TOGGLE_BIT) {
            // Toggle command received
            wifi_mode_t current_mode;
            if (esp_wifi_get_mode(&current_mode) != ESP_OK) {
                current_mode = WIFI_MODE_NULL; // Default if check fails
            }
            
            if (current_mode == WIFI_MODE_STA) {
                // If currently STA mode (connected or attempting), disconnect
                if (esp_wifi_stop() == ESP_OK) { // Use stop for a full reset/disconnect
                    ESP_LOGI(TAG, "WiFi Stop requested (Disabling STA mode).");
                    xEventGroupClearBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
                }
            } else {
                // Not connected, start the connection process
                wifi_config_t wifi_config = {
                    .sta = {
                        .ssid = CONFIG_PIPBOY_SSID,
                        .password = CONFIG_PIPBOY_PASSWORD,
                        .scan_method = WIFI_FAST_SCAN,
                        .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
                        .threshold = { .rssi = -127, .authmode = WIFI_AUTH_OPEN },
                    },
                };
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                ESP_ERROR_CHECK(esp_wifi_start());
                ESP_ERROR_CHECK(esp_wifi_connect());
                ESP_LOGI(TAG, "WiFi Connect requested for %s", CONFIG_PIPBOY_SSID);
            }
        }
    }
}