#ifndef PIPBOY_ENCODER_H
#define PIPBOY_ENCODER_H

#include "esp_err.h"

/**
 * @brief Initializes GPIOs for the rotary encoder and starts the processing task.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t pipboy_encoder_init(void);

#endif // PIPBOY_ENCODER_H