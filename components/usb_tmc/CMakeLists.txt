idf_component_register(
    SRCS 
    "src/my_usbtmc_app.c" 
    "src/usb_descriptors.c" 
    "src/usb_tmc_cb.c" 
    "src/usb_tmc_fsm_process.c"
    INCLUDE_DIRS "include" 
    REQUIRES tinyusb esp_tinyusb esp_system libscpi
    WHOLE_ARCHIVE)

