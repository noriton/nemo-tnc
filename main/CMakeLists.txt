idf_component_register(SRCS
    "main.c"

    "hw/usb_descriptors.c"
    "hw/tnc_init.c"
    "hw/indicator.c"
    "hw/nvs_if.c"

    "port/tnc_buffer.c"
    "port/command_parser.c"
    "port/tnc_pc_port.c"

    "ax25/ax25.c"
    "ax25/ax25_fcs.c"
    "ax25/ax25_hdlc.c"
    "ax25/ax25_utils.c"
    "ax25/callsign.c"
    "ax25/kiss_parser.c"

    "packet/frame_metadata.c"
    "packet/rawpacket.c"
    "packet/packet_monitor.c"

    "packet/tx_frame.c"

INCLUDE_DIRS "." "ax25" "port" "packet" "hw" "modem"
PRIV_REQUIRES nvs_flash esp_ringbuf freertos tinyusb esp_timer console
)
