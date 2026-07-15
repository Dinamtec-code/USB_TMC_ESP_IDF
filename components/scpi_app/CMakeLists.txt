idf_component_register(
    SRCS 
    "src/scpi_engine_task.c" 
    INCLUDE_DIRS "include" 
    REQUIRES usb_tmc libscpi
    WHOLE_ARCHIVE)

