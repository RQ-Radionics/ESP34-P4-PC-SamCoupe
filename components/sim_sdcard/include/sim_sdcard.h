#pragma once
#include "esp_err.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Mount the SD card via SDMMC.
 *
 * Mounts the SD card at /sdcard using SDMMC host with 4-bit bus width.
 * GPIO pins are configured via Kconfig (CONFIG_SIM_SDCARD_*).
 *
 * @return ESP_OK on success, or an esp_err_t on failure.
 */
esp_err_t sim_sdcard_mount(void);

/**
 * @brief Check if the SD card is currently mounted.
 *
 * @return true if mounted, false otherwise.
 */
bool sim_sdcard_mounted(void);

#ifdef __cplusplus
}
#endif
