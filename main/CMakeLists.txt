# USB_TMC_APP/main/CMakeLists.txt

idf_component_register( 
    SRCS "main.c" 
    INCLUDE_DIRS "." 
    REQUIRES usb_tmc nvs_flash
)

# Puedes dejar esto si tu componente usb_tmc requiere que no se descarten objetos,
# aunque normalmente ESP-IDF gestiona bien las dependencias declaradas en REQUIRES.
target_link_options(${COMPONENT_LIB} PRIVATE "-Wl,--whole-archive" "$<TARGET_FILE:usb_tmc>" "-Wl,--no-whole-archive")
