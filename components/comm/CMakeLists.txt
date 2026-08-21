idf_component_register(
    SRCS "uart_send.c"
    INCLUDE_DIRS "."
    REQUIRES common esp_driver_usb_serial_jtag esp_new_jpeg driver esp_hw_support
)
