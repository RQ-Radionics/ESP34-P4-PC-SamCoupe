idf_component_register(
    SRCS "esp_lcd_lt8912b.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd driver esp_hw_support esp_timer log hal
)
