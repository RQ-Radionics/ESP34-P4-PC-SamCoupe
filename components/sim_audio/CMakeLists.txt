if(CONFIG_SIM_AUDIO_ENABLED)
    idf_component_register(
        SRCS "sim_audio.c" "es8311_local.c"
        INCLUDE_DIRS "." "include"
        REQUIRES esp_driver_i2s esp_driver_i2c esp_driver_gpio log esp_common
    )
else()
    idf_component_register(
        INCLUDE_DIRS "." "include"
        REQUIRES esp_driver_i2s esp_driver_i2c esp_driver_gpio log esp_common
    )
endif()
