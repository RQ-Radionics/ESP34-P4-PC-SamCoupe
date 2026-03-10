#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the USB HID keyboard driver.
 *
 * Deasserts HUB_RST# (GPIO21 HIGH), installs the USB host stack on PHY0
 * (peripheral_map = 0), and starts the HID host driver.
 * All USB tasks are pinned to core 0.
 *
 * PS/2 Set 2 scancode bytes are enqueued to scancode_queue as they arrive.
 * Multi-byte sequences (0xE0 xx, 0xF0 xx, 0xE0 0xF0 xx) are delivered
 * as individual bytes in order.
 *
 * @param scancode_queue  FreeRTOS queue to receive uint8_t scancode bytes.
 *                        Must not be NULL. Queue item size must be 1 byte.
 * @return ESP_OK on success, or an esp_err_t on failure.
 */
esp_err_t sim_kbd_init(QueueHandle_t scancode_queue);

/**
 * @brief Stop the USB HID keyboard driver and release resources.
 * Safe to call even if sim_kbd_init() failed.
 */
void sim_kbd_deinit(void);

#ifdef __cplusplus
}
#endif
