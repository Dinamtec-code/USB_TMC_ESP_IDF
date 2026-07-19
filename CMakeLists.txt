# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

add_compile_definitions(
    CFG_TUD_USBTMC=1
    CFG_TUSB_CONFIG_FILE="usb_tmc_config.h" 
)
include_directories("components/usb_tmc/include")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(COMPILE_DEFINITIONS "CFG_TUD_TMC=1" APPEND)

project(usb_tmc_app)
