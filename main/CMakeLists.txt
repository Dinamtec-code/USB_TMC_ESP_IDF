idf_component_register( 
    SRCS "main.c" 
    INCLUDE_DIRS "." 
    REQUIRES usb_tmc nvs_flash
)

# Esto fuerza a incluir todos los objetos de tu componente en el binario final
target_link_options(${COMPONENT_LIB} PRIVATE "-Wl,--whole-archive" "$<TARGET_FILE:usb_tmc>" "-Wl,--no-whole-archive")
# Esto fuerza a incluir el objeto específico de los descriptores
target_link_options(${COMPONENT_LIB} PRIVATE "-Wl,--undefined=tud_descriptor_device_cb")
target_link_options(${COMPONENT_LIB} PRIVATE "-Wl,--undefined=tud_descriptor_configuration_cb")
target_link_options(${COMPONENT_LIB} PRIVATE "-Wl,--undefined=tud_descriptor_string_cb")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=tud_descriptor_device_cb")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=tud_descriptor_configuration_cb")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=tud_descriptor_string_cb")


