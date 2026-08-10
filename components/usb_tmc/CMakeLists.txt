idf_component_register(
    SRCS 
    "src/usb_descriptors.c" 
    "src/usb_tmc_cb.c" 
    "src/usb_tmc_init.c" 
    "src/usb_tmc_process.c"
    INCLUDE_DIRS "include" 
    REQUIRES tinyusb esp_tinyusb esp_system libscpi scpi_app
    WHOLE_ARCHIVE)

