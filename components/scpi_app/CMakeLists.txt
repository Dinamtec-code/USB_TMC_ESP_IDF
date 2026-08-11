idf_component_register(
    SRCS 
    "src/scpi_engine.c" 
    "src/iface_msg.c" 
    "src/scpi_drv_iface.c" 
    INCLUDE_DIRS "include" 
    REQUIRES usb_tmc libscpi esp_system
    WHOLE_ARCHIVE)

