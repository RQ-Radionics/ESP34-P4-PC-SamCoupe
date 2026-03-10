/*
 * sim_sdcard.c — SD card driver for SimCoupe (Olimex ESP32-P4-PC)
 *
 * Mounts the SD card via SDMMC host (Slot 0, 4-bit) at /sdcard.
 *
 * Hardware (Olimex ESP32-P4-PC / Waveshare ESP32-P4-WIFI6):
 *   CLK:  GPIO43
 *   CMD:  GPIO44
 *   D0:   GPIO39
 *   D1:   GPIO40
 *   D2:   GPIO41
 *   D3:   GPIO42
 *
 * The SDMMC IO pins on ESP32-P4 are powered by internal LDO_VO4.
 * Without enabling it via sd_pwr_ctrl_new_on_chip_ldo(), card init
 * times out immediately (ESP_ERR_TIMEOUT).
 */

#include "sim_sdcard.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

static const char *TAG = "sim_sdcard";
static sdmmc_card_t      *s_card       = NULL;
static sd_pwr_ctrl_handle_t s_pwr_handle = NULL;

esp_err_t sim_sdcard_mount(void)
{
    if (s_card) return ESP_ERR_INVALID_STATE;  /* already mounted */

    /* Enable LDO_VO4 to power the SDMMC IO pins on ESP32-P4 */
    sd_pwr_ctrl_ldo_config_t ldo_cfg = { .ldo_chan_id = 4 };
    esp_err_t ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &s_pwr_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD LDO init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 16,
        .allocation_unit_size   = 16 * 1024,
    };

    sdmmc_host_t host    = SDMMC_HOST_DEFAULT();
    host.slot            = SDMMC_HOST_SLOT_0;
    host.max_freq_khz    = SDMMC_FREQ_DEFAULT;  /* 20 MHz — safe for all cards */
    host.pwr_ctrl_handle = s_pwr_handle;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk   = 43;
    slot.cmd   = 44;
    slot.d0    = 39;
    slot.d1    = 40;
    slot.d2    = 41;
    slot.d3    = 42;
    slot.cd    = SDMMC_SLOT_NO_CD;   /* no Card Detect pin */
    slot.wp    = SDMMC_SLOT_NO_WP;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        sd_pwr_ctrl_del_on_chip_ldo(s_pwr_handle);
        s_pwr_handle = NULL;
        s_card = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "SD card mounted at /sdcard (%lluMB)",
             ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024));
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

bool sim_sdcard_mounted(void)
{
    return s_card != NULL;
}
