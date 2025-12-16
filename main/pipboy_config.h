#ifndef PIPBOY_CONFIG_H
#define PIPBOY_CONFIG_H

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// --- TFT PIN CONFIG (Working Pins) ---
#define TFT_HOST    SPI2_HOST // Commonly used SPI Host for ESP32
#define TFT_CS      5         // Chip Select (CS)
#define TFT_RST     4         // Reset (RST)
#define TFT_DC      16        // Data/Command (DC)
#define TFT_MOSI    23        // Master Out Slave In (Data)
#define TFT_SCLK    18        // Serial Clock (SCLK)
#define TFT_BCKL    -1        // Backlight pin (set to GPIO pin number if used)

// Display dimensions
#define LCD_H_RES   320 // 320x240 landscape
#define LCD_V_RES   240 

// --- ROTARY ENCODER PIN CONFIG ---
#define ROTARY_ENCODER_CLK_PIN 32
#define ROTARY_ENCODER_DT_PIN 33
#define ROTARY_ENCODER_SW_PIN  27

// --- COMPATIBILITY ALIASES for app_main.c ---
// These aliases ensure app_main.c compiles while using the ROTARY_ENCODER_* definitions.
#define PIN_ENCODER_CLK ROTARY_ENCODER_CLK_PIN
#define PIN_ENCODER_DT  ROTARY_ENCODER_DT_PIN
#define PIN_ENCODER_SW  ROTARY_ENCODER_SW_PIN

#endif // PIPBOY_CONFIG_H