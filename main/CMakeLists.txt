idf_component_register(SRCS 
    "main.c" 
    "usb_descriptors.c" 
    "tnc_init.c"
    "indicator.c"
    "tnc_buffer.c"
    "command_parser.c"
    "tnc_settings.c"
    "ax25_utils.c"
INCLUDE_DIRS "."
PRIV_REQUIRES nvs_flash esp_ringbuf freertos tinyusb esp_timer
)