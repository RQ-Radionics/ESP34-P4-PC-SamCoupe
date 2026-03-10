idf_component_register(
    SRCS "sim_sdcard.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_sdmmc fatfs vfs log sdmmc
)
