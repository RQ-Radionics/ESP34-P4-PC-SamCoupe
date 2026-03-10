idf_component_register(
    SRCS "sim_display.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_i2c
        esp_driver_gpio
        esp_lcd
        esp_hw_support
        esp_mm
        log
        freertos
        esp_lcd_lt8912b
)
