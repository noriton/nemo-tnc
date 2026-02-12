idf_component_register(SRCS 
    "main.c" 
    "usb_descriptors.c" 
    "tnc_init.c"
    "indicator.c"
    "tnc_buffer.c"
    "command_parser.c"
    "tnc_settings.c"
    "tx_frame.c"
    "rx_frame.c"
    "ax25.c"
    "ax25_utils.c"
    "ax25_fcs.c"
    "tnc_pc_port.c"
INCLUDE_DIRS "."
PRIV_REQUIRES nvs_flash esp_ringbuf freertos tinyusb esp_timer console
)