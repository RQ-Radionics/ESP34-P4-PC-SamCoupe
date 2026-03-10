idf_component_register(
    SRCS "sim_kbd.c"
    INCLUDE_DIRS "include"
    REQUIRES driver log freertos usb
    PRIV_REQUIRES hal
)
